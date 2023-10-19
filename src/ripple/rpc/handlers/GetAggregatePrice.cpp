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

#include <ripple/app/main/Application.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

/**
 * oracles: array of OracleID
 * symbol: is the symbol to be priced
 * priceUnit: is the denomination in which the prices are expressed
 * trim : percentage of outliers to trim [optional]
 * flags : specify aggregation type. at least one flag must be included
 *   tfSimpleAverage : 0x01
 *   tfMedian :        0x02
 *   tfTrimmedMedian : 0x04
 */
Json::Value
doGetAggregatePrice(RPC::JsonContext& context)
{
    auto const& params(context.params);
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    if (!params.isMember(jss::oracles))
        return RPC::missing_field_error(jss::oracles);
    if (!params[jss::oracles].isArray())
    {
        RPC::inject_error(rpcORACLE_MALFORMED, result);
        return result;
    }

    if (!params.isMember(jss::symbol))
        return RPC::missing_field_error(jss::symbol);

    if (!params.isMember(jss::price_unit))
        return RPC::missing_field_error(jss::price_unit);

    std::optional<std::uint8_t> trim = params.isMember(jss::trim)
        ? std::make_optional(params[jss::trim].asUInt())
        : std::nullopt;

    auto const symbol = params[jss::symbol];
    auto const priceUnit = params[jss::price_unit];

    // prices sorted low to high. use STAmount since Number is int64 only
    std::vector<STAmount> prices;
    Issue const someIssue = {to_currency("SOM"), AccountID{1}};
    STAmount avg{someIssue, 0, 0};
    for (auto const& oracle : params[jss::oracles])
    {
        if (!oracle.isMember(jss::oracle_sequence) ||
            !oracle.isMember(jss::account))
        {
            RPC::inject_error(rpcORACLE_MALFORMED, result);
            return result;
        }
        auto const sequence = oracle[jss::oracle_sequence].isConvertibleTo(
                                  Json::ValueType::uintValue)
            ? std::make_optional(oracle[jss::oracle_sequence].asUInt())
            : std::nullopt;
        auto const account =
            parseBase58<AccountID>(oracle[jss::account].asString());
        if (!account || account->isZero() || !sequence)
        {
            RPC::inject_error(rpcINVALID_PARAMS, result);
            return result;
        }
        if (auto const sle = ledger->read(keylet::oracle(*account, *sequence)))
        {
            auto const series = sle->getFieldArray(sfPriceDataSeries);
            if (auto iter = std::find_if(
                    series.begin(),
                    series.end(),
                    [&](STObject const& o) -> bool {
                        return o.getFieldCurrency(sfSymbol).getText() ==
                            symbol &&
                            o.getFieldCurrency(sfPriceUnit).getText() ==
                            priceUnit;
                    });
                iter == series.end())
            {
                RPC::inject_error(rpcOBJECT_NOT_FOUND, result);
                return result;
            }
            else
            {
                auto const price = iter->getFieldU64(sfSymbolPrice);
                auto const scale = -static_cast<int>(iter->getFieldU8(sfScale));
                prices.push_back(STAmount{someIssue, price, scale});
            }
            avg += prices.back();
        }
        else
        {
            RPC::inject_error(rpcOBJECT_NOT_FOUND, result);
            return result;
        }
    }
    if (prices.empty())
    {
        RPC::inject_error(rpcORACLE_MALFORMED, result);
        return result;
    }
    avg = divide(
        avg,
        STAmount{someIssue, static_cast<std::uint64_t>(prices.size()), 0},
        someIssue);
    result[jss::simple_average] = avg.getText();
    result[jss::size] = static_cast<std::uint32_t>(prices.size());
    auto sd = STAmount{someIssue, 0};
    result[jss::standard_deviation] = "0";
    if (prices.size() > 1)
    {
        for (auto const& price : prices)
            sd += multiply(price - avg, price - avg, someIssue);
        result[jss::standard_deviation] = to_string(root2(sd));
    }

    std::stable_sort(prices.begin(), prices.end());
    auto const median = [&]() {
        if (prices.size() % 2 == 0)
        {
            // Even number of elements
            size_t middle = prices.size() / 2;
            return divide(
                prices[middle - 1] + prices[middle],
                STAmount{someIssue, 2, 0},
                someIssue);
        }
        else
        {
            // Odd number of elements
            return divide(
                prices[prices.size()], STAmount{someIssue, 2, 0}, someIssue);
        }
    }();
    result[jss::median] = median.getText();

    if (trim)
    {
        auto const trimCount = prices.size() * *trim / 100;
        size_t start = trimCount;
        size_t end = prices.size() - trimCount;

        avg = std::accumulate(
            prices.begin() + trimCount,
            prices.begin() + end,
            STAmount{someIssue, 0, 0});

        avg = divide(
            avg,
            STAmount{someIssue, static_cast<std::uint64_t>(end - start), 0},
            someIssue);
        result[jss::trimmed_mean] = avg.getText();
    }

    return result;
}

}  // namespace ripple
