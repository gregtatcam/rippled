//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/paths/detail/AmountSpec.h>
#include <xrpld/ledger/PaymentSandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>

namespace ripple {

namespace detail {

auto
DeferredCredits::makeIOUKey(
    AccountID const& a1,
    AccountID const& a2,
    Issue const& issue) -> Key
{
    if (a1 < a2)
        return std::make_tuple(a1, a2, issue.currency);
    else
        return std::make_tuple(a2, a1, issue.currency);
}

auto
DeferredCredits::makeMPTKey(
    std::optional<AccountID> const& account,
    MPTIssue const& issue) -> Key
{
    auto const& issuer = issue.getIssuer();
    if (account)
    {
        if (*account == issuer)
            Throw<std::runtime_error>("makeMPTKey: invalid holder");
        return std::make_tuple(issuer, *account, issue.getMptID());
    }
    else
        return std::make_tuple(issuer, issuer, issue.getMptID());
}

void
DeferredCredits::creditIOU(
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    STAmount const& preCreditSenderBalance)
{
    XRPL_ASSERT(
        sender != receiver,
        "ripple::detail::DeferredCredits::creditIOU : sender is not receiver");
    XRPL_ASSERT(
        !amount.negative(),
        "ripple::detail::DeferredCredits::creditIOU : positive amount");

    auto const k = makeIOUKey(sender, receiver, amount.asset().get<Issue>());
    auto i = credits_.find(k);
    if (i == credits_.end())
    {
        IssueValue v;
        if (sender < receiver)
        {
            v.highAcctCredits = amount;
            v.lowAcctCredits = amount.zeroed();
            v.lowAcctOrigBalance = preCreditSenderBalance;
        }
        else
        {
            v.highAcctCredits = amount.zeroed();
            v.lowAcctCredits = amount;
            v.lowAcctOrigBalance = -preCreditSenderBalance;
        }
        credits_[k] = v;
    }
    else
    {
        // only record the balance the first time, do not record it here
        auto& v = std::get<IssueValue>(i->second);
        if (sender < receiver)
            v.highAcctCredits += amount;
        else
            v.lowAcctCredits += amount;
    }
}

void
DeferredCredits::creditMPT(
    std::optional<AccountID> const& account,
    STAmount const& amount,
    STAmount const& originalBalance)
{
    auto const& asset = amount.asset();
    if (!asset.holds<MPTIssue>())
        Throw<std::runtime_error>("creditMPT: non MPTIssue");
    auto const& issue = asset.get<MPTIssue>();
    auto const k = makeMPTKey(account, issue);
    auto it = credits_.find(k);
    if (it == credits_.end())
    {
        MPTIssueValue v{issue};
        if (account)
            v.creditsHolder = amount;
        else
            v.creditsIssuer = amount;
        v.originalBalance = originalBalance;
        credits_[k] = v;
    }
    else
    {
        if (!std::holds_alternative<MPTIssueValue>(it->second))
            Throw<std::runtime_error>("creditMPT: credit is not MPTIssueValue");
        auto& v = std::get<MPTIssueValue>(it->second);
        if (account)
            v.creditsHolder += amount;
        else
            v.creditsIssuer += amount;
    }
}

void
DeferredCredits::ownerCount(
    AccountID const& id,
    std::uint32_t cur,
    std::uint32_t next)
{
    auto const v = std::max(cur, next);
    auto r = ownerCounts_.emplace(std::make_pair(id, v));
    if (!r.second)
    {
        auto& mapVal = r.first->second;
        mapVal = std::max(v, mapVal);
    }
}

std::optional<std::uint32_t>
DeferredCredits::ownerCount(AccountID const& id) const
{
    auto i = ownerCounts_.find(id);
    if (i != ownerCounts_.end())
        return i->second;
    return std::nullopt;
}

// Get the adjustments for the balance between main and other.
auto
DeferredCredits::adjustmentsIOU(
    AccountID const& main,
    AccountID const& other,
    Issue const& issue) const -> std::optional<Adjustment>
{
    std::optional<Adjustment> result;

    Key const k = makeIOUKey(main, other, issue);
    auto i = credits_.find(k);
    if (i == credits_.end())
        return result;

    auto& v = std::get<IssueValue>(i->second);
    if (main < other)
        result.emplace(
            v.highAcctCredits, v.lowAcctCredits, v.lowAcctOrigBalance);
    else
        result.emplace(
            v.lowAcctCredits, v.highAcctCredits, -v.lowAcctOrigBalance);

    return result;
}

auto
DeferredCredits::adjustmentsMPT(
    std::optional<AccountID> const& account,
    MPTIssue const& issue) const -> std::optional<Adjustment>
{
    std::optional<Adjustment> result;

    Key const k = makeMPTKey(account, issue);
    auto i = credits_.find(k);
    if (i == credits_.end())
        return result;

    auto& v = std::get<MPTIssueValue>(i->second);

    if (account)
        result.emplace(v.creditsIssuer, v.creditsHolder, v.originalBalance);
    else
        result.emplace(v.creditsHolder, v.creditsIssuer, v.originalBalance);

    return result;
}

void
DeferredCredits::apply(DeferredCredits& to)
{
    for (auto const& i : credits_)
    {
        auto r = to.credits_.emplace(i);
        if (!r.second)
        {
            std::visit(
                [&]<typename TV>(TV& fromVal) {
                    if constexpr (std::is_same_v<TV, IssueValue>)
                    {
                        auto& toVal = std::get<IssueValue>(r.first->second);
                        toVal.lowAcctCredits += fromVal.lowAcctCredits;
                        toVal.highAcctCredits += fromVal.highAcctCredits;
                    }
                    else if constexpr (std::is_same_v<TV, MPTIssue>)
                    {
                        auto& toVal = std::get<MPTIssueValue>(r.first->second);
                        toVal.creditsHolder += fromVal.creditsHolder;
                        toVal.creditsIssuer += fromVal.creditsIssuer;
                    }
                    // Do not update the orig balance, it's already correct
                },
                i.second);
        }
    }

    for (auto const& i : ownerCounts_)
    {
        auto r = to.ownerCounts_.emplace(i);
        if (!r.second)
        {
            auto& toVal = r.first->second;
            auto const& fromVal = i.second;
            toVal = std::max(toVal, fromVal);
        }
    }
}

}  // namespace detail

STAmount
PaymentSandbox::balanceHookIOU(
    AccountID const& account,
    AccountID const& issuer,
    STAmount const& amount) const
{
    /*
    There are two algorithms here. The pre-switchover algorithm takes the
    current amount and subtracts the recorded credits. The post-switchover
    algorithm remembers the original balance, and subtracts the debits. The
    post-switchover algorithm should be more numerically stable. Consider a
    large creditIOU with a small initial balance. The pre-switchover algorithm
    computes (B+C)-C (where B+C will the amount passed in). The
    post-switchover algorithm returns B. When B and C differ by large
    magnitudes, (B+C)-C may not equal B.
    */

    auto const asset = amount.asset();

    auto delta = amount.zeroed();
    auto lastBal = amount;
    auto minBal = amount;
    for (auto curSB = this; curSB; curSB = curSB->ps_)
    {
        if (auto adj =
                curSB->tab_.adjustmentsIOU(account, issuer, asset.get<Issue>()))
        {
            delta += adj->debits;
            lastBal = adj->origBalance;
            if (lastBal < minBal)
                minBal = lastBal;
        }
    }

    // The adjusted amount should never be larger than the balance. In
    // some circumstances, it is possible for the deferred credits table
    // to compute usable balance just slightly above what the ledger
    // calculates (but always less than the actual balance).
    auto adjustedAmt = std::min({amount, lastBal - delta, minBal});
    if (amount.holds<Issue>())
        adjustedAmt.setIssuer(amount.getIssuer());

    if (isXRP(issuer) && adjustedAmt < beast::zero)
        // A calculated negative XRP balance is not an error case. Consider a
        // payment snippet that credits a large XRP amount and then debits the
        // same amount. The credit can't be used but we subtract the debit and
        // calculate a negative value. It's not an error case.
        adjustedAmt.clear();

    return adjustedAmt;
}

STAmount
PaymentSandbox::balanceHookMPT(
    std::optional<AccountID> const& account,
    STAmount const& amount) const
{
    auto const asset = amount.asset();

    auto delta = amount.zeroed();
    auto lastBal = amount;
    auto minBal = amount;
    for (auto curSB = this; curSB; curSB = curSB->ps_)
    {
        if (auto adj =
                curSB->tab_.adjustmentsMPT(account, asset.get<MPTIssue>()))
        {
            delta += adj->debits;
            lastBal = adj->origBalance;
            if (lastBal < minBal)
                minBal = lastBal;
        }
    }

    // The adjusted amount should never be larger than the balance. In
    // some circumstances, it is possible for the deferred credits table
    // to compute usable balance just slightly above what the ledger
    // calculates (but always less than the actual balance).
    auto adjustedAmt = std::min({amount, lastBal - delta, minBal});

    return adjustedAmt;
}

std::uint32_t
PaymentSandbox::ownerCountHook(AccountID const& account, std::uint32_t count)
    const
{
    std::uint32_t result = count;
    for (auto curSB = this; curSB; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.ownerCount(account))
            result = std::max(result, *adj);
    }
    return result;
}

void
PaymentSandbox::creditHookIOU(
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    STAmount const& preCreditBalance)
{
    tab_.creditIOU(from, to, amount, preCreditBalance);
}

void
PaymentSandbox::creditHookMPT(
    std::optional<AccountID> const& account,
    STAmount const& amount,
    STAmount const& preCreditBalance)
{
    tab_.creditMPT(account, amount, preCreditBalance);
}

void
PaymentSandbox::adjustOwnerCountHook(
    AccountID const& account,
    std::uint32_t cur,
    std::uint32_t next)
{
    tab_.ownerCount(account, cur, next);
}

void
PaymentSandbox::apply(RawView& to)
{
    XRPL_ASSERT(!ps_, "ripple::PaymentSandbox::apply : non-null sandbox");
    items_.apply(to);
}

void
PaymentSandbox::apply(PaymentSandbox& to)
{
    XRPL_ASSERT(ps_ == &to, "ripple::PaymentSandbox::apply : matching sandbox");
    items_.apply(to);
    tab_.apply(to.tab_);
}

std::map<std::tuple<AccountID, AccountID, Currency>, STAmount>
PaymentSandbox::balanceChanges(ReadView const& view) const
{
    using key_t = std::tuple<AccountID, AccountID, Currency>;
    // Map of delta trust lines. As a special case, when both ends of the trust
    // line are the same currency, then it's delta currency for that issuer. To
    // get the change in XRP balance, Account == root, issuer == root, currency
    // == XRP
    std::map<key_t, STAmount> result;

    // populate a dictionary with low/high/currency/delta. This can be
    // compared with the other versions payment code.
    auto each = [&result](
                    uint256 const& key,
                    bool isDelete,
                    std::shared_ptr<SLE const> const& before,
                    std::shared_ptr<SLE const> const& after) {
        STAmount oldBalance;
        STAmount newBalance;
        AccountID lowID;
        AccountID highID;

        // before is read from prev view
        if (isDelete)
        {
            if (!before)
                return;

            auto const bt = before->getType();
            switch (bt)
            {
                case ltACCOUNT_ROOT:
                    lowID = xrpAccount();
                    highID = (*before)[sfAccount];
                    oldBalance = (*before)[sfBalance];
                    newBalance = oldBalance.zeroed();
                    break;
                case ltRIPPLE_STATE:
                    lowID = (*before)[sfLowLimit].getIssuer();
                    highID = (*before)[sfHighLimit].getIssuer();
                    oldBalance = (*before)[sfBalance];
                    newBalance = oldBalance.zeroed();
                    break;
                case ltOFFER:
                    // TBD
                    break;
                default:
                    break;
            }
        }
        else if (!before)
        {
            // insert
            auto const at = after->getType();
            switch (at)
            {
                case ltACCOUNT_ROOT:
                    lowID = xrpAccount();
                    highID = (*after)[sfAccount];
                    newBalance = (*after)[sfBalance];
                    oldBalance = newBalance.zeroed();
                    break;
                case ltRIPPLE_STATE:
                    lowID = (*after)[sfLowLimit].getIssuer();
                    highID = (*after)[sfHighLimit].getIssuer();
                    newBalance = (*after)[sfBalance];
                    oldBalance = newBalance.zeroed();
                    break;
                case ltOFFER:
                    // TBD
                    break;
                default:
                    break;
            }
        }
        else
        {
            // modify
            auto const at = after->getType();
            XRPL_ASSERT(
                at == before->getType(),
                "ripple::PaymentSandbox::balanceChanges : after and before "
                "types matching");
            switch (at)
            {
                case ltACCOUNT_ROOT:
                    lowID = xrpAccount();
                    highID = (*after)[sfAccount];
                    oldBalance = (*before)[sfBalance];
                    newBalance = (*after)[sfBalance];
                    break;
                case ltRIPPLE_STATE:
                    lowID = (*after)[sfLowLimit].getIssuer();
                    highID = (*after)[sfHighLimit].getIssuer();
                    oldBalance = (*before)[sfBalance];
                    newBalance = (*after)[sfBalance];
                    break;
                case ltOFFER:
                    // TBD
                    break;
                default:
                    break;
            }
        }
        // The following are now set, put them in the map
        auto delta = newBalance - oldBalance;
        auto const cur = newBalance.get<Issue>().currency;
        result[std::make_tuple(lowID, highID, cur)] = delta;
        auto r = result.emplace(std::make_tuple(lowID, lowID, cur), delta);
        if (r.second)
        {
            r.first->second += delta;
        }

        delta.negate();
        r = result.emplace(std::make_tuple(highID, highID, cur), delta);
        if (r.second)
        {
            r.first->second += delta;
        }
    };
    items_.visit(view, each);
    return result;
}

XRPAmount
PaymentSandbox::xrpDestroyed() const
{
    return items_.dropsDestroyed();
}

}  // namespace ripple
