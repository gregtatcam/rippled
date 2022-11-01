//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#include <ripple/app/misc/AMM.h>
#include <ripple/app/paths/TrustLine.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STObject.h>

namespace ripple {

AccountID
ammAccountID(
    std::uint16_t prefix,
    uint256 const& parentHash,
    uint256 const& ammID)
{
    ripesha_hasher rsh;
    auto const hash = sha512Half(prefix, parentHash, ammID);
    rsh(hash.data(), hash.size());
    return AccountID{static_cast<ripesha_hasher::result_type>(rsh)};
}

Currency
ammLPTCurrency(Currency const& cur1, Currency const& cur2)
{
    std::int32_t constexpr AMMCurrencyCode = 0x03;
    auto const [min, max] = std::minmax(cur1, cur2);
    auto const hash = sha512Half(min, max);
    Currency currency;
    *currency.begin() = AMMCurrencyCode;
    std::copy(
        hash.begin(), hash.begin() + currency.size() - 1, currency.begin() + 1);
    return currency;
}

Issue
ammLPTIssue(
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccountID)
{
    return Issue(ammLPTCurrency(cur1, cur2), ammAccountID);
}

std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue1,
    Issue const& issue2,
    beast::Journal const j)
{
    auto const assetInBalance = accountHolds(
        view, ammAccountID, issue1, FreezeHandling::fhZERO_IF_FROZEN, j);
    auto const assetOutBalance = accountHolds(
        view, ammAccountID, issue2, FreezeHandling::fhZERO_IF_FROZEN, j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Issue> const& optIssue1,
    std::optional<Issue> const& optIssue2,
    beast::Journal const j)
{
    auto const issues = [&]() -> std::optional<std::pair<Issue, Issue>> {
        if (optIssue1 && optIssue2)
            return {{*optIssue1, *optIssue2}};
        auto const issue1 = ammSle[sfAsset];
        auto const issue2 = ammSle[sfAsset2];
        if (optIssue1)
        {
            if (*optIssue1 == issue1)
                return {{issue1, issue2}};
            else if (*optIssue1 == issue2)
                return {{issue2, issue1}};
            JLOG(j.debug()) << "ammHolds: Invalid optIssue1 " << *optIssue1;
            return std::nullopt;
        }
        else if (optIssue2)
        {
            if (*optIssue2 == issue2)
                return {{issue2, issue1}};
            else if (*optIssue2 == issue1)
                return {{issue1, issue2}};
            JLOG(j.debug()) << "ammHolds: Invalid optIssue2 " << *optIssue2;
            return std::nullopt;
        }
        return {{issue1, issue2}};
    }();
    if (!issues)
        return Unexpected(tecAMM_INVALID_TOKENS);
    auto const [asset1, asset2] = ammPoolHolds(
        view,
        ammSle.getAccountID(sfAMMAccount),
        issues->first,
        issues->second,
        j);
    return std::make_tuple(asset1, asset2, ammSle[sfLPTokenBalance]);
}

STAmount
ammLPHolds(
    ReadView const& view,
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    auto const lptIss = ammLPTIssue(cur1, cur2, ammAccount);
    return accountHolds(
        view,
        lpAccount,
        lptIss.currency,
        lptIss.account,
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
        ammSle[sfAsset].currency,
        ammSle[sfAsset2].currency,
        ammSle[sfAMMAccount],
        lpAccount,
        j);
}

NotTEC
invalidAMMAsset(
    Issue const& issue,
    std::optional<std::pair<Issue, Issue>> const& pair)
{
    if (badCurrency() == issue.currency)
        return temBAD_CURRENCY;
    if (isXRP(issue) && true != !issue.account)
        return temBAD_ISSUER;
    if (pair && issue != pair->first && issue != pair->second)
        return temBAD_AMM_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAssetPair(
    Issue const& issue1,
    Issue const& issue2,
    std::optional<std::pair<Issue, Issue>> const& pair)
{
    if (auto const res = invalidAMMAsset(issue1, pair))
        return res;
    if (auto const res = invalidAMMAsset(issue2, pair))
        return res;
    if (issue1 == issue2)
        return temBAD_AMM_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAmount(
    std::optional<STAmount> const& a,
    std::optional<std::pair<Issue, Issue>> const& pair,
    bool nonNegative)
{
    if (!a)
        return tesSUCCESS;
    if (auto const res = invalidAMMAsset(a->issue(), pair))
        return res;
    if (!nonNegative && *a <= beast::zero)
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

bool
isFrozen(ReadView const& view, std::optional<STAmount> const& a)
{
    return a && !a->native() && isGlobalFrozen(view, a->getIssuer());
}

TER
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account)
{
    if (isXRP(issue) || issue.account == account)
        return tesSUCCESS;
    if (auto const issuerAccount = view.read(keylet::account(issue.account));
        issuerAccount && (*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        if (auto const trustLine =
                view.read(keylet::line(account, issue.account, issue.currency));
            trustLine)
            return !((*trustLine)[sfFlags] &
                     ((account > issue.account) ? lsfLowAuth : lsfHighAuth))
                ? TER{tecNO_AUTH}
                : tesSUCCESS;
        return TER{tecNO_LINE};
    }

    return tesSUCCESS;
}

std::uint16_t
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account)
{
    using namespace std::chrono;
    if (ammSle.isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(ammSle.peekAtField(sfAuctionSlot));
        if (auto const expiration = auctionSlot[~sfExpiration])
        {
            auto const notExpired =
                duration_cast<seconds>(
                    view.info().parentCloseTime.time_since_epoch())
                    .count() < expiration;
            if (auctionSlot[~sfAccount] == account && notExpired)
                return auctionSlot[sfDiscountedFee];
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                for (auto const& acct :
                     auctionSlot.getFieldArray(sfAuthAccounts))
                    if (acct[~sfAccount] == account && notExpired)
                        return auctionSlot[sfDiscountedFee];
            }
        }
    }
    return ammSle[sfTradingFee];
}

TER
ammSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j)
{
    if (isXRP(amount))
        return accountSend(view, from, to, amount, j);

    auto const issuer = amount.getIssuer();

    if (from == issuer || to == issuer || issuer == noAccount())
        return rippleCredit(view, from, to, amount, false, j);

    TER terResult = rippleCredit(view, issuer, to, amount, true, j);

    if (tesSUCCESS == terResult)
        terResult = rippleCredit(view, from, issuer, amount, true, j);

    return terResult;
}

std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot)
{
    using namespace std::chrono;
    std::uint32_t constexpr totalSlotTimeSecs = 24 * 3600;
    std::uint32_t constexpr intervals = 20;
    std::uint32_t constexpr intervalDuration = totalSlotTimeSecs / intervals;
    if (auto const expiration = auctionSlot[~sfExpiration])
    {
        auto const diff = current - (*expiration - totalSlotTimeSecs);
        if (diff < totalSlotTimeSecs)
            return diff / intervalDuration;
    }
    return std::nullopt;
}

bool
ammEnabled(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber);
}

STAmount
ammAccountHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    const Issue& issue)
{
    if (isXRP(issue))
    {
        if (auto const sle = view.read(keylet::account(ammAccountID)))
            return (*sle)[sfBalance];
    }
    else if (auto const sle = view.read(
                 keylet::line(ammAccountID, issue.account, issue.currency));
             sle &&
             !isFrozen(view, ammAccountID, issue.currency, issue.account))
    {
        auto amount = (*sle)[sfBalance];
        if (ammAccountID > issue.account)
            amount.negate();
        amount.setIssuer(issue.account);
        return amount;
    }

    return STAmount{issue};
}

Expected<std::shared_ptr<SLE const>, TER>
getAMMSle(ReadView const& view, Issue const& issue1, Issue const& issue2)
{
    if (auto const ammSle = view.read(keylet::amm(issue1, issue2)))
        return ammSle;
    else
        return Unexpected(tecINTERNAL);
}

Expected<std::shared_ptr<SLE>, TER>
getAMMSle(Sandbox& sb, Issue const& issue1, Issue const& issue2)
{
    if (auto ammSle = sb.peek(keylet::amm(issue1, issue2)))
        return ammSle;
    else
        return Unexpected(tecINTERNAL);
}

}  // namespace ripple
