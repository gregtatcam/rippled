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

static inline uint256
tokenPairHash(STObject const& pair)
{
    return sha512Half(
        pair.getFieldCurrency(sfSymbol).currency(),
        pair.getFieldCurrency(sfPriceUnit).currency());
}

NotTEC
SetOracle::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featurePriceOracle))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const& dataSeries = ctx.tx.getFieldArray(sfPriceDataSeries);
    if (dataSeries.size() == 0 || dataSeries.size() > maxOracleDataSeries)
        return temARRAY_SIZE;

    auto invalidLength = [&](auto const& sField, std::size_t length) {
        return ctx.tx.isFieldPresent(sField) &&
            ctx.tx[sField].length() > length;
    };

    if (invalidLength(sfProvider, maxOracleProvider) ||
        invalidLength(sfURI, maxOracleURI) ||
        invalidLength(sfSymbolClass, maxOracleSymbolClass))
        return temMALFORMED;

    return preflight2(ctx);
}

TER
SetOracle::preclaim(PreclaimContext const& ctx)
{
    auto const sleSetter =
        ctx.view.read(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!sleSetter)
        return {terNO_ACCOUNT};

    // lastUpdateTime must be within 30 seconds of the last closed ledger
    using namespace std::chrono;
    std::size_t const closeTime =
        duration_cast<seconds>(ctx.view.info().closeTime.time_since_epoch())
            .count();
    std::size_t const lastUpdateTime = ctx.tx[sfLastUpdateTime];
    if (lastUpdateTime < closeTime ||
        lastUpdateTime > (closeTime + maxLastUpdateTimeDelta))
        return tecINVALID_UPDATE_TIME;

    hash_set<uint256> pairs;
    for (auto const& entry : ctx.tx.getFieldArray(sfPriceDataSeries))
    {
        if (!entry.isFieldPresent(sfSymbolPrice))
            return temMALFORMED;
        auto const hash = tokenPairHash(entry);
        if (pairs.contains(hash))
            return tecDUPLICATE;
        pairs.emplace(hash);
    }

    if (auto const sle = ctx.view.read(keylet::oracle(
            ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleSequence])))
    {
        // update

        // lastUpdateTime must be more recent than the previous one
        if (ctx.tx[sfLastUpdateTime] <= (*sle)[sfLastUpdateTime])
            return tecINVALID_UPDATE_TIME;

        if (ctx.tx[sfAccount] != sle->getAccountID(sfOwner))
            return tecNO_PERMISSION;

        if (ctx.tx.isFieldPresent(sfProvider) ||
            ctx.tx.isFieldPresent(sfSymbolClass))
            return temMALFORMED;

        for (auto const& entry : sle->getFieldArray(sfPriceDataSeries))
        {
            auto const hash = tokenPairHash(entry);
            if (!pairs.contains(hash))
                pairs.emplace(hash);
        }
    }
    else
    {
        // create

        if (!ctx.tx.isFieldPresent(sfProvider) ||
            !ctx.tx.isFieldPresent(sfSymbolClass))
            return temMALFORMED;
    }

    if (pairs.size() > maxOracleDataSeries)
        return temARRAY_SIZE;

    auto const add = pairs.size() <= 5 ? 1 : 2;
    auto const reserve = ctx.view.fees().accountReserve(
        sleSetter->getFieldU32(sfOwnerCount) + add);
    auto const& balance = sleSetter->getFieldAmount(sfBalance);

    if (balance < reserve)
        return tecINSUFFICIENT_RESERVE;

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
        // update
        // the token pair that doesn't have their price updated will not
        // include neither price nor scale in the updated PriceDataSeries

        hash_map<uint256, STObject> pairs;
        // collect current token pairs
        for (auto const& entry : sle->getFieldArray(sfPriceDataSeries))
        {
            STObject priceData{sfPriceData};
            priceData.setFieldCurrency(
                sfSymbol, entry.getFieldCurrency(sfSymbol));
            priceData.setFieldCurrency(
                sfPriceUnit, entry.getFieldCurrency(sfPriceUnit));
            pairs.emplace(tokenPairHash(entry), std::move(priceData));
        }
        // update/add pairs
        for (auto const& entry : ctx_.tx.getFieldArray(sfPriceDataSeries))
        {
            auto const hash = tokenPairHash(entry);
            if (auto iter = pairs.find(hash); iter != pairs.end())
            {
                // update the price
                iter->second.setFieldU64(
                    sfSymbolPrice, entry.getFieldU64(sfSymbolPrice));
                if (entry.isFieldPresent(sfScale))
                    iter->second.setFieldU8(sfScale, entry.getFieldU8(sfScale));
            }
            else
            {
                // add a token pair with the price
                STObject priceData{sfPriceData};
                priceData.setFieldCurrency(
                    sfSymbol, entry.getFieldCurrency(sfSymbol));
                priceData.setFieldCurrency(
                    sfPriceUnit, entry.getFieldCurrency(sfPriceUnit));
                priceData.setFieldU64(
                    sfSymbolPrice, entry.getFieldU64(sfSymbolPrice));
                if (entry.isFieldPresent(sfScale))
                    priceData.setFieldU8(sfScale, entry.getFieldU8(sfScale));
                pairs.emplace(hash, std::move(priceData));
            }
        }
        STArray updatedSeries;
        for (auto const& iter : pairs)
            updatedSeries.push_back(std::move(iter.second));
        sle->setFieldArray(sfPriceDataSeries, updatedSeries);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);
        sle->setFieldU32(sfLastUpdateTime, ctx_.tx[sfLastUpdateTime]);

        sb.update(sle);
    }
    else
    {
        // create

        auto const sleAccount = sb.peek(keylet::account(account_));
        if (!sleAccount)
            return {tefINTERNAL, false};

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

        adjustOwnerCount(sb, sleAccount, 1, j_);

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
