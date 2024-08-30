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

#ifndef RIPPLE_TX_CREATEOFFER_H_INCLUDED
#define RIPPLE_TX_CREATEOFFER_H_INCLUDED

#include <xrpld/app/tx/detail/OfferStream.h>
#include <xrpld/app/tx/detail/Transactor.h>
#include <utility>

namespace ripple {

class PaymentSandbox;
class Sandbox;

/** Transactor specialized for creating offers in the ledger. */
class CreateOffer : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    /** Construct a Transactor subclass that creates an offer in the ledger. */
    explicit CreateOffer(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Precondition: fee collection is likely.  Attempt to create the offer. */
    TER
    doApply() override;
};

template <ValidSerialAmountType TPays, ValidSerialAmountType TGets>
class CreateOfferHelper
{
public:
    CreateOfferHelper(
        ApplyContext& ctx,
        AccountID const& account,
        XRPAmount const& priorBalance);

    std::pair<TER, bool>
    applyGuts(Sandbox& sb, Sandbox& sbCancel);

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

private:
    // Determine if we are authorized to hold the asset we want to get.
    template <ValidIssueType Iss>
    static TER
    checkAcceptAsset(
        ReadView const& view,
        ApplyFlags const flags,
        AccountID const id,
        beast::Journal const j,
        Iss const& issue);

    // Use the payment flow code to perform offer crossing.
    std::pair<TER, TAmounts<TGets, TPays>>
    flowCross(
        PaymentSandbox& psb,
        PaymentSandbox& psbCancel,
        TAmounts<TGets, TPays> const& takerAmount);

    template <ValidSerialAmountType T>
    static std::string
    format_amount(T const& amount);

private:
    ApplyContext& ctx_;
    AccountID const account_;
    XRPAmount const& priorBalance_;
    beast::Journal j_;
};

}  // namespace ripple

#endif
