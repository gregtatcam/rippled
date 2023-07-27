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

#ifndef RIPPLE_TEST_JTX_ORACLE_H_INCLUDED
#define RIPPLE_TEST_JTX_ORACLE_H_INCLUDED

#include <test/jtx.h>

namespace ripple {
namespace test {
namespace jtx {

class Oracle
{
private:
    Env& env_;
    uint256 oracleID_;
    std::optional<msig> const msig_;
    std::uint32_t fee_;

private:
    void
    submit(
        Json::Value const& jv,
        std::optional<jtx::msig> const& msig,
        std::optional<jtx::seq> const& seq,
        std::optional<ter> const& ter);

public:
    Oracle(
        Env& env,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::uint32_t fee = 0);

    Oracle(
        Env& env,
        Account const& owner,
        std::string const& symbol,
        std::string const& priceUnit,
        std::string const& symbolClass,
        std::optional<std::uint8_t> numberHistorical = std::nullopt,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    Oracle(
        Env& env,
        Account const& owner,
        std::string const& name,
        std::string const& tomlDomain,
        std::optional<std::uint8_t> numberHistorical = std::nullopt,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    void
    create(
        std::uint32_t const& flags,
        AccountID const& owner,
        std::optional<std::string> const& symbol = std::nullopt,
        std::optional<std::string> const& priceUnit = std::nullopt,
        std::optional<std::string> const& symbolClass = std::nullopt,
        std::optional<std::string> const& name = std::nullopt,
        std::optional<std::string> const& tomlDomain = std::nullopt,
        std::optional<std::uint8_t> numberHistorical = std::nullopt,
        std::optional<uint256> const& oracleID = std::nullopt,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    void
    remove(
        AccountID const& owner,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::optional<uint256> const& oracleID = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    void
    update(
        std::uint32_t const& flags,
        AccountID const& owner,
        std::optional<std::uint64_t> const& price,
        std::optional<std::uint8_t> const& scale,
        std::optional<uint256> const& value,
        std::optional<std::uint32_t> const& lastUpdateTime,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::optional<uint256> const& oracleID = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    uint256
    oracleID() const
    {
        return oracleID_;
    }

    bool
    exists() const;

    uint256
    randOracleID() const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_ORACLE_H_INCLUDED
