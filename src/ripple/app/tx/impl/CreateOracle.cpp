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

#include <ripple/app/tx/impl/CreateOracle.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>

namespace ripple {

NotTEC
CreateOracle::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfOracleMask)
    {
        JLOG(ctx.j.debug()) << "Oracle Instance: invalid flags.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
CreateOracle::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.read(keylet::oracle(
            ctx.tx.getAccountID(sfAccount),
            ctx.tx.getFieldVL(sfSymbol).data(),
            ctx.tx.getFieldVL(sfPriceUnit).data())))
    {
        JLOG(ctx.j.debug()) << "Oracle Instance: Oracle already exists.";
        return tecDUPLICATE;
    }
    return tesSUCCESS;
}

static std::pair<TER, bool>
applyCreate(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    auto const sleCreator = sb.read(keylet::account(account_));
    if (!sleCreator)
        return {tefINTERNAL, false};
    if (auto const reserve =
            sb.fees().accountReserve(sleCreator->getFieldU32(sfOwnerCount) + 1);
        sleCreator->getFieldAmount(sfBalance) < reserve)
    {
        JLOG(j_.debug()) << "Oracle Instance: insufficient reserve";
        return {tecINSUFFICIENT_RESERVE, false};
    }

    auto sle = std::make_shared<SLE>(keylet::oracle(
        account_,
        ctx_.tx.getFieldVL(sfSymbol).data(),
        ctx_.tx.getFieldVL(sfPriceUnit).data()));
    sle->setAccountID(sfOwner, ctx_.tx[sfAccount]);
    sle->setFieldVL(sfSymbol, ctx_.tx[sfSymbol]);
    sle->setFieldVL(sfPriceUnit, ctx_.tx[sfPriceUnit]);
    sle->setFieldVL(sfSymbolClass, ctx_.tx[sfSymbolClass]);
    sb.insert(sle);

    adjustOwnerCount(sb, sb.peek(keylet::account(account_)), 1, j_);

    return {tesSUCCESS, true};
}

TER
CreateOracle::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applyCreate(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
