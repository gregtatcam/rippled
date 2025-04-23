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

#ifndef RIPPLE_LEDGER_PAYMENTSANDBOX_H_INCLUDED
#define RIPPLE_LEDGER_PAYMENTSANDBOX_H_INCLUDED

#include <xrpld/ledger/RawView.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/AccountID.h>
#include <map>
#include <utility>

namespace ripple {

namespace detail {

// VFALCO TODO Inline this implementation
//        into the PaymentSandbox class itself
class DeferredCredits
{
public:
    struct Adjustment
    {
        Adjustment(STAmount const& d, STAmount const& c, STAmount const& b)
            : debits(d), credits(c), origBalance(b)
        {
        }
        STAmount debits;
        STAmount credits;
        STAmount origBalance;
    };

    // Get the adjustmentsIOU for the balance between main and other.
    // Returns the debits, credits and the original balance
    std::optional<Adjustment>
    adjustmentsIOU(
        AccountID const& main,
        AccountID const& other,
        Issue const& issue) const;

    std::optional<Adjustment>
    adjustmentsMPT(
        std::optional<AccountID> const& account,
        MPTIssue const& issue) const;

    void
    creditIOU(
        AccountID const& sender,
        AccountID const& receiver,
        STAmount const& amount,
        STAmount const& preCreditSenderBalance);

    /** MPT doesn't have bidirectional relationship between an issuer
     * and holder and it doesn't have balances based on high/low accounts.
     * MPT has one global limit of MaximumAmount. Available amount that
     * can be issued is determined as MaximumAmount - OutstandingAmount.
     */
    /** Credit to the holder
     * @receiver holder's account
     * @amount of credit
     * @preCreditBalance balance before credit
     */
    void
    creditMPT(
        std::optional<AccountID> const& account,
        STAmount const& amount,
        STAmount const& preCreditBalance);

    void
    ownerCount(AccountID const& id, std::uint32_t cur, std::uint32_t next);

    // Get the adjusted owner count. Since DeferredCredits is meant to be used
    // in payments, and payments only decrease owner counts, return the max
    // remembered owner count.
    std::optional<std::uint32_t>
    ownerCount(AccountID const& id) const;

    void
    apply(DeferredCredits& to);

private:
    // if IOU then lowAccount, highAccount
    // if MPT then lowAccount is holder, highAccount is issuer
    using Key = std::tuple<AccountID, AccountID, Asset::token_type>;
    struct IssueValue
    {
        explicit IssueValue() = default;

        STAmount lowAcctCredits;
        STAmount highAcctCredits;
        STAmount lowAcctOrigBalance;
    };
    struct MPTIssueValue
    {
        explicit MPTIssueValue() = default;
        MPTIssueValue(MPTIssue const& issue)
            : creditsHolder{STAmount{issue}}
            , creditsIssuer{STAmount{issue}}
            , originalBalance{STAmount{issue}}
        {
        }

        // credit to holder
        STAmount creditsHolder;
        // debit from holder
        STAmount creditsIssuer;
        // issuer available balance on creditIOU
        // holder available balance on debit
        STAmount originalBalance;
    };
    using Value = std::variant<IssueValue, MPTIssueValue>;

    static Key
    makeIOUKey(AccountID const& a1, AccountID const& a2, Issue const& issue);

    /** If account is seated then it's a debit, otherwise it's a credit
     */
    static Key
    makeMPTKey(std::optional<AccountID> const& account, MPTIssue const& issue);

    std::map<Key, Value> credits_;
    std::map<AccountID, std::uint32_t> ownerCounts_;
};

}  // namespace detail

//------------------------------------------------------------------------------

/** A wrapper which makes credits unavailable to balances.

    This is used for payments and pathfinding, so that consuming
    liquidity from a path never causes portions of that path or
    other paths to gain liquidity.

    The behavior of certain free functions in the ApplyView API
    will change via the balanceHookIOU/MPT and creditHookIOU/MPT overrides
    of PaymentSandbox.

    @note Presented as ApplyView to clients
*/
class PaymentSandbox final : public detail::ApplyViewBase
{
public:
    PaymentSandbox() = delete;
    PaymentSandbox(PaymentSandbox const&) = delete;
    PaymentSandbox&
    operator=(PaymentSandbox&&) = delete;
    PaymentSandbox&
    operator=(PaymentSandbox const&) = delete;

    PaymentSandbox(PaymentSandbox&&) = default;

    PaymentSandbox(ReadView const* base, ApplyFlags flags)
        : ApplyViewBase(base, flags)
    {
    }

    PaymentSandbox(ApplyView const* base) : ApplyViewBase(base, base->flags())
    {
    }

    /** Construct on top of existing PaymentSandbox.

        The changes are pushed to the parent when
        apply() is called.

        @param parent A non-null pointer to the parent.

        @note A pointer is used to prevent confusion
              with copy construction.
    */
    // VFALCO If we are constructing on top of a PaymentSandbox,
    //        or a PaymentSandbox-derived class, we MUST go through
    //        one of these constructors or invariants will be broken.
    /** @{ */
    explicit PaymentSandbox(PaymentSandbox const* base)
        : ApplyViewBase(base, base->flags()), ps_(base)
    {
    }

    explicit PaymentSandbox(PaymentSandbox* base)
        : ApplyViewBase(base, base->flags()), ps_(base)
    {
    }
    /** @} */

    STAmount
    balanceHookIOU(
        AccountID const& account,
        AccountID const& issuer,
        STAmount const& amount) const override;

    STAmount
    balanceHookMPT(
        std::optional<AccountID> const& account,
        STAmount const& amount) const override;

    void
    creditHookIOU(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        STAmount const& preCreditBalance) override;

    void
    creditHookMPT(
        std::optional<AccountID> const& account,
        STAmount const& amount,
        STAmount const& preCreditBalance) override;

    void
    adjustOwnerCountHook(
        AccountID const& account,
        std::uint32_t cur,
        std::uint32_t next) override;

    std::uint32_t
    ownerCountHook(AccountID const& account, std::uint32_t count)
        const override;

    /** Apply changes to base view.

        `to` must contain contents identical to the parent
        view passed upon construction, else undefined
        behavior will result.
    */
    /** @{ */
    void
    apply(RawView& to);

    void
    apply(PaymentSandbox& to);
    /** @} */

    // Return a map of balance changes on trust lines. The low account is the
    // first account in the key. If the two accounts are equal, the map contains
    // the total changes in currency regardless of issuer. This is useful to get
    // the total change in XRP balances.
    std::map<std::tuple<AccountID, AccountID, Currency>, STAmount>
    balanceChanges(ReadView const& view) const;

    XRPAmount
    xrpDestroyed() const;

private:
    detail::DeferredCredits tab_;
    PaymentSandbox const* ps_ = nullptr;
};

}  // namespace ripple

#endif
