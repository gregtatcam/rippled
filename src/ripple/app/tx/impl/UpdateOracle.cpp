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

#include <ripple/app/tx/impl/UpdateOracle.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

NotTEC
UpdateOracle::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "Oracle Update: invalid flags.";
        return temINVALID_FLAG;
    }

    if (ctx.tx.getFieldU8(sfScale) > 10)
        return temMALFORMED;

    return preflight2(ctx);
}

TER
UpdateOracle::preclaim(PreclaimContext const& ctx)
{
    auto const sle = ctx.view.read(keylet::oracle(ctx.tx[sfOracleID]));
    if (!sle)
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: Oracle does not exist.";
        return tecNO_ENTRY;
    }
    if (ctx.tx.getAccountID(sfAccount) != sle->getAccountID(sfOwner))
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: invalid account.";
        return tecNO_PERMISSION;
    }
    // Should check if last update time is valid
    return tesSUCCESS;
}

static std::pair<TER, bool>
applyUpdate(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    auto sle = sb.peek(keylet::oracle(ctx_.tx[sfOracleID]));
    if (!sle)
    {
        JLOG(j_.error()) << "Oracle Delete: Oracle does not exist.";
        return {tecINTERNAL, false};
    }

    sle->setFieldU64(sfSymbolPrice, ctx_.tx[sfSymbolPrice]);
    sle->setFieldU8(sfScale, ctx_.tx[sfScale]);
    sle->setFieldU32(sfLastUpdateTime, ctx_.tx[sfLastUpdateTime]);
    sb.update(sle);

    return {tesSUCCESS, true};
}

TER
UpdateOracle::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applyUpdate(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
