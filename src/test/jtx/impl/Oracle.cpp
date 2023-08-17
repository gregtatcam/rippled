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
    std::string const& provider,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee,
    std::optional<ter> const& ter)
    : Oracle(env, msig, fee)
{
    create(owner, symbol, priceUnit, symbolClass, provider, 0, msig, fee, ter);
}

void
Oracle::create(
    AccountID const& owner,
    std::string const& symbol,
    std::string const& priceUnit,
    std::string const& symbolClass,
    std::string const& provider,
    std::uint32_t flags,
    std::optional<jtx::msig> const& msig,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    oracleID_ = sha512Half(
        oracleNameSpace_, owner, to_currency(symbol), to_currency(priceUnit));
    jv[jss::TransactionType] = jss::OracleCreate;
    jv[jss::Account] = to_string(owner);
    if (flags != 0)
        jv[jss::Flags] = flags;
    if (auto const f = fee != 0 ? fee : fee_)
        jv[jss::Fee] = std::to_string(f);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    jv[jss::Symbol] = symbol;
    jv[jss::PriceUnit] = priceUnit;
    jv[jss::SymbolClass] = strHex(symbolClass);
    jv[jss::Provider] = strHex(provider);
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
    AccountID const& owner,
    std::uint64_t const& price,
    std::uint8_t const& scale,
    std::optional<std::uint32_t> const& lastUpdateTime,
    std::uint32_t flags,
    std::optional<jtx::msig> const& msig,
    std::optional<uint256> const& oracleID,
    std::uint32_t fee,
    std::optional<ter> const& ter)
{
    using namespace std::chrono;
    Json::Value jv;
    jv[jss::TransactionType] = jss::OracleUpdate;
    jv[jss::Account] = to_string(owner);
    jv[jss::OracleID] = to_string(oracleID.value_or(oracleID_));
    if (auto const f = fee != 0 ? fee : fee_)
        jv[jss::Fee] = std::to_string(f);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    std::stringstream str;
    str << std::hex << price;
    jv[jss::SymbolPrice] = str.str();
    jv[jss::Scale] = scale;
    if (lastUpdateTime)
        jv[jss::LastUpdateTime] = *lastUpdateTime;
    else
        jv[jss::LastUpdateTime] = to_string(
            duration_cast<seconds>(env_.timeKeeper().now().time_since_epoch())
                .count());
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

bool
Oracle::expectPrice(std::uint64_t price, std::uint8_t scale)
{
    if (auto const sle = env_.le(keylet::oracle(oracleID_)))
        return sle->getFieldU64(sfSymbolPrice) == price &&
            sle->getFieldU8(sfScale) == scale;
    return false;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple