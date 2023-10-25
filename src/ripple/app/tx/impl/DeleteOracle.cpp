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

#include <ripple/app/tx/impl/DeleteOracle.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rules.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

NotTEC
DeleteOracle::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featurePriceOracle))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: invalid flags.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
DeleteOracle::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.exists(keylet::account(ctx.tx.getAccountID(sfAccount))))
        return terNO_ACCOUNT;

    if (auto const sle = ctx.view.read(keylet::oracle(
            ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleSequence]));
        !sle)
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: Oracle does not exist.";
        return tecNO_ENTRY;
    }
    else if (ctx.tx.getAccountID(sfAccount) != sle->getAccountID(sfOwner))
    {
        // this can't happen because of the above check
        JLOG(ctx.j.debug()) << "Oracle Delete: invalid account.";
        return tecINTERNAL;
    }
    return tesSUCCESS;
}

TER
DeleteOracle::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    if (auto sle = sb.peek(keylet::oracle(account_, ctx_.tx[sfOracleSequence]));
        !sle)
        return tecINTERNAL;
    else
    {
        if (!sb.dirRemove(
                keylet::ownerDir(account_),
                (*sle)[sfOwnerNode],
                sle->key(),
                true))
        {
            JLOG(j_.fatal()) << "Unable to delete Oracle from owner.";
            return tefBAD_LEDGER;
        }

        auto const sleOwner = sb.peek(keylet::account(account_));
        if (!sleOwner)
            return tecINTERNAL;

        adjustOwnerCount(sb, sleOwner, -1, j_);
        sb.update(sleOwner);

        sb.erase(sle);
        sb.apply(ctx_.rawView());
    }

    return tesSUCCESS;
}

}  // namespace ripple
