//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================
#include <ripple/app/misc/AMMUtils.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STObject.h>

namespace ripple {

std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Asset const& asset1,
    Asset const& asset2,
    FreezeHandling freezeHandling,
    beast::Journal const j)
{
    auto const assetInBalance =
        accountHolds(view, ammAccountID, asset1, freezeHandling, j);
    auto const assetOutBalance =
        accountHolds(view, ammAccountID, asset2, freezeHandling, j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Asset> const& optAsset1,
    std::optional<Asset> const& optAsset2,
    FreezeHandling freezeHandling,
    beast::Journal const j)
{
    auto const assets = [&]() -> std::optional<std::pair<Asset, Asset>> {
        auto const asset1 = ammSle[sfAsset];
        auto const asset2 = ammSle[sfAsset2];
        if (optAsset1 && optAsset2)
        {
            if (invalidAMMAssetPair(
                    *optAsset1,
                    *optAsset2,
                    std::make_optional(std::make_pair(asset1, asset2))))
            {
                JLOG(j.debug()) << "ammHolds: Invalid optAsset1 or optAsset2 "
                                << *optAsset1 << " " << *optAsset2;
                return std::nullopt;
            }
            return std::make_optional(std::make_pair(*optAsset1, *optAsset2));
        }
        auto const singleAsset =
            [&asset1, &asset2, &j](
                Asset checkAsset,
                const char* label) -> std::optional<std::pair<Asset, Asset>> {
            if (checkAsset == asset1)
                return std::make_optional(std::make_pair(asset1, asset2));
            else if (checkAsset == asset2)
                return std::make_optional(std::make_pair(asset2, asset1));
            JLOG(j.debug())
                << "ammHolds: Invalid " << label << " " << checkAsset;
            return std::nullopt;
        };
        if (optAsset1)
        {
            return singleAsset(*optAsset1, "optAsset1");
        }
        else if (optAsset2)
        {
            return singleAsset(*optAsset2, "optAsset2");
        }
        return std::make_optional(std::make_pair(asset1, asset2));
    }();
    if (!assets)
        return Unexpected(tecAMM_INVALID_TOKENS);
    auto const [amount1, amount2] = ammPoolHolds(
        view,
        ammSle.getAccountID(sfAccount),
        assets->first,
        assets->second,
        freezeHandling,
        j);
    return std::make_tuple(amount1, amount2, ammSle[sfLPTokenBalance]);
}

STAmount
ammLPHolds(
    ReadView const& view,
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    return accountHolds(
        view,
        lpAccount,
        ammLPTCurrency(asset1, asset2),
        ammAccount,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
}

STAmount
ammLPHolds(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    return ammLPHolds(
        view,
        ammSle[sfAsset],
        ammSle[sfAsset2],
        ammSle[sfAccount],
        lpAccount,
        j);
}

std::uint16_t
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account)
{
    using namespace std::chrono;
    assert(
        !view.rules().enabled(fixInnerObjTemplate) ||
        ammSle.isFieldPresent(sfAuctionSlot));
    if (ammSle.isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(ammSle.peekAtField(sfAuctionSlot));
        // Not expired
        if (auto const expiration = auctionSlot[~sfExpiration];
            duration_cast<seconds>(
                view.info().parentCloseTime.time_since_epoch())
                .count() < expiration)
        {
            if (auctionSlot[~sfAccount] == account)
                return auctionSlot[sfDiscountedFee];
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                for (auto const& acct :
                     auctionSlot.getFieldArray(sfAuthAccounts))
                    if (acct[~sfAccount] == account)
                        return auctionSlot[sfDiscountedFee];
            }
        }
    }
    return ammSle[sfTradingFee];
}

STAmount
ammAccountHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Asset const& asset)
{
    if (asset.isMPT())
        return accountHolds(view, ammAccountID, asset.mptIssue());

    if (isXRP(asset))
    {
        if (auto const sle = view.read(keylet::account(ammAccountID)))
            return (*sle)[sfBalance];
    }
    else if (auto const sle = view.read(keylet::line(
                 ammAccountID, asset.issue().account, asset.issue().currency));
             sle &&
             !isFrozen(
                 view,
                 ammAccountID,
                 asset.issue().currency,
                 asset.issue().account))
    {
        auto amount = (*sle)[sfBalance];
        if (ammAccountID > asset.issue().account)
            amount.negate();
        amount.setIssuer(asset.issue().account);
        return amount;
    }

    return STAmount{asset};
}

static TER
deleteAMMTrustLines(
    Sandbox& sb,
    AccountID const& ammAccountID,
    std::uint16_t maxTrustlinesToDelete,
    beast::Journal j)
{
    return cleanupOnAccountDelete(
        sb,
        keylet::ownerDir(ammAccountID),
        [&](LedgerEntryType nodeType,
            uint256 const&,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            // Skip AMM and MPT
            if (nodeType == LedgerEntryType::ltAMM ||
                nodeType == LedgerEntryType::ltMPTOKEN ||
                nodeType == LedgerEntryType::ltMPTOKEN_ISSUANCE)
                return {tesSUCCESS, SkipEntry::Yes};
            // Should only have the trustlines
            if (nodeType != LedgerEntryType::ltRIPPLE_STATE)
            {
                JLOG(j.error())
                    << "deleteAMMTrustLines: deleting non-trustline "
                    << nodeType;
                return {tecINTERNAL, SkipEntry::No};
            }

            // Trustlines must have zero balance
            if (sleItem->getFieldAmount(sfBalance) != beast::zero)
            {
                JLOG(j.error())
                    << "deleteAMMTrustLines: deleting trustline with "
                       "non-zero balance.";
                return {tecINTERNAL, SkipEntry::No};
            }

            return {
                deleteAMMTrustLine(sb, sleItem, ammAccountID, j),
                SkipEntry::No};
        },
        j,
        maxTrustlinesToDelete);
}

TER
deleteAMMAccount(
    Sandbox& sb,
    Asset const& asset,
    Asset const& asset2,
    beast::Journal j)
{
    auto ammSle = sb.peek(keylet::amm(asset, asset2));
    if (!ammSle)
    {
        JLOG(j.error()) << "deleteAMMAccount: AMM object does not exist "
                        << asset << " " << asset2;
        return tecINTERNAL;
    }

    auto const ammAccountID = (*ammSle)[sfAccount];
    auto sleAMMRoot = sb.peek(keylet::account(ammAccountID));
    if (!sleAMMRoot)
    {
        JLOG(j.error()) << "deleteAMMAccount: AMM account does not exist "
                        << to_string(ammAccountID);
        return tecINTERNAL;
    }

    if (auto const ter =
            deleteAMMTrustLines(sb, ammAccountID, maxDeletableAMMTrustLines, j);
        ter != tesSUCCESS)
        return ter;

    auto checkDeleteMPToken = [&](Asset const& asset_) -> TER {
        if (asset_.isMPT())
        {
            auto const mptIssuanceID =
                keylet::mptIssuance(asset_.mptIssue().mpt());
            auto const mptokenKey =
                keylet::mptoken(mptIssuanceID.key, ammAccountID);

            auto const sleMpt = sb.peek(mptokenKey);
            if (!sleMpt)
                return tecINTERNAL;

            if (!sb.dirRemove(
                    keylet::ownerDir(ammAccountID),
                    (*sleMpt)[sfOwnerNode],
                    sleMpt->key(),
                    false))
                return tecINTERNAL;

            sb.erase(sleMpt);
        }

        return tesSUCCESS;
    };

    if (auto const err = checkDeleteMPToken(asset))
        return err;

    if (auto const err = checkDeleteMPToken(asset2))
        return err;

    auto const ownerDirKeylet = keylet::ownerDir(ammAccountID);
    if (!sb.dirRemove(
            ownerDirKeylet, (*ammSle)[sfOwnerNode], ammSle->key(), false))
    {
        JLOG(j.error()) << "deleteAMMAccount: failed to remove dir link";
        return tecINTERNAL;
    }
    if (sb.exists(ownerDirKeylet) && !sb.emptyDirDelete(ownerDirKeylet))
    {
        JLOG(j.error()) << "deleteAMMAccount: cannot delete root dir node of "
                        << toBase58(ammAccountID);
        return tecINTERNAL;
    }

    sb.erase(ammSle);
    sb.erase(sleAMMRoot);

    return tesSUCCESS;
}

void
initializeFeeAuctionVote(
    ApplyView& view,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account,
    Issue const& lptIssue,
    std::uint16_t tfee)
{
    auto const& rules = view.rules();
    // AMM creator gets the voting slot.
    STArray voteSlots;
    STObject voteEntry = STObject::makeInnerObject(sfVoteEntry, rules);
    if (tfee != 0)
        voteEntry.setFieldU16(sfTradingFee, tfee);
    voteEntry.setFieldU32(sfVoteWeight, VOTE_WEIGHT_SCALE_FACTOR);
    voteEntry.setAccountID(sfAccount, account);
    voteSlots.push_back(voteEntry);
    ammSle->setFieldArray(sfVoteSlots, voteSlots);
    // AMM creator gets the auction slot for free.
    // AuctionSlot is created on AMMCreate and updated on AMMDeposit
    // when AMM is in an empty state
    if (rules.enabled(fixInnerObjTemplate) &&
        !ammSle->isFieldPresent(sfAuctionSlot))
    {
        STObject auctionSlot = STObject::makeInnerObject(sfAuctionSlot, rules);
        ammSle->set(std::move(auctionSlot));
    }
    STObject& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
    auctionSlot.setAccountID(sfAccount, account);
    // current + sec in 24h
    auto const expiration = std::chrono::duration_cast<std::chrono::seconds>(
                                view.info().parentCloseTime.time_since_epoch())
                                .count() +
        TOTAL_TIME_SLOT_SECS;
    auctionSlot.setFieldU32(sfExpiration, expiration);
    auctionSlot.setFieldAmount(sfPrice, STAmount{lptIssue, 0});
    // Set the fee
    if (tfee != 0)
        ammSle->setFieldU16(sfTradingFee, tfee);
    else if (ammSle->isFieldPresent(sfTradingFee))
        ammSle->makeFieldAbsent(sfTradingFee);
    if (auto const dfee = tfee / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION)
        auctionSlot.setFieldU16(sfDiscountedFee, dfee);
    else if (auctionSlot.isFieldPresent(sfDiscountedFee))
        auctionSlot.makeFieldAbsent(sfDiscountedFee);
}

}  // namespace ripple
