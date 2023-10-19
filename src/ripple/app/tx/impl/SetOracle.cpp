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

#include <ripple/app/tx/impl/SetOracle.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rules.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>

namespace ripple {

NotTEC
SetOracle::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featurePriceOracle))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfOracleMask)
    {
        JLOG(ctx.j.debug()) << "Oracle Set: invalid flags.";
        return temINVALID_FLAG;
    }

    if (ctx.tx.getFieldArray(sfPriceDataSeries).size() > maxOracleDataSeries)
    {
        JLOG(ctx.j.debug()) << "Oracle Set: price data series too large";
        return temARRAY_SIZE;
    }

    if (ctx.tx.getFieldVL(sfProvider).size() > maxOracleProvider)
    {
        JLOG(ctx.j.debug()) << "Oracle Set: provider too large";
        return temMALFORMED;
    }

    if (ctx.tx.getFieldVL(sfURI).size() > maxOracleURI)
    {
        JLOG(ctx.j.debug()) << "Oracle Set: URI too large";
        return temMALFORMED;
    }

    if (ctx.tx.getFieldVL(sfSymbolClass).size() > maxOracleSymbolClass)
    {
        JLOG(ctx.j.debug()) << "Oracle Set: symbol class too large";
        return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
SetOracle::preclaim(PreclaimContext const& ctx)
{
    hash_set<uint256> pairs;
    for (auto const& entry : ctx.tx.getFieldArray(sfPriceDataSeries))
    {
        auto const hash = sha512Half(
            entry.getFieldCurrency(sfSymbol).currency(),
            entry.getFieldCurrency(sfPriceUnit).currency());
        if (pairs.contains(hash))
            return tecDUPLICATE;
        pairs.emplace(hash);
    }

    if (auto const sle = ctx.view.read(keylet::oracle(
            ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleSequence])))
    {
        if (ctx.tx[sfAccount] != sle->getAccountID(sfOwner))
            return tecNO_PERMISSION;

        if (ctx.tx.isFieldPresent(sfProvider) ||
            ctx.tx.isFieldPresent(sfSymbolClass))
            return temMALFORMED;

        for (auto const& entry : sle->getFieldArray(sfPriceDataSeries))
        {
            auto const hash = sha512Half(
                entry.getFieldCurrency(sfSymbol).currency(),
                entry.getFieldCurrency(sfPriceUnit).currency());
            if (!pairs.contains(hash))
                pairs.emplace(hash);
        }
    }
    else
    {
        if (!ctx.tx.isFieldPresent(sfProvider) ||
            !ctx.tx.isFieldPresent(sfSymbolClass))
            return temMALFORMED;
    }

    if (pairs.size() == 0 || pairs.size() > 10)
        return temARRAY_SIZE;

    auto const sleSetter =
        ctx.view.read(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!sleSetter)
        return {tefINTERNAL};

    auto const reserve = ctx.view.fees().accountReserve(
        sleSetter->getFieldU32(sfOwnerCount) + pairs.size() <= 5 ? 1 : 2);
    auto const balance = sleSetter->getFieldAmount(sfBalance);

    if (balance < reserve)
    {
        JLOG(ctx.j.debug()) << "Oracle Set: insufficient reserve";
        return tecINSUFFICIENT_RESERVE;
    }

    return tesSUCCESS;
}

static std::pair<TER, bool>
applySet(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    auto const oracleID = keylet::oracle(account_, ctx_.tx[sfOracleSequence]);

    if (auto sle = sb.peek(oracleID))
    {
        // update Oracle

        hash_map<uint256, STObject> pairs;
        // collect current pairs
        for (auto const& entry : sle->getFieldArray(sfPriceDataSeries))
        {
            STObject priceData{sfPriceData};
            priceData.setFieldCurrency(
                sfSymbol, entry.getFieldCurrency(sfSymbol));
            priceData.setFieldCurrency(
                sfPriceUnit, entry.getFieldCurrency(sfPriceUnit));
            priceData.setFieldU64(sfSymbolPrice, 0);
            priceData.setFieldU8(sfScale, 0);
            pairs.emplace(
                sha512Half(
                    entry.getFieldCurrency(sfSymbol).currency(),
                    entry.getFieldCurrency(sfPriceUnit).currency()),
                std::move(priceData));
        }
        // update/add pairs
        for (auto const& entry : ctx_.tx.getFieldArray(sfPriceDataSeries))
        {
            auto const hash = sha512Half(
                entry.getFieldCurrency(sfSymbol).currency(),
                entry.getFieldCurrency(sfPriceUnit).currency());
            if (auto iter = pairs.find(hash); iter != pairs.end())
            {
                iter->second.setFieldU64(
                    sfSymbolPrice, entry.getFieldU64(sfSymbolPrice));
                iter->second.setFieldU8(sfScale, entry.getFieldU8(sfScale));
            }
            else
            {
                STObject priceData{sfPriceData};
                priceData.setFieldCurrency(
                    sfSymbol, entry.getFieldCurrency(sfSymbol));
                priceData.setFieldCurrency(
                    sfPriceUnit, entry.getFieldCurrency(sfPriceUnit));
                priceData.setFieldU64(
                    sfSymbolPrice, entry.getFieldU64(sfSymbolPrice));
                priceData.setFieldU8(sfScale, entry.getFieldU8(sfScale));
                pairs.emplace(
                    sha512Half(
                        entry.getFieldCurrency(sfSymbol).currency(),
                        entry.getFieldCurrency(sfPriceUnit).currency()),
                    std::move(priceData));
            }
        }
        STArray updatedSeries;
        for (auto iter : pairs)
            updatedSeries.push_back(std::move(iter.second));
        sle->setFieldArray(sfPriceDataSeries, updatedSeries);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);
        sle->setFieldU32(sfLastUpdateTime, ctx_.tx[sfLastUpdateTime]);

        sb.update(sle);
    }
    else
    {
        // create new Oracle

        sle = std::make_shared<SLE>(oracleID);
        sle->setAccountID(sfOwner, ctx_.tx.getAccountID(sfAccount));
        sle->setFieldVL(sfProvider, ctx_.tx[sfProvider]);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);
        sle->setFieldArray(
            sfPriceDataSeries, ctx_.tx.getFieldArray(sfPriceDataSeries));
        sle->setFieldVL(sfSymbolClass, ctx_.tx[sfSymbolClass]);
        sle->setFieldU32(sfLastUpdateTime, ctx_.tx[sfLastUpdateTime]);

        auto page = sb.dirInsert(
            keylet::ownerDir(account_), sle->key(), describeOwnerDir(account_));
        if (!page)
            return {tecDIR_FULL, false};

        (*sle)[sfOwnerNode] = *page;

        adjustOwnerCount(sb, sb.peek(keylet::account(account_)), 1, j_);

        sb.insert(sle);
    }

    return {tesSUCCESS, true};
}

TER
SetOracle::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applySet(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
