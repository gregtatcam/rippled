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

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

namespace ripple {

struct CompareDescending
{
    bool
    operator()(std::uint32_t const& a, std::uint32_t const& b) const
    {
        return a > b;  // Sort lastUpdateTime in descending order
    }
};

using namespace boost::bimaps;
using Prices =
    bimap<multiset_of<std::uint32_t, CompareDescending>, multiset_of<STAmount>>;
using PriceData = std::pair<std::uint32_t, STAmount>;

class PriceDataIterator
{
private:
    RPC::JsonContext& context_;
    STObject const* sle_;

public:
    PriceDataIterator(RPC::JsonContext& context, STObject const* sle)
        : context_(context), sle_(sle)
    {
    }

    void
    foreach(std::function<bool(STObject const&)>&& f)
    {
        constexpr std::uint8_t maxHistory = 3;

        if (!sle_)
            return;

        if (f(*sle_))
            return;

        uint256 prevTx = sle_->getFieldH256(sfPreviousTxnID);
        std::uint32_t prevSeq = sle_->getFieldU32(sfPreviousTxnLgrSeq);
        std::uint8_t history = 0;

        auto getMeta = [&]() -> std::shared_ptr<STObject const> {
            if (auto const ledger =
                    context_.ledgerMaster.getLedgerBySeq(prevSeq))
                return ledger->txRead(prevTx).second;
            return nullptr;
        };

        while (++history <= maxHistory)
        {
            if (auto const meta = getMeta())
            {
                for (STObject const& node :
                     meta->getFieldArray(sfAffectedNodes))
                {
                    if (node.getFieldU16(sfLedgerEntryType) == ltORACLE)
                    {
                        bool const isNew = node.isFieldPresent(sfNewFields);
                        // if a meta is for the new and this is the first
                        // look-up then it's the meta for the tx that
                        // created the current object; i.e. there is no
                        // historical data
                        if (isNew && history == 1)
                            return;

                        auto const& n = isNew
                            ? static_cast<const STObject&>(
                                  node.peekAtField(sfNewFields))
                            : static_cast<const STObject&>(
                                  node.peekAtField(sfFinalFields));

                        if (f(n))
                            return;

                        if (!isNew)
                        {
                            prevTx = node.getFieldH256(sfPreviousTxnID);
                            prevSeq = node.getFieldU32(sfPreviousTxnLgrSeq);
                        }
                        else
                            return;

                        break;
                    }
                }
            }
        }
    }
};

// Return avg, sd, data set size
static std::tuple<STAmount, Number, std::uint16_t>
getStats(
    Prices::right_const_iterator const& begin,
    Prices::right_const_iterator const& end)
{
    STAmount avg{noIssue(), 0, 0};
    Number sd{0};
    std::uint16_t size = std::distance(begin, end);
    avg = std::accumulate(
        begin, end, avg, [&](STAmount const& acc, auto const& it) {
            return acc + it.first;
        });
    avg = divide(avg, STAmount{noIssue(), size, 0}, noIssue());
    if (size > 1)
    {
        sd = std::accumulate(
            begin, end, sd, [&](Number const& acc, auto const& it) {
                return acc + (it.first - avg) * (it.first - avg);
            });
        sd = root2(sd / (size - 1));
    }
    return {avg, sd, size};
};

/**
 * oracles: array of {account, oracle_sequence}
 * symbol: is the symbol to be priced
 * priceUnit: is the denomination in which the prices are expressed
 * trim : percentage of outliers to trim [optional]
 * time_threshold : defines a range of prices to include based on the timestamp
 *   range - {most recent, most recent - time_threshold} [optional]
 */
Json::Value
doGetAggregatePrice(RPC::JsonContext& context)
{
    auto const& params(context.params);
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    constexpr std::uint16_t maxOracles = 200;
    if (!params.isMember(jss::oracles))
        return RPC::missing_field_error(jss::oracles);
    if (!params[jss::oracles].isArray() || params[jss::oracles].size() == 0 ||
        params[jss::oracles].size() > maxOracles)
    {
        RPC::inject_error(rpcORACLE_MALFORMED, result);
        return result;
    }

    if (!params.isMember(jss::symbol))
        return RPC::missing_field_error(jss::symbol);

    if (!params.isMember(jss::price_unit))
        return RPC::missing_field_error(jss::price_unit);

    auto getField = [&](Json::StaticString const& field,
                        unsigned int def =
                            0) -> std::variant<std::uint32_t, error_code_i> {
        if (params.isMember(field))
        {
            if (!params[field].isConvertibleTo(Json::ValueType::uintValue))
                return rpcORACLE_MALFORMED;
            return params[field].asUInt();
        }
        return def;
    };

    auto const trim = getField(jss::trim);
    if (std::holds_alternative<error_code_i>(trim))
    {
        RPC::inject_error(std::get<error_code_i>(trim), result);
        return result;
    }

    constexpr std::uint32_t defaultTimeThreshold = 4;
    auto const timeThreshold =
        getField(jss::time_threshold, defaultTimeThreshold);
    if (std::holds_alternative<error_code_i>(timeThreshold))
    {
        RPC::inject_error(std::get<error_code_i>(timeThreshold), result);
        return result;
    }

    auto const symbol = params[jss::symbol];
    auto const priceUnit = params[jss::price_unit];

    // Collect the dataset into bimap keyed by lastUpdateTime and
    // STAmount (Number is int64 and price is uint64)
    Prices prices;
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

        auto const sle = ledger->read(keylet::oracle(*account, *sequence));
        PriceDataIterator it(context, sle ? sle.get() : nullptr);
        it.foreach([&](STObject const& node) {
            auto const& series = node.getFieldArray(sfPriceDataSeries);
            // find the token pair entry
            if (auto iter = std::find_if(
                    series.begin(),
                    series.end(),
                    [&](STObject const& o) -> bool {
                        return o.getFieldCurrency(sfSymbol).getText() ==
                            symbol &&
                            o.getFieldCurrency(sfPriceUnit).getText() ==
                            priceUnit;
                    });
                iter != series.end())
            {
                if (iter->isFieldPresent(sfSymbolPrice))
                {
                    auto const price = iter->getFieldU64(sfSymbolPrice);
                    auto const scale = iter->isFieldPresent(sfScale)
                        ? -static_cast<int>(iter->getFieldU8(sfScale))
                        : 0;
                    prices.insert(Prices::value_type(
                        node.getFieldU32(sfLastUpdateTime),
                        STAmount{noIssue(), price, scale}));
                    return true;
                }
            }
            return false;
        });
    }

    if (prices.empty())
    {
        RPC::inject_error(rpcOBJECT_NOT_FOUND, result);
        return result;
    }

    // erase outdated data
    // sorted in descending, therefore begin is the latest, end is the oldest
    auto const latestTime = prices.left.begin()->first;
    auto const oldestTime = prices.left.rbegin()->first;
    auto const threshold = std::get<std::uint32_t>(timeThreshold);
    auto const upperBound =
        latestTime > threshold ? (latestTime - threshold) : oldestTime;
    if (upperBound > oldestTime)
        prices.left.erase(
            prices.left.upper_bound(upperBound), prices.left.end());

    if (prices.empty())
    {
        RPC::inject_error(rpcOBJECT_NOT_FOUND, result);
        return result;
    }

    result[jss::time] = latestTime;
    // calculate stats
    auto const [avg, sd, size] =
        getStats(prices.right.begin(), prices.right.end());
    result[jss::entire_set][jss::average] = avg.getText();
    result[jss::entire_set][jss::size] = size;
    result[jss::entire_set][jss::standard_deviation] = to_string(sd);

    auto advRight = [&](auto begin, std::size_t by) {
        auto it = begin;
        std::advance(it, by);
        return it;
    };

    auto const median = [&]() {
        size_t const middle = size / 2;
        if ((size % 2) == 0)
        {
            static STAmount two{noIssue(), 2, 0};
            auto const sum = advRight(prices.right.begin(), middle - 1)->first +
                advRight(prices.right.begin(), middle)->first;
            return divide(sum, two, noIssue());
        }
        return advRight(prices.right.begin(), middle)->first;
    }();
    result[jss::median] = median.getText();

    if (std::get<std::uint32_t>(trim) != 0)
    {
        auto const trimCount =
            prices.size() * std::get<std::uint32_t>(trim) / 100;
        size_t start = trimCount;
        size_t end = prices.size() - trimCount;

        auto const [avg, sd, size] = getStats(
            advRight(prices.right.begin(), start),
            advRight(prices.right.begin(), end));
        result[jss::trimmed_set][jss::average] = avg.getText();
        result[jss::trimmed_set][jss::size] = size;
        result[jss::trimmed_set][jss::standard_deviation] = to_string(sd);
    }

    return result;
}

}  // namespace ripple
