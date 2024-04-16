//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/InvariantCheck.h>

#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/app/misc/AMMUtils.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/nftPageMask.h>

namespace ripple {

void
TransactionFeeCheck::visitEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // nothing to do
}

bool
TransactionFeeCheck::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const fee,
    ReadView const&,
    beast::Journal const& j)
{
    // We should never charge a negative fee
    if (fee.drops() < 0)
    {
        JLOG(j.fatal()) << "Invariant failed: fee paid was negative: "
                        << fee.drops();
        return false;
    }

    // We should never charge a fee that's greater than or equal to the
    // entire XRP supply.
    if (fee >= INITIAL_XRP)
    {
        JLOG(j.fatal()) << "Invariant failed: fee paid exceeds system limit: "
                        << fee.drops();
        return false;
    }

    // We should never charge more for a transaction than the transaction
    // authorizes. It's possible to charge less in some circumstances.
    if (fee > tx.getFieldAmount(sfFee).xrp())
    {
        JLOG(j.fatal()) << "Invariant failed: fee paid is " << fee.drops()
                        << " exceeds fee specified in transaction.";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
XRPNotCreated::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    /* We go through all modified ledger entries, looking only at account roots,
     * escrow payments, and payment channels. We remove from the total any
     * previous XRP values and add to the total any new XRP values. The net
     * balance of a payment channel is computed from two fields (amount and
     * balance) and deletions are ignored for paychan and escrow because the
     * amount fields have not been adjusted for those in the case of deletion.
     */
    if (before)
    {
        switch (before->getType())
        {
            case ltACCOUNT_ROOT:
                drops_ -= (*before)[sfBalance].xrp().drops();
                break;
            case ltPAYCHAN:
                drops_ -=
                    ((*before)[sfAmount] - (*before)[sfBalance]).xrp().drops();
                break;
            case ltESCROW:
                drops_ -= (*before)[sfAmount].xrp().drops();
                break;
            default:
                break;
        }
    }

    if (after)
    {
        switch (after->getType())
        {
            case ltACCOUNT_ROOT:
                drops_ += (*after)[sfBalance].xrp().drops();
                break;
            case ltPAYCHAN:
                if (!isDelete)
                    drops_ += ((*after)[sfAmount] - (*after)[sfBalance])
                                  .xrp()
                                  .drops();
                break;
            case ltESCROW:
                if (!isDelete)
                    drops_ += (*after)[sfAmount].xrp().drops();
                break;
            default:
                break;
        }
    }
}

bool
XRPNotCreated::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const fee,
    ReadView const&,
    beast::Journal const& j)
{
    // The net change should never be positive, as this would mean that the
    // transaction created XRP out of thin air. That's not possible.
    if (drops_ > 0)
    {
        JLOG(j.fatal()) << "Invariant failed: XRP net change was positive: "
                        << drops_;
        return false;
    }

    // The negative of the net change should be equal to actual fee charged.
    if (-drops_ != fee.drops())
    {
        JLOG(j.fatal()) << "Invariant failed: XRP net change of " << drops_
                        << " doesn't match fee " << fee.drops();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
XRPBalanceChecks::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto isBad = [](STAmount const& balance) {
        if (!balance.native())
            return true;

        auto const drops = balance.xrp();

        // Can't have more than the number of drops instantiated
        // in the genesis ledger.
        if (drops > INITIAL_XRP)
            return true;

        // Can't have a negative balance (0 is OK)
        if (drops < XRPAmount{0})
            return true;

        return false;
    };

    if (before && before->getType() == ltACCOUNT_ROOT)
        bad_ |= isBad((*before)[sfBalance]);

    if (after && after->getType() == ltACCOUNT_ROOT)
        bad_ |= isBad((*after)[sfBalance]);
}

bool
XRPBalanceChecks::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: incorrect account XRP balance";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
NoBadOffers::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto isBad = [](STAmount const& pays, STAmount const& gets) {
        // An offer should never be negative
        if (pays < beast::zero)
            return true;

        if (gets < beast::zero)
            return true;

        // Can't have an XRP to XRP offer:
        return pays.native() && gets.native();
    };

    if (before && before->getType() == ltOFFER)
        bad_ |= isBad((*before)[sfTakerPays], (*before)[sfTakerGets]);

    if (after && after->getType() == ltOFFER)
        bad_ |= isBad((*after)[sfTakerPays], (*after)[sfTakerGets]);
}

bool
NoBadOffers::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: offer with a bad amount";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
NoZeroEscrow::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto isBad = [](STAmount const& amount) {
        if (!amount.native())
            return true;

        if (amount.xrp() <= XRPAmount{0})
            return true;

        if (amount.xrp() >= INITIAL_XRP)
            return true;

        return false;
    };

    if (before && before->getType() == ltESCROW)
        bad_ |= isBad((*before)[sfAmount]);

    if (after && after->getType() == ltESCROW)
        bad_ |= isBad((*after)[sfAmount]);
}

bool
NoZeroEscrow::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: escrow specifies invalid amount";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
AccountRootsNotDeleted::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const&)
{
    if (isDelete && before && before->getType() == ltACCOUNT_ROOT)
        accountsDeleted_++;
}

bool
AccountRootsNotDeleted::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    // AMM account root can be deleted as the result of AMM withdraw/delete
    // transaction when the total AMM LP Tokens balance goes to 0.
    // A successful AccountDelete or AMMDelete MUST delete exactly
    // one account root.
    if ((tx.getTxnType() == ttACCOUNT_DELETE ||
         tx.getTxnType() == ttAMM_DELETE) &&
        result == tesSUCCESS)
    {
        if (accountsDeleted_ == 1)
            return true;

        if (accountsDeleted_ == 0)
            JLOG(j.fatal()) << "Invariant failed: account deletion "
                               "succeeded without deleting an account";
        else
            JLOG(j.fatal()) << "Invariant failed: account deletion "
                               "succeeded but deleted multiple accounts!";
        return false;
    }

    // A successful AMMWithdraw MAY delete one account root
    // when the total AMM LP Tokens balance goes to 0. Not every AMM withdraw
    // deletes the AMM account, accountsDeleted_ is set if it is deleted.
    if (tx.getTxnType() == ttAMM_WITHDRAW && result == tesSUCCESS &&
        accountsDeleted_ == 1)
        return true;

    if (accountsDeleted_ == 0)
        return true;

    JLOG(j.fatal()) << "Invariant failed: an account root was deleted";
    return false;
}

//------------------------------------------------------------------------------

void
LedgerEntryTypesMatch::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && after && before->getType() != after->getType())
        typeMismatch_ = true;

    if (after)
    {
        switch (after->getType())
        {
            case ltACCOUNT_ROOT:
            case ltDIR_NODE:
            case ltRIPPLE_STATE:
            case ltTICKET:
            case ltSIGNER_LIST:
            case ltOFFER:
            case ltLEDGER_HASHES:
            case ltAMENDMENTS:
            case ltFEE_SETTINGS:
            case ltESCROW:
            case ltPAYCHAN:
            case ltCHECK:
            case ltDEPOSIT_PREAUTH:
            case ltNEGATIVE_UNL:
            case ltNFTOKEN_PAGE:
            case ltNFTOKEN_OFFER:
            case ltAMM:
            case ltBRIDGE:
            case ltXCHAIN_OWNED_CLAIM_ID:
            case ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID:
            case ltDID:
            case ltORACLE:
                break;
            default:
                invalidTypeAdded_ = true;
                break;
        }
    }
}

bool
LedgerEntryTypesMatch::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if ((!typeMismatch_) && (!invalidTypeAdded_))
        return true;

    if (typeMismatch_)
    {
        JLOG(j.fatal()) << "Invariant failed: ledger entry type mismatch";
    }

    if (invalidTypeAdded_)
    {
        JLOG(j.fatal()) << "Invariant failed: invalid ledger entry type added";
    }

    return false;
}

//------------------------------------------------------------------------------

void
NoXRPTrustLines::visitEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltRIPPLE_STATE)
    {
        // checking the issue directly here instead of
        // relying on .native() just in case native somehow
        // were systematically incorrect
        xrpTrustLine_ =
            after->getFieldAmount(sfLowLimit).issue() == xrpIssue() ||
            after->getFieldAmount(sfHighLimit).issue() == xrpIssue();
    }
}

bool
NoXRPTrustLines::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (!xrpTrustLine_)
        return true;

    JLOG(j.fatal()) << "Invariant failed: an XRP trust line was created";
    return false;
}

//------------------------------------------------------------------------------

void
ValidNewAccountRoot::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (!before && after->getType() == ltACCOUNT_ROOT)
    {
        accountsCreated_++;
        accountSeq_ = (*after)[sfSequence];
    }
}

bool
ValidNewAccountRoot::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (accountsCreated_ == 0)
        return true;

    if (accountsCreated_ > 1)
    {
        JLOG(j.fatal()) << "Invariant failed: multiple accounts "
                           "created in a single transaction";
        return false;
    }

    // From this point on we know exactly one account was created.
    if ((tx.getTxnType() == ttPAYMENT || tx.getTxnType() == ttAMM_CREATE ||
         tx.getTxnType() == ttXCHAIN_ADD_CLAIM_ATTESTATION ||
         tx.getTxnType() == ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION) &&
        result == tesSUCCESS)
    {
        std::uint32_t const startingSeq{
            view.rules().enabled(featureDeletableAccounts) ? view.seq() : 1};

        if (accountSeq_ != startingSeq)
        {
            JLOG(j.fatal()) << "Invariant failed: account created with "
                               "wrong starting sequence number";
            return false;
        }
        return true;
    }

    JLOG(j.fatal()) << "Invariant failed: account root created "
                       "by a non-Payment, by an unsuccessful transaction, "
                       "or by AMM";
    return false;
}

//------------------------------------------------------------------------------

void
ValidNFTokenPage::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    static constexpr uint256 const& pageBits = nft::pageMask;
    static constexpr uint256 const accountBits = ~pageBits;

    auto check = [this, isDelete](std::shared_ptr<SLE const> const& sle) {
        uint256 const account = sle->key() & accountBits;
        uint256 const hiLimit = sle->key() & pageBits;
        std::optional<uint256> const prev = (*sle)[~sfPreviousPageMin];

        // Make sure that any page links...
        //  1. Are properly associated with the owning account and
        //  2. The page is correctly ordered between links.
        if (prev)
        {
            if (account != (*prev & accountBits))
                badLink_ = true;

            if (hiLimit <= (*prev & pageBits))
                badLink_ = true;
        }

        if (auto const next = (*sle)[~sfNextPageMin])
        {
            if (account != (*next & accountBits))
                badLink_ = true;

            if (hiLimit >= (*next & pageBits))
                badLink_ = true;
        }

        {
            auto const& nftokens = sle->getFieldArray(sfNFTokens);

            // An NFTokenPage should never contain too many tokens or be empty.
            if (std::size_t const nftokenCount = nftokens.size();
                (!isDelete && nftokenCount == 0) ||
                nftokenCount > dirMaxTokensPerPage)
                invalidSize_ = true;

            // If prev is valid, use it to establish a lower bound for
            // page entries.  If prev is not valid the lower bound is zero.
            uint256 const loLimit =
                prev ? *prev & pageBits : uint256(beast::zero);

            // Also verify that all NFTokenIDs in the page are sorted.
            uint256 loCmp = loLimit;
            for (auto const& obj : nftokens)
            {
                uint256 const tokenID = obj[sfNFTokenID];
                if (!nft::compareTokens(loCmp, tokenID))
                    badSort_ = true;
                loCmp = tokenID;

                // None of the NFTs on this page should belong on lower or
                // higher pages.
                if (uint256 const tokenPageBits = tokenID & pageBits;
                    tokenPageBits < loLimit || tokenPageBits >= hiLimit)
                    badEntry_ = true;

                if (auto uri = obj[~sfURI]; uri && uri->empty())
                    badURI_ = true;
            }
        }
    };

    if (before && before->getType() == ltNFTOKEN_PAGE)
        check(before);

    if (after && after->getType() == ltNFTOKEN_PAGE)
        check(after);
}

bool
ValidNFTokenPage::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (badLink_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT page is improperly linked.";
        return false;
    }

    if (badEntry_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT found in incorrect page.";
        return false;
    }

    if (badSort_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFTs on page are not sorted.";
        return false;
    }

    if (badURI_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT contains empty URI.";
        return false;
    }

    if (invalidSize_)
    {
        JLOG(j.fatal()) << "Invariant failed: NFT page has invalid size.";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
void
NFTokenCountTracking::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (before && before->getType() == ltACCOUNT_ROOT)
    {
        beforeMintedTotal += (*before)[~sfMintedNFTokens].value_or(0);
        beforeBurnedTotal += (*before)[~sfBurnedNFTokens].value_or(0);
    }

    if (after && after->getType() == ltACCOUNT_ROOT)
    {
        afterMintedTotal += (*after)[~sfMintedNFTokens].value_or(0);
        afterBurnedTotal += (*after)[~sfBurnedNFTokens].value_or(0);
    }
}

bool
NFTokenCountTracking::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (TxType const txType = tx.getTxnType();
        txType != ttNFTOKEN_MINT && txType != ttNFTOKEN_BURN)
    {
        if (beforeMintedTotal != afterMintedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: the number of minted tokens "
                               "changed without a mint transaction!";
            return false;
        }

        if (beforeBurnedTotal != afterBurnedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: the number of burned tokens "
                               "changed without a burn transaction!";
            return false;
        }

        return true;
    }

    if (tx.getTxnType() == ttNFTOKEN_MINT)
    {
        if (result == tesSUCCESS && beforeMintedTotal >= afterMintedTotal)
        {
            JLOG(j.fatal())
                << "Invariant failed: successful minting didn't increase "
                   "the number of minted tokens.";
            return false;
        }

        if (result != tesSUCCESS && beforeMintedTotal != afterMintedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: failed minting changed the "
                               "number of minted tokens.";
            return false;
        }

        if (beforeBurnedTotal != afterBurnedTotal)
        {
            JLOG(j.fatal())
                << "Invariant failed: minting changed the number of "
                   "burned tokens.";
            return false;
        }
    }

    if (tx.getTxnType() == ttNFTOKEN_BURN)
    {
        if (result == tesSUCCESS)
        {
            if (beforeBurnedTotal >= afterBurnedTotal)
            {
                JLOG(j.fatal())
                    << "Invariant failed: successful burning didn't increase "
                       "the number of burned tokens.";
                return false;
            }
        }

        if (result != tesSUCCESS && beforeBurnedTotal != afterBurnedTotal)
        {
            JLOG(j.fatal()) << "Invariant failed: failed burning changed the "
                               "number of burned tokens.";
            return false;
        }

        if (beforeMintedTotal != afterMintedTotal)
        {
            JLOG(j.fatal())
                << "Invariant failed: burning changed the number of "
                   "minted tokens.";
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------

void
ValidClawback::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const&)
{
    if (before && before->getType() == ltRIPPLE_STATE)
        trustlinesChanged++;
}

bool
ValidClawback::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (tx.getTxnType() != ttCLAWBACK)
        return true;

    if (result == tesSUCCESS)
    {
        if (trustlinesChanged > 1)
        {
            JLOG(j.fatal())
                << "Invariant failed: more than one trustline changed.";
            return false;
        }

        AccountID const issuer = tx.getAccountID(sfAccount);
        STAmount const amount = tx.getFieldAmount(sfAmount);
        AccountID const& holder = amount.getIssuer();
        STAmount const holderBalance = accountHolds(
            view, holder, amount.getCurrency(), issuer, fhIGNORE_FREEZE, j);

        if (holderBalance.signum() < 0)
        {
            JLOG(j.fatal())
                << "Invariant failed: trustline balance is negative";
            return false;
        }
    }
    else
    {
        if (trustlinesChanged != 0)
        {
            JLOG(j.fatal()) << "Invariant failed: some trustlines were changed "
                               "despite failure of the transaction.";
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------

ValidAMM::Pool::Pool(STAmount const& a) : amount{a}
{
}

bool
ValidAMM::Pool::hasAsset(Issue const& iss) const
{
    return amount.issue() == iss || amount2.issue() == iss;
}

bool
ValidAMM::Pool::hasBothAssets() const
{
    return amount.issue() != noIssue() && amount2.issue() != noIssue() &&
        amount.issue() != amount2.issue();
}

bool
ValidAMM::Pool::hasAssetOrBothAssets(Issue const& iss) const
{
    return hasAsset(iss) || hasBothAssets();
}

void
ValidAMM::Pool::addAmount2(STAmount const& a)
{
    if (a.issue() > amount.issue())
        amount2 = a;
    else
    {
        amount2 = amount;
        amount = a;
    }
}

bool
ValidAMM::Pool::consistent(Pool const& other) const
{
    if (!hasBothAssets() || !other.hasBothAssets())
        return false;
    return amount.issue() == other.amount.issue() &&
        amount2.issue() == other.amount2.issue();
}

bool
ValidAMM::Pool::positive(AccountID const& ammAccount, STAmount const& amount)
{
    if (!isXRP(amount) && ammAccount > amount.getIssuer())
        return amount.negative();
    return !amount.negative();
}

bool
ValidAMM::Pool::goodPool(AccountID const& ammAccount) const
{
    return hasBothAssets() && amount != beast::zero && amount2 != beast::zero &&
        positive(ammAccount, amount) && positive(ammAccount, amount2);
}

Number
ValidAMM::Pool::product(AccountID const& ammAccount) const
{
    Number amount_ = amount;
    Number amount2_ = amount2;
    if (!isXRP(amount) && ammAccount > amount.getIssuer())
        amount_ = -amount_;
    if (!isXRP(amount2) && ammAccount > amount2.getIssuer())
        amount2_ = -amount2_;
    return amount_ * amount2_;
}

void
ValidAMM::addPoolXRP(
    std::shared_ptr<SLE const> const& sle,
    hash_map<AccountID, Pool>& ammPool)
{
    isAMMPayment_ = true;
    auto const ammAccount = sle->getAccountID(sfAccount);
    ammAccounts_.insert(ammAccount);
    auto const& balance = sle->getFieldAmount(sfBalance);
    if (!ammPool.contains(ammAccount))
        ammPool.emplace(std::make_pair(ammAccount, Pool{balance}));
    else if (ammPool.at(ammAccount).hasAssetOrBothAssets(balance.issue()))
        error_ = true;
    else
        ammPool.at(ammAccount).addAmount2(balance);
}

void
ValidAMM::addPoolIOU(
    std::shared_ptr<SLE const> const& sle,
    hash_map<AccountID, Pool>& ammPool)
{
    isAMMPayment_ = true;
    auto balance = sle->getFieldAmount(sfBalance);
    auto const& lowLimit = sle->getFieldAmount(sfLowLimit);
    auto const& highLimit = sle->getFieldAmount(sfHighLimit);
    auto addBalanceToPool = [&](AccountID const& ammAccount,
                                AccountID const& issuer) {
        balance.setIssuer(issuer);
        if (!ammPool.contains(ammAccount))
            ammPool.emplace(std::make_pair(ammAccount, balance));
        else if (ammPool.at(ammAccount).hasAssetOrBothAssets(balance.issue()))
        {
            // This must be a non AMM account. The same AMM can not have
            // more than two assets whether the same or not. But there might be
            // multiple IOU for the same issuer.
            if (ammAccounts_.contains(ammAccount))
                error_ = true;
        }
        else
            ammPool.at(ammAccount).addAmount2(balance);
    };
    auto updateKnownAMMAccount = [&](AccountID const& ammAccount,
                                     AccountID const& issuer) {
        if (!error_)
        {
            ammAccounts_.insert(ammAccount);
            nonAMMAccounts_.insert(issuer);
            poolBefore_.erase(issuer);
            poolAfter_.erase(issuer);
        }
    };
    // Have to add for both accounts since it is not known which one is AMM.
    // There might be multiple AMM's in the payment with the same IOU
    if (ammAccounts_.contains(lowLimit.getIssuer()) ||
        nonAMMAccounts_.contains(highLimit.getIssuer()))
    {
        addBalanceToPool(lowLimit.getIssuer(), highLimit.getIssuer());
        updateKnownAMMAccount(lowLimit.getIssuer(), highLimit.getIssuer());
    }
    else if (
        ammAccounts_.contains(highLimit.getIssuer()) ||
        nonAMMAccounts_.contains(lowLimit.getIssuer()))
    {
        addBalanceToPool(highLimit.getIssuer(), lowLimit.getIssuer());
        updateKnownAMMAccount(highLimit.getIssuer(), lowLimit.getIssuer());
    }
    else
    {
        addBalanceToPool(lowLimit.getIssuer(), highLimit.getIssuer());
        addBalanceToPool(highLimit.getIssuer(), lowLimit.getIssuer());
    }
}

void
ValidAMM::visitEntry(
    bool isDeleted,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (error_)
        return;

    if (isDeleted && after && after->getType() == ltAMM)
    {
        auto const asset_ = (*before)[sfAsset];
        auto const asset2_ = (*before)[sfAsset2];
        auto const& [asset, asset2] = std::minmax(asset_, asset2_);
        deleted_.insert(std::make_pair(asset, asset2));
    }
    else if (before && after)
    {
        if (before->getFieldU16(sfLedgerEntryType) == ltACCOUNT_ROOT &&
            before->isFieldPresent(sfAMMID))
        {
            addPoolXRP(before, poolBefore_);
        }
        if (after->getFieldU16(sfLedgerEntryType) == ltACCOUNT_ROOT &&
            after->isFieldPresent(sfAMMID))
        {
            addPoolXRP(after, poolAfter_);
        }
        if (before->getFieldU16(sfLedgerEntryType) == ltRIPPLE_STATE &&
            (before->getFlags() & lsfAMMNode))
        {
            addPoolIOU(before, poolBefore_);
        }
        if (after->getFieldU16(sfLedgerEntryType) == ltRIPPLE_STATE &&
            (after->getFlags() & lsfAMMNode))
        {
            addPoolIOU(after, poolAfter_);
        }
    }
}

std::optional<std::tuple<STAmount, STAmount, STAmount>>
ValidAMM::getBalances(
    ReadView const& view,
    TxType txType,
    Issue const& asset,
    Issue const& asset2,
    beast::Journal j) const
{
    auto ammSle = view.read(keylet::amm(asset, asset2));
    if (!ammSle)
    {
        JLOG(j.error()) << "ValidAMM::getBalances, failed amm SLE " << asset
                        << " " << asset2;
        return std::nullopt;
    }
    auto const expected = ammHolds(
        view,
        *ammSle,
        std::nullopt,
        std::nullopt,
        FreezeHandling::fhIGNORE_FREEZE,
        j);
    if (!expected)
    {
        JLOG(j.error()) << "ValidAMM::getBalances ammHolds failed " << asset
                        << " " << asset2;
        return std::nullopt;
    }

    // All zero balances are valid if AMM is in an empty state after
    // Withdraw.
    std::uint8_t zeros = (std::get<0>(*expected) == beast::zero) +
        (std::get<1>(*expected) == beast::zero) +
        (std::get<2>(*expected) == beast::zero);
    if (((txType == ttAMM_CREATE || txType == ttAMM_DEPOSIT) && zeros) ||
        (txType == ttAMM_WITHDRAW && (zeros == 1 || zeros == 2)))
    {
        JLOG(j.error()) << "ValidAMM::getBalances invalid balances" << asset
                        << " " << asset2 << std::get<0>(*expected) << " "
                        << std::get<1>(*expected) << " "
                        << std::get<2>(*expected);
        return std::nullopt;
    }

    return *expected;
}

bool
ValidAMM::checkCreate(
    ReadView const& view,
    Issue const& asset,
    Issue const& asset2,
    beast::Journal j) const
{
    auto const res = getBalances(view, ttAMM_CREATE, asset, asset2, j);
    if (!res)
        return false;
    auto const& [amount, amount2, lptAMMBalance] = *res;
    auto const lpTokens = ammLPTokens(amount, amount2, lptAMMBalance.issue());
    if (lpTokens != lptAMMBalance || lpTokens == beast::zero)
    {
        JLOG(j.error()) << "ValidAMM::checkCreate failed: invariant " << amount
                        << " " << amount2 << " " << lptAMMBalance;
        return false;
    }

    auto checkNonAMMAsset = [&](STAmount const& a) {
        if (isXRP(a))
            return true;
        auto const sle = view.read(keylet::account(a.getIssuer()));
        if (!sle)
        {
            JLOG(j.error()) << "ValidAMM::checkCreate failed: get account "
                            << to_string(a.getIssuer()) << " " << amount << " "
                            << amount2 << " " << lptAMMBalance;
            return false;
        }
        if (sle->isFieldPresent(sfAMMID))
        {
            JLOG(j.error()) << "ValidAMM::checkCreate failed: invariant " << a
                            << " is AMM token " << amount << " " << amount2
                            << " " << lptAMMBalance;
            return false;
        }
        return true;
    };

    return checkNonAMMAsset(amount) && checkNonAMMAsset(amount2);
}

bool
ValidAMM::checkDepositWithdraw(
    ReadView const& view,
    TxType txType,
    Issue const& asset_,
    Issue const& asset2_,
    beast::Journal j) const
{
    auto const& [asset, asset2] = std::minmax(asset_, asset2_);
    if (deleted_.contains(std::make_pair(asset, asset2)))
        return true;

    auto const res = getBalances(view, txType, asset, asset2, j);
    if (!res)
        return false;
    auto const& [amount, amount2, lptAMMBalance] = *res;

    auto const lpTokens = ammLPTokens(amount, amount2, lptAMMBalance.issue());
    if (lpTokens < lptAMMBalance &&
        !withinRelativeDistance(lpTokens, lptAMMBalance, Number{1, -7}))
    {
        JLOG(j.error()) << "ValidAMM::checkDepositWithdraw failed: invariant "
                        << static_cast<int>(txType) << " " << amount << " "
                        << amount2 << " " << lpTokens << " " << lptAMMBalance
                        << " diff: " << (lptAMMBalance - lpTokens) / lpTokens;
        return false;
    }

    return true;
}

bool
ValidAMM::checkPayment(ReadView const& view, beast::Journal j)
{
    if (error_ || poolBefore_.size() != poolAfter_.size() ||
        poolBefore_.empty())
    {
        JLOG(j.error()) << "ValidAMM::checkPayment failed: "
                           "inconsistent before/after";
        return false;
    }
    if (!std::equal(
            poolBefore_.begin(),
            poolBefore_.end(),
            poolAfter_.begin(),
            [](auto const& it1, auto const& it2) {
                return it1.first == it2.first;
            }))
    {
        JLOG(j.error()) << "ValidAMM::checkPayment failed: "
                           "inconsistent keys before/after";
        return false;
    }

    auto invariantHolds = [&](AccountID const& ammAccount,
                              Pool const& before,
                              Pool const& after) {
        if (!before.consistent(after))
        {
            JLOG(j.error()) << "ValidAMM::checkPayment failed: "
                               "invalid before/after";
            return false;
        }
        if (!after.goodPool(ammAccount))
        {
            JLOG(j.error()) << "ValidAMM::checkPayment failed: bad pool "
                            << to_string(ammAccount) << " " << after.amount
                            << " " << after.amount2;
            return false;
        }
        Number const productBefore = before.product(ammAccount);
        Number const productAfter = after.product(ammAccount);
        if (productAfter < productBefore &&
            !withinRelativeDistance(productBefore, productAfter, Number{1, -7}))
        {
            JLOG(j.error())
                << "ValidAMM::checkPayment failed: invariant " << before.amount
                << " " << before.amount2 << after.amount << " " << after.amount2
                << (productBefore - productAfter) / productBefore;
            return false;
        }
        return true;
    };

    for (auto const& it : poolBefore_)
    {
        auto const& account = it.first;
        if (ammAccounts_.contains(account))
        {
            if (!invariantHolds(
                    account, poolBefore_.at(account), poolAfter_.at(account)))
                return false;
            ammAccounts_.erase(account);
            nonAMMAccounts_.erase(poolBefore_.at(account).amount.getIssuer());
            nonAMMAccounts_.erase(poolBefore_.at(account).amount2.getIssuer());
        }
        else
        {
            auto const sle = view.read(keylet::account(account));
            if (!sle)
            {
                JLOG(j.error()) << "ValidAMM::checkPayment failed: get account "
                                << to_string(account);
                return false;
            }
            if (sle->isFieldPresent(sfAMMID))
            {
                if (!invariantHolds(
                        account,
                        poolBefore_.at(account),
                        poolAfter_.at(account)))
                    return false;
                nonAMMAccounts_.erase(
                    poolBefore_.at(account).amount.getIssuer());
                nonAMMAccounts_.erase(
                    poolBefore_.at(account).amount2.getIssuer());
            }
        }
    }

    if (!ammAccounts_.empty() || !nonAMMAccounts_.empty())
    {
        JLOG(j.error())
            << "ValidAMM::checkPayment failed: inconsistent accounts";
        return false;
    }

    return true;
}

bool
ValidAMM::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!view.rules().enabled(fixAMMV1))
        return true;

    auto const txType = tx.getTxnType();

    if (result == tesSUCCESS)
    {
        if (txType == ttAMM_CREATE)
            return checkCreate(
                view, tx[sfAmount].issue(), tx[sfAmount2].issue(), j);
        else if (txType == ttAMM_DEPOSIT || txType == ttAMM_WITHDRAW)
            return checkDepositWithdraw(
                view, txType, tx[sfAsset], tx[sfAsset2], j);
        else if (txType == ttPAYMENT && isAMMPayment_)
            return checkPayment(view, j);
    }

    return true;
}

}  // namespace ripple
