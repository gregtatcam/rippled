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

#ifndef RIPPLE_TEST_JTX_CFT_H_INCLUDED
#define RIPPLE_TEST_JTX_CFT_H_INCLUDED

#include <test/jtx/multisign.h>
#include <test/jtx/seq.h>
#include <test/jtx/ter.h>

#include <ripple/protocol/UintTypes.h>

namespace ripple {
namespace test {
namespace jtx {

class CFTIssuance
{
private:
    Env& env_;
    Keylet const cftID_;
    Account const issuer_;
    Currency const currency_;
    std::optional<jtx::msig> const msig_;
    bool const close_;

public:
    CFTIssuance(Env& env);

    CFTIssuance(
        Env& env,
        Account const& issuer,
        Currency const& currency,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::uint16_t tfee = 0,
        std::uint32_t fee = 0,
        std::uint32_t flags = 0,
        std::optional<jtx::seq> seq = std::nullopt,
        std::optional<jtx::msig> ms = std::nullopt,
        bool close = true);

    /** Destroy a CFT. */
    void
    destroy(
        std::optional<Account> const& acct = std::nullopt,
        std::optional<uint256> const id = std::nullopt,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::optional<jtx::seq> const& seq = std::nullopt);

    void
    cftrust(
        std::optional<Account> const& acct = std::nullopt,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::uint32_t flags = 0,
        std::optional<uint256> const id = std::nullopt,
        std::optional<jtx::seq> const& seq = std::nullopt);

    uint256
    cftIssuance() const
    {
        return cftID_.key;
    }

    Json::Value
    ledgerEntry(
        std::optional<AccountID> const& acct = std::nullopt,
        std::optional<uint256> const& id = std::nullopt) const;

    std::uint64_t
    outstandingAmount() const;

    std::uint64_t
    holderAmount(Account const& acct) const;

    CFT
    cft()
    {
        return CFT(issuer_, cftID_.key);
    }

    template <class T>
    requires(sizeof(T) >= sizeof(int) && std::is_arithmetic_v<T>)
    PrettyAmount cft(T v) const
    {
        return {
            amountFromString({cftID_.key, issuer_}, std::to_string(v)),
            issuer_.human()};
    }

private:
    /** Issue a CFT. */
    void
    create(
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::uint16_t tfee = 0,
        std::uint32_t fee = 0,
        std::uint32_t flags = 0,
        std::optional<jtx::seq> seq = std::nullopt);

    void
    submit(
        Json::Value const& jv,
        std::optional<jtx::seq> const& seq,
        std::optional<jtx::ter> const& ter);
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
