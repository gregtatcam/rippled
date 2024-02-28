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

#include <ripple/app/tx/impl/Clawback.h>
#include <ripple/basics/MPTAmount.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
Clawback::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureClawback))
        return temDISABLED;

    auto const mptHolder = ctx.tx[~sfMPTokenHolder];
    STAmount const clawAmount = ctx.tx[sfAmount];
    if ((mptHolder || clawAmount.isMPT()) &&
        !ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (!mptHolder && clawAmount.isMPT())
        return temMALFORMED;

    if (mptHolder && !clawAmount.isMPT())
        return temMALFORMED;

    if (ctx.tx.getFlags() & tfClawbackMask)
        return temINVALID_FLAG;

    AccountID const issuer = ctx.tx[sfAccount];

    // The issuer field is used for the token holder if asset is IOU
    AccountID const& holder =
        clawAmount.isMPT() ? *mptHolder : clawAmount.getIssuer();

    if (clawAmount.isMPT())
    {
        if (issuer == holder)
            return temMALFORMED;

        if (clawAmount.mpt() > MPTAmount{maxMPTokenAmount} ||
            clawAmount <= beast::zero)
            return temBAD_AMOUNT;
    }
    else
    {
        if (issuer == holder || isXRP(clawAmount) || clawAmount <= beast::zero)
            return temBAD_AMOUNT;
    }

    return preflight2(ctx);
}

TER
Clawback::preclaim(PreclaimContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    STAmount const clawAmount = ctx.tx[sfAmount];
    AccountID const& holder =
        clawAmount.isMPT() ? ctx.tx[sfMPTokenHolder] : clawAmount.getIssuer();

    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    auto const sleHolder = ctx.view.read(keylet::account(holder));
    if (!sleIssuer || !sleHolder)
        return terNO_ACCOUNT;

    if (sleHolder->isFieldPresent(sfAMMID))
        return tecAMM_ACCOUNT;

    if (clawAmount.isMPT())
    {
        auto const issuanceKey =
            keylet::mptIssuance(clawAmount.mptIssue().mpt());
        auto const sleIssuance = ctx.view.read(issuanceKey);
        if (!sleIssuance)
            return tecOBJECT_NOT_FOUND;

        if (!((*sleIssuance)[sfFlags] & lsfMPTCanClawback))
            return tecNO_PERMISSION;

        if (sleIssuance->getAccountID(sfIssuer) != issuer)
            return tecNO_PERMISSION;

        if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, holder)))
            return tecOBJECT_NOT_FOUND;

        if (accountHolds(
                ctx.view,
                holder,
                clawAmount.mptIssue(),
                fhIGNORE_FREEZE,
                ahIGNORE_AUTH,
                ctx.j) <= beast::zero)
            return tecINSUFFICIENT_FUNDS;
    }
    else
    {
        std::uint32_t const issuerFlagsIn = sleIssuer->getFieldU32(sfFlags);

        // If AllowTrustLineClawback is not set or NoFreeze is set, return no
        // permission
        if (!(issuerFlagsIn & lsfAllowTrustLineClawback) ||
            (issuerFlagsIn & lsfNoFreeze))
            return tecNO_PERMISSION;

        auto const sleRippleState = ctx.view.read(
            keylet::line(holder, issuer, clawAmount.getCurrency()));
        if (!sleRippleState)
            return tecNO_LINE;

        STAmount const& balance = (*sleRippleState)[sfBalance];

        // If balance is positive, issuer must have higher address than holder
        if (balance > beast::zero && issuer < holder)
            return tecNO_PERMISSION;

        // If balance is negative, issuer must have lower address than holder
        if (balance < beast::zero && issuer > holder)
            return tecNO_PERMISSION;

        // At this point, we know that issuer and holder accounts
        // are correct and a trustline exists between them.
        //
        // Must now explicitly check the balance to make sure
        // available balance is non-zero.
        //
        // We can't directly check the balance of trustline because
        // the available balance of a trustline is prone to new changes (eg.
        // XLS-34). So we must use `accountHolds`.
        if (accountHolds(
                ctx.view,
                holder,
                clawAmount.getCurrency(),
                issuer,
                fhIGNORE_FREEZE,
                ctx.j) <= beast::zero)
            return tecINSUFFICIENT_FUNDS;
    }

    return tesSUCCESS;
}

TER
Clawback::doApply()
{
    AccountID const& issuer = account_;
    STAmount clawAmount = ctx_.tx[sfAmount];
    AccountID const holder = clawAmount.isMPT()
        ? ctx_.tx[sfMPTokenHolder]
        : clawAmount.getIssuer();  // cannot be reference because clawAmount is
                                   // modified beblow

    if (clawAmount.isMPT())
    {
        // Get the spendable balance. Must use `accountHolds`.
        STAmount const spendableAmount = accountHolds(
            view(),
            holder,
            clawAmount.mptIssue(),
            fhIGNORE_FREEZE,
            ahIGNORE_AUTH,
            j_);

        return rippleMPTCredit(
            view(), holder, issuer, std::min(spendableAmount, clawAmount), j_);
    }

    // Replace the `issuer` field with issuer's account if asset is IOU
    clawAmount.setIssuer(issuer);
    if (holder == issuer)
        return tecINTERNAL;

    // Get the spendable balance. Must use `accountHolds`.
    STAmount const spendableAmount = accountHolds(
        view(),
        holder,
        clawAmount.getCurrency(),
        clawAmount.getIssuer(),
        fhIGNORE_FREEZE,
        j_);

    return rippleCredit(
        view(),
        holder,
        issuer,
        std::min(spendableAmount, clawAmount),
        true,
        j_);
}

}  // namespace ripple
