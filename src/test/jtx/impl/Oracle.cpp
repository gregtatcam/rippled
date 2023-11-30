//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/Oracle.h>

#include <boost/lexical_cast/try_lexical_convert.hpp>

#include <vector>

namespace ripple {
namespace test {
namespace jtx {

Oracle::Oracle(Env& env, CreateArg const& arg, bool submit)
    : env_(env), owner_{}, sequence_{}
{
    if (arg.owner)
        owner_ = *arg.owner;
    if (arg.sequence)
        sequence_ = *arg.sequence;
    if (submit)
        set(arg);
}

void
Oracle::remove(RemoveArg const& arg)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleDelete;
    jv[jss::Account] = to_string(arg.owner.value_or(owner_));
    jv[jss::OracleDocumentID] = arg.sequence.value_or(sequence_);
    if (Oracle::fee != 0)
        jv[jss::Fee] = std::to_string(Oracle::fee);
    else if (arg.fee != 0)
        jv[jss::Fee] = std::to_string(arg.fee);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    submit(jv, arg.msig, arg.seq, arg.ter);
}

void
Oracle::submit(
    Json::Value const& jv,
    std::optional<jtx::msig> const& msig,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    if (msig)
    {
        if (seq && ter)
            env_(jv, *msig, *seq, *ter);
        else if (seq)
            env_(jv, *msig, *seq);
        else if (ter)
            env_(jv, *msig, *ter);
        else
            env_(jv, *msig);
    }
    else if (seq && ter)
        env_(jv, *seq, *ter);
    else if (seq)
        env_(jv, *seq);
    else if (ter)
        env_(jv, *ter);
    else
        env_(jv);
    env_.close();
}

bool
Oracle::exists(Env& env, AccountID const& account, std::uint32_t sequence)
{
    assert(account.isNonZero());
    return env.le(keylet::oracle(account, sequence)) != nullptr;
}

bool
Oracle::expectPrice(DataSeries const& series) const
{
    if (auto const sle = env_.le(keylet::oracle(owner_, sequence_)))
    {
        auto const& leSeries = sle->getFieldArray(sfPriceDataSeries);
        if (leSeries.size() == 0 || leSeries.size() != series.size())
            return false;
        for (auto const& data : series)
        {
            if (std::find_if(
                    leSeries.begin(),
                    leSeries.end(),
                    [&](STObject const& o) -> bool {
                        auto const& baseAsset = o.getFieldCurrency(sfBaseAsset);
                        auto const& quoteAsset =
                            o.getFieldCurrency(sfQuoteAsset);
                        auto const& price = o.getFieldU64(sfAssetPrice);
                        auto const& scale = o.getFieldU8(sfScale);
                        return baseAsset.getText() == std::get<0>(data) &&
                            quoteAsset.getText() == std::get<1>(data) &&
                            price == std::get<2>(data) &&
                            scale == std::get<3>(data);
                    }) == leSeries.end())
                return false;
        }
        return true;
    }
    return false;
}

bool
Oracle::expectLastUpdateTime(std::uint32_t lastUpdateTime) const
{
    auto const sle = env_.le(keylet::oracle(owner_, sequence_));
    return sle && (*sle)[sfLastUpdateTime] == lastUpdateTime;
}

Json::Value
Oracle::aggregatePrice(
    Env& env,
    std::optional<std::string> const& baseAsset,
    std::optional<std::string> const& quoteAsset,
    std::optional<std::vector<std::pair<Account, std::uint32_t>>> const&
        oracles,
    std::optional<std::uint8_t> const& trim,
    std::optional<std::uint8_t> const& timeThreshold)
{
    Json::Value jv;
    Json::Value jvOracles(Json::arrayValue);
    if (oracles)
    {
        for (auto const& id : *oracles)
        {
            Json::Value oracle;
            oracle[jss::account] = to_string(id.first.id());
            oracle[jss::oracle_sequence] = id.second;
            jvOracles.append(oracle);
        }
        jv[jss::oracles] = jvOracles;
    }
    if (trim)
        jv[jss::trim] = *trim;
    if (baseAsset)
        jv[jss::base_asset] = *baseAsset;
    if (quoteAsset)
        jv[jss::quote_asset] = *quoteAsset;
    if (timeThreshold)
        jv[jss::time_threshold] = *timeThreshold;

    auto jr = env.rpc("json", "get_aggregate_price", to_string(jv));

    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::status))
        return jr[jss::result];
    return Json::nullValue;
}

void
Oracle::set(UpdateArg const& arg)
{
    using namespace std::chrono;
    Json::Value jv;
    if (arg.owner)
        owner_ = *arg.owner;
    if (arg.sequence)
        sequence_ = *arg.sequence;
    jv[jss::TransactionType] = jss::OracleSet;
    jv[jss::Account] = to_string(owner_);
    jv[jss::OracleDocumentID] = sequence_;
    if (arg.assetClass)
        jv[jss::AssetClass] = strHex(*arg.assetClass);
    if (arg.provider)
        jv[jss::Provider] = strHex(*arg.provider);
    if (arg.uri)
        jv[jss::URI] = strHex(*arg.uri);
    if (arg.flags != 0)
        jv[jss::Flags] = arg.flags;
    if (Oracle::fee != 0)
        jv[jss::Fee] = std::to_string(Oracle::fee);
    else if (arg.fee != 0)
        jv[jss::Fee] = std::to_string(arg.fee);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    if (arg.lastUpdateTime)
        jv[jss::LastUpdateTime] = *arg.lastUpdateTime +
            static_cast<std::uint32_t>(epoch_offset.count());
    else
        jv[jss::LastUpdateTime] = to_string(
            duration_cast<seconds>(
                env_.current()->info().closeTime.time_since_epoch())
                .count() +
            10 + epoch_offset.count());
    Json::Value dataSeries(Json::arrayValue);
    auto assetToStr = [](std::string const& s) {
        // assume standard currency
        if (s.size() == 3)
            return s;
        assert(s.size() <= 20);
        // anything else must be 160-bit hex string
        std::string h = strHex(s);
        return strHex(s).append(40 - s.size() * 2, '0');
    };
    for (auto const& data : arg.series)
    {
        Json::Value priceData;
        Json::Value price;
        price[jss::BaseAsset] = assetToStr(std::get<0>(data));
        price[jss::QuoteAsset] = assetToStr(std::get<1>(data));
        if (std::get<2>(data))
            price[jss::AssetPrice] = *std::get<2>(data);
        if (std::get<3>(data))
            price[jss::Scale] = *std::get<3>(data);
        priceData[jss::PriceData] = price;
        dataSeries.append(priceData);
    }
    jv[jss::PriceDataSeries] = dataSeries;
    submit(jv, arg.msig, arg.seq, arg.ter);
}

void
Oracle::set(CreateArg const& arg)
{
    set(UpdateArg{
        .owner = arg.owner,
        .sequence = arg.sequence,
        .series = arg.series,
        .assetClass = arg.assetClass,
        .provider = arg.provider,
        .uri = arg.uri,
        .lastUpdateTime = arg.lastUpdateTime,
        .flags = arg.flags,
        .msig = arg.msig,
        .seq = arg.seq,
        .fee = arg.fee,
        .ter = arg.ter});
}

Json::Value
Oracle::ledgerEntry(
    Env& env,
    AccountID const& account,
    std::uint32_t sequence,
    std::optional<std::string> const& index)
{
    Json::Value jvParams;
    jvParams[jss::oracle][jss::account] = to_string(account);
    jvParams[jss::oracle][jss::oracle_sequence] = sequence;
    if (index)
    {
        std::uint32_t i;
        if (boost::conversion::try_lexical_convert(*index, i))
            jvParams[jss::oracle][jss::ledger_index] = i;
        else
            jvParams[jss::oracle][jss::ledger_index] = *index;
    }
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple