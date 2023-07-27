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

#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/Oracle.h>

#include <vector>

namespace ripple {
namespace test {
namespace jtx {

Oracle::Oracle(
    Env& env,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee)
    : env_(env), msig_(msig), fee_(fee)
{
    oracleID_ = randOracleID();
}

Oracle::Oracle(
    Env& env,
    Account const& owner,
    std::string const& symbol,
    std::string const& priceUnit,
    std::string const& symbolClass,
    std::optional<std::uint8_t> numberHistorical,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee,
    std::optional<ter> const& ter)
    : Oracle(env, msig, fee)
{
    create(
        tfPriceOracle,
        owner,
        symbol,
        priceUnit,
        symbolClass,
        std::nullopt,
        std::nullopt,
        numberHistorical,
        std::nullopt,
        msig,
        fee,
        ter);
}

Oracle::Oracle(
    Env& env,
    Account const& owner,
    std::string const& name,
    std::string const& tomlDomain,
    std::optional<std::uint8_t> numberHistorical,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee,
    std::optional<ter> const& ter)
    : Oracle(env, msig, fee)
{
    create(
        tfAnyOracle,
        owner,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        name,
        tomlDomain,
        numberHistorical,
        std::nullopt,
        msig,
        fee,
        ter);
}

void
Oracle::create(
    std::uint32_t const& flags,
    AccountID const& owner,
    std::optional<std::string> const& symbol,
    std::optional<std::string> const& priceUnit,
    std::optional<std::string> const& symbolClass,
    std::optional<std::string> const& name,
    std::optional<std::string> const& tomlDomain,
    std::optional<std::uint8_t> numberHistorical,
    std::optional<uint256> const& oracleID,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleCreate;
    jv[jss::Flags] = flags;
    jv[jss::Account] = to_string(owner);
    jv[jss::Owner] = to_string(owner);  // do we need both?
    if (auto const f = fee != 0 ? fee : fee_)
        jv[jss::Fee] = std::to_string(f);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    jv[jss::OracleID] = to_string(oracleID.value_or(oracleID_));
    if (symbol)
        jv[jss::Symbol] = strHex(*symbol);
    if (priceUnit)
        jv[jss::PriceUnit] = strHex(*priceUnit);
    if (symbolClass)
        jv[jss::SymbolClass] = strHex(*symbolClass);
    if (name)
        jv[jss::Name] = strHex(*name);
    if (tomlDomain)
        jv[jss::TOMLDomain] = strHex(*tomlDomain);
    jv[jss::NumberHistorical] = numberHistorical.value_or(3);
    submit(jv, msig, std::nullopt, ter);
}

void
Oracle::remove(
    AccountID const& owner,
    std::optional<jtx::msig> const& msig,
    std::optional<uint256> const& oracleID,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleDelete;
    jv[jss::Account] = to_string(owner);
    jv[jss::OracleID] = to_string(oracleID.value_or(oracleID_));
    if (auto const f = fee != 0 ? fee : fee_)
        jv[jss::Fee] = std::to_string(f);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    submit(jv, msig, std::nullopt, ter);
}

void
Oracle::update(
    std::uint32_t const& flags,
    AccountID const& owner,
    std::optional<std::uint64_t> const& price,
    std::optional<std::uint8_t> const& scale,
    std::optional<uint256> const& value,
    std::optional<std::uint32_t> const& lastUpdateTime,
    std::optional<jtx::msig> const& msig,
    std::optional<uint256> const& oracleID,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    using namespace std::chrono;
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleDelete;
    jv[jss::Account] = to_string(owner);
    jv[jss::OracleID] = to_string(oracleID.value_or(oracleID_));
    if (auto const f = fee != 0 ? fee : fee_)
        jv[jss::Fee] = std::to_string(f);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    if (price)
        jv[jss::SymbolPrice] = std::to_string(*price);
    if (scale)
        jv[jss::Scale] = *scale;
    if (lastUpdateTime)
        jv[jss::LastUpdateTime] = *lastUpdateTime;
    else
        jv[jss::LastUpdateTime] = to_string(
            duration_cast<seconds>(env_.timeKeeper().now().time_since_epoch())
                .count());
    if (value)
        jv[jss::Value] = to_string(*value);
    submit(jv, msig, std::nullopt, ter);
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
Oracle::exists() const
{
    return env_.le(keylet::oracle(oracleID_)) != nullptr;
}

uint256
Oracle::randOracleID() const
{
    auto keys = randomKeyPair(KeyType::secp256k1);
    return sha512Half(keys.first);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple