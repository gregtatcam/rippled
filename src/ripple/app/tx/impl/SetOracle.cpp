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
        pair.getFieldCurrency(sfBaseAsset).currency(),
        pair.getFieldCurrency(sfQuoteAsset).currency());
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
        return temBAD_ARRAY_SIZE;

    auto isInvalidLength = [&](auto const& sField, std::size_t length) {
        return ctx.tx.isFieldPresent(sField) &&
            ctx.tx[sField].length() > length;
    };

    if (isInvalidLength(sfProvider, maxOracleProvider) ||
        isInvalidLength(sfURI, maxOracleURI) ||
        isInvalidLength(sfAssetClass, maxOracleSymbolClass))
        return temMALFORMED;

    return preflight2(ctx);
}

TER
SetOracle::preclaim(PreclaimContext const& ctx)
{
    auto const sleSetter =
        ctx.view.read(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!sleSetter)
        return terNO_ACCOUNT;

    // lastUpdateTime must be within maxLastUpdateTimeDelta seconds
    // of the last closed ledger
    using namespace std::chrono;
    std::size_t const closeTime =
        duration_cast<seconds>(ctx.view.info().closeTime.time_since_epoch())
            .count();
    std::size_t const lastUpdateTime = ctx.tx[sfLastUpdateTime];
    if (lastUpdateTime <= epoch_offset.count())
        return tecINVALID_UPDATE_TIME;
    std::size_t const lastUpdateTimeEpoch =
        lastUpdateTime - epoch_offset.count();
    if (lastUpdateTimeEpoch < closeTime ||
        lastUpdateTimeEpoch > (closeTime + maxLastUpdateTimeDelta))
        return tecINVALID_UPDATE_TIME;

    hash_set<uint256> pairs;
    for (auto const& entry : ctx.tx.getFieldArray(sfPriceDataSeries))
    {
        if (!entry.isFieldPresent(sfAssetPrice))
            return temMALFORMED;
        auto const hash = tokenPairHash(entry);
        if (pairs.contains(hash))
            return tecDUPLICATE;
        pairs.emplace(hash);
    }

    if (auto const sle = ctx.view.read(keylet::oracle(
            ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID])))
    {
        // update

        // lastUpdateTime must be more recent than the previous one
        if (ctx.tx[sfLastUpdateTime] <= (*sle)[sfLastUpdateTime])
            return tecINVALID_UPDATE_TIME;

        if (ctx.tx[sfAccount] != sle->getAccountID(sfOwner))
            return tecNO_PERMISSION;

        if (ctx.tx.isFieldPresent(sfProvider) ||
            ctx.tx.isFieldPresent(sfAssetClass))
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
            !ctx.tx.isFieldPresent(sfAssetClass))
            return temMALFORMED;
    }

    if (pairs.size() > maxOracleDataSeries)
        return temBAD_ARRAY_SIZE;

    auto const add = pairs.size() > 5 ? 2 : 1;
    auto const reserve = ctx.view.fees().accountReserve(
        sleSetter->getFieldU32(sfOwnerCount) + add);
    auto const& balance = sleSetter->getFieldAmount(sfBalance);

    if (balance < reserve)
        return tecINSUFFICIENT_RESERVE;

    return tesSUCCESS;
}

static bool
adjustOwnerCount(ApplyContext& ctx, std::uint16_t count)
{
    if (auto const sleAccount =
            ctx.view().peek(keylet::account(ctx.tx[sfAccount])))
    {
        adjustOwnerCount(ctx.view(), sleAccount, count, ctx.journal);
        return true;
    }

    return false;
}

TER
SetOracle::doApply()
{
    auto const oracleID = keylet::oracle(account_, ctx_.tx[sfOracleDocumentID]);

    if (auto sle = ctx_.view().peek(oracleID))
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
                sfBaseAsset, entry.getFieldCurrency(sfBaseAsset));
            priceData.setFieldCurrency(
                sfQuoteAsset, entry.getFieldCurrency(sfQuoteAsset));
            pairs.emplace(tokenPairHash(entry), std::move(priceData));
        }
        auto const oldCount = pairs.size() > 5 ? 2 : 1;
        // update/add pairs
        for (auto const& entry : ctx_.tx.getFieldArray(sfPriceDataSeries))
        {
            auto const hash = tokenPairHash(entry);
            if (auto iter = pairs.find(hash); iter != pairs.end())
            {
                // update the price
                iter->second.setFieldU64(
                    sfAssetPrice, entry.getFieldU64(sfAssetPrice));
                if (entry.isFieldPresent(sfScale))
                    iter->second.setFieldU8(sfScale, entry.getFieldU8(sfScale));
            }
            else
            {
                // add a token pair with the price
                STObject priceData{sfPriceData};
                priceData.setFieldCurrency(
                    sfBaseAsset, entry.getFieldCurrency(sfBaseAsset));
                priceData.setFieldCurrency(
                    sfQuoteAsset, entry.getFieldCurrency(sfQuoteAsset));
                priceData.setFieldU64(
                    sfAssetPrice, entry.getFieldU64(sfAssetPrice));
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
        // LastUpdateTime is Unix time, store internally as Ripple Epoch
        sle->setFieldU32(
            sfLastUpdateTime, ctx_.tx[sfLastUpdateTime] - epoch_offset.count());

        auto const newCount = pairs.size() > 5 ? 2 : 1;
        if (newCount > oldCount && !adjustOwnerCount(ctx_, 1))
            return tefINTERNAL;

        ctx_.view().update(sle);
    }
    else
    {
        // create

        sle = std::make_shared<SLE>(oracleID);
        sle->setAccountID(sfOwner, ctx_.tx.getAccountID(sfAccount));
        sle->setFieldVL(sfProvider, ctx_.tx[sfProvider]);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);
        auto const& series = ctx_.tx.getFieldArray(sfPriceDataSeries);
        sle->setFieldArray(sfPriceDataSeries, series);
        sle->setFieldVL(sfAssetClass, ctx_.tx[sfAssetClass]);
        // LastUpdateTime is Unix time, store internally as Ripple Epoch
        sle->setFieldU32(
            sfLastUpdateTime, ctx_.tx[sfLastUpdateTime] - epoch_offset.count());

        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(account_), sle->key(), describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;

        (*sle)[sfOwnerNode] = *page;

        auto const count = series.size() > 5 ? 2 : 1;
        if (!adjustOwnerCount(ctx_, count))
            return tefINTERNAL;

        ctx_.view().insert(sle);
    }

    return tesSUCCESS;
}

}  // namespace ripple
