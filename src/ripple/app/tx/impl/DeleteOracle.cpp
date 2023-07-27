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
#include <ripple/protocol/TxFlags.h>

namespace ripple {

NotTEC
DeleteOracle::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid flags.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
DeleteOracle::preclaim(PreclaimContext const& ctx)
{
    if (auto const sle = ctx.view.read(keylet::oracle(ctx.tx[sfOracleID]));
        !sle)
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: Oracle does not exist.";
        return tecNO_ENTRY;
    }
    else if (ctx.tx.getAccountID(sfAccount) != sle->getAccountID(sfOwner))
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: invalid account.";
        return tecNO_PERMISSION;
    }
    return tesSUCCESS;
}

TER
DeleteOracle::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    if (auto sle = sb.peek(keylet::oracle(ctx_.tx[sfOracleID])); !sle)
        return tecINTERNAL;
    else
    {
        sb.erase(sle);
        sb.apply(ctx_.rawView());
    }

    adjustOwnerCount(sb, sb.peek(keylet::account(account_)), -1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
