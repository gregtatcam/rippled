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

#include <ripple/basics/safe_cast.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/Oracle.h>

#include <format>
#include <vector>

namespace ripple {
namespace test {
namespace jtx {

Oracle::Oracle(
    Env& env,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee)
    : env_(env)
    , owner_{}
    , oracleSequence_(time(nullptr))
    , msig_(msig)
    , fee_(fee)
{
}

Oracle::Oracle(
    Env& env,
    Account const& owner,
    std::uint32_t sequence,
    DataSeries const& series,
    std::string const& symbolClass,
    std::string const& provider,
    std::optional<std::string> const& URI,
    std::optional<std::uint32_t> const& lastUpdateTime,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee,
    std::optional<ter> const& ter)
    : env_(env)
    , owner_(owner)
    , oracleSequence_(sequence)
    , msig_(msig)
    , fee_(fee)
{
    create(
        owner,
        series,
        sequence,
        symbolClass,
        provider,
        URI,
        lastUpdateTime,
        0,
        msig,
        fee,
        ter);
}

Oracle::Oracle(
    Env& env,
    Account const& owner,
    std::uint32_t sequence,
    DataSeries const& series,
    std::optional<ter> const& ter)
    : Oracle(
          env,
          owner,
          sequence,
          series,
          "currency",
          "provider",
          "URI",
          std::nullopt,
          std::nullopt,
          0,
          ter)
{
}

void
Oracle::create(
    AccountID const& owner,
    DataSeries const& series,
    std::optional<std::uint32_t> const& sequence,
    std::optional<std::string> const& symbolClass,
    std::optional<std::string> const& provider,
    std::optional<std::string> const& URI,
    std::optional<std::uint32_t> const& lastUpdateTime,
    std::uint32_t flags,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    owner_ = owner;
    if (sequence)
        oracleSequence_ = *sequence;
    set(owner,
        series,
        symbolClass,
        provider,
        URI,
        lastUpdateTime,
        flags,
        msig,
        sequence,
        fee,
        ter);
}

void
Oracle::remove(
    AccountID const& owner,
    std::optional<jtx::msig> const& msig,
    std::optional<std::uint32_t> const& oracleSequence,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleDelete;
    jv[jss::Account] = to_string(owner);
    jv[jss::OracleSequence] = oracleSequence.value_or(oracleSequence_);
    if (auto const f = fee != 0 ? fee : fee_)
        jv[jss::Fee] = std::to_string(f);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    submit(jv, msig, std::nullopt, ter);
}

void
Oracle::update(
    AccountID const& owner,
    DataSeries const& series,
    std::optional<std::string> const& URI,
    std::optional<std::uint32_t> const& lastUpdateTime,
    std::uint32_t flags,
    std::optional<jtx::msig> const& msig,
    std::optional<std::uint32_t> const& oracleSequence,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    set(owner,
        series,
        std::nullopt,
        std::nullopt,
        URI,
        lastUpdateTime,
        flags,
        msig,
        oracleSequence,
        fee,
        ter);
}

void
Oracle::submit(
    Json::Value const& jv,
    std::optional<jtx::msig> const& msig,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    if (auto const& ms = msig ? msig : msig_)
    {
        if (seq && ter)
            env_(jv, *ms, *seq, *ter);
        else if (seq)
            env_(jv, *ms, *seq);
        else if (ter)
            env_(jv, *ms, *ter);
        else
            env_(jv, *ms);
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
    if (auto const sle = env_.le(keylet::oracle(owner_, oracleSequence_)))
    {
        auto const leSeries = sle->getFieldArray(sfPriceDataSeries);
        if (leSeries.size() != series.size())
            return false;
        for (auto const& data : series)
        {
            if (std::find_if(
                    leSeries.begin(),
                    leSeries.end(),
                    [&](STObject const& o) -> bool {
                        auto const& symbol = o.getFieldCurrency(sfSymbol);
                        auto const& priceUnit = o.getFieldCurrency(sfPriceUnit);
                        auto const& price = o.getFieldU64(sfSymbolPrice);
                        auto const& scale = o.getFieldU8(sfScale);
                        return symbol.getText() == std::get<0>(data) &&
                            priceUnit.getText() == std::get<1>(data) &&
                            price == std::get<2>(data) &&
                            scale == std::get<3>(data);
                    }) == leSeries.end())
                return false;
        }
        return true;
    }
    return false;
}

Json::Value
Oracle::aggregatePrice(
    Env& env,
    std::optional<std::string> const& symbol,
    std::optional<std::string> const& priceUnit,
    std::optional<std::vector<std::pair<AccountID, std::uint32_t>>> const&
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
            oracle[jss::account] = to_string(id.first);
            oracle[jss::oracle_sequence] = id.second;
            jvOracles.append(oracle);
        }
        jv[jss::oracles] = jvOracles;
    }
    if (trim)
        jv[jss::trim] = *trim;
    if (symbol)
        jv[jss::symbol] = *symbol;
    if (priceUnit)
        jv[jss::price_unit] = *priceUnit;
    if (timeThreshold)
        jv[jss::time_interval] = *timeThreshold;

    auto jr = env.rpc("json", "get_aggregate_price", to_string(jv));

    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::status))
        return jr[jss::result];
    return Json::nullValue;
}

void
Oracle::set(
    AccountID const& owner,
    DataSeries const& series,
    std::optional<std::string> const& symbolClass,
    std::optional<std::string> const& provider,
    std::optional<std::string> const& URI,
    std::optional<std::uint32_t> const& lastUpdateTime,
    std::uint32_t flags,
    std::optional<jtx::msig> const& msig,
    std::optional<std::uint32_t> const& oracleSequence,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    using namespace std::chrono;
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleSet;
    jv[jss::Account] = to_string(owner);
    jv[jss::OracleSequence] =
        to_string(oracleSequence.value_or(oracleSequence_));
    if (symbolClass)
        jv[jss::SymbolClass] = strHex(*symbolClass);
    if (provider)
        jv[jss::Provider] = strHex(*provider);
    if (URI)
        jv[jss::URI] = strHex(*URI);
    if (flags != 0)
        jv[jss::Flags] = flags;
    if (auto const f = fee != 0 ? fee : fee_)
        jv[jss::Fee] = std::to_string(f);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    if (lastUpdateTime)
        jv[jss::LastUpdateTime] = *lastUpdateTime;
    else
        jv[jss::LastUpdateTime] = to_string(
            duration_cast<seconds>(env_.timeKeeper().now().time_since_epoch())
                .count());
    Json::Value dataSeries(Json::arrayValue);
    for (auto const& data : series)
    {
        Json::Value priceData;
        Json::Value price;
        price[jss::Symbol] = std::get<0>(data);
        price[jss::PriceUnit] = std::get<1>(data);
        // std::stringstream str;
        // str << std::hex << std::get<3>(data);
        price[jss::SymbolPrice] = std::get<2>(data);
        price[jss::Scale] = std::get<3>(data);
        priceData[jss::PriceData] = price;
        dataSeries.append(priceData);
    }
    jv[jss::PriceDataSeries] = dataSeries;
    submit(jv, msig, std::nullopt, ter);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple