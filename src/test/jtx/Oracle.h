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
    // symbol, price unit, price, scale
    using DataSeries = std::vector<
        std::tuple<std::string, std::string, std::uint32_t, std::uint8_t>>;

private:
    static inline std::uint32_t id_ = 0;
    Env& env_;
    AccountID owner_;
    std::uint32_t oracleSequence_;
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
    using ustring = std::basic_string<unsigned char>;
    Oracle(
        Env& env,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::uint32_t fee = 0);

    Oracle(
        Env& env,
        Account const& owner,
        std::uint32_t sequence,
        DataSeries const& series,
        std::string const& symbolClass = "currency",
        std::string const& provider = "provider",
        std::optional<std::string> const& URI = "URI",
        std::optional<std::uint32_t> const& lastUpdateTime = std::nullopt,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    Oracle(
        Env& env,
        Account const& owner,
        std::uint32_t sequence,
        DataSeries const& series,
        std::optional<ter> const& ter);

    void
    create(
        AccountID const& owner,
        DataSeries const& series,
        std::optional<std::uint32_t> const& sequence = std::nullopt,
        std::optional<std::string> const& symbolClass = std::nullopt,
        std::optional<std::string> const& provider = std::nullopt,
        std::optional<std::string> const& URI = std::nullopt,
        std::optional<std::uint32_t> const& lastUpdateTime = std::nullopt,
        std::uint32_t flags = 0,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    void
    remove(
        AccountID const& owner,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::optional<std::uint32_t> const& oracleSequence = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    void
    update(
        AccountID const& owner,
        DataSeries const& series,
        std::optional<std::string> const& URI = std::nullopt,
        std::optional<std::uint32_t> const& lastUpdateTime = std::nullopt,
        std::uint32_t flags = 0,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::optional<std::uint32_t> const& oracleSequence = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

    static Json::Value
    aggregatePrice(
        Env& env,
        std::optional<std::string> const& symbol,
        std::optional<std::string> const& priceUnit,
        std::optional<std::vector<std::pair<AccountID, std::uint32_t>>> const&
            oracles = std::nullopt,
        std::optional<std::uint8_t> const& trim = std::nullopt,
        std::optional<std::uint8_t> const& timeTreshold = std::nullopt);

    std::uint32_t
    oracleSequence() const
    {
        return oracleSequence_;
    }

    bool
    exists() const
    {
        return exists(env_, owner_, oracleSequence_);
    }

    static bool
    exists(Env& env, AccountID const& account, std::uint32_t sequence);

    bool
    expectPrice(DataSeries const& pricess) const;

    void
    set(AccountID const& owner,
        DataSeries const& series,
        std::optional<std::string> const& symbolClass = std::nullopt,
        std::optional<std::string> const& provider = std::nullopt,
        std::optional<std::string> const& URI = std::nullopt,
        std::optional<std::uint32_t> const& lastUpdateTime = std::nullopt,
        std::uint32_t flags = 0,
        std::optional<jtx::msig> const& msig = std::nullopt,
        std::optional<std::uint32_t> const& oracleSequence = std::nullopt,
        std::uint32_t fee = 0,
        std::optional<ter> const& ter = std::nullopt);

private:
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_ORACLE_H_INCLUDED
