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

#include <test/jtx/cft.h>

#include <ripple/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

CFTIssuance::CFTIssuance(Env& env)
    : env_(env)
    , cftID_(keylet::cftIssuance(uint256{}))
    , issuer_(Account{})
    , currency_(Currency{})
    , msig_(std::nullopt)
    , close_(true)
{
}

CFTIssuance::CFTIssuance(
    Env& env,
    Account const& issuer,
    Currency const& currency,
    std::optional<jtx::ter> const& ter,
    std::uint16_t tfee,
    std::uint32_t fee,
    std::uint32_t flags,
    std::optional<jtx::seq> seq,
    std::optional<jtx::msig> ms,
    bool close)
    : env_(env)
    , cftID_(keylet::cftIssuance(issuer, currency))
    , issuer_(issuer)
    , currency_(currency)
    , msig_(ms)
    , close_(close)
{
    create(ter, tfee, fee, flags, seq);
}

void
CFTIssuance::submit(
    Json::Value const& jv,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    if (msig_)
    {
        if (seq && ter)
            env_(jv, *msig_, *seq, *ter);
        else if (seq)
            env_(jv, *msig_, *seq);
        else if (ter)
            env_(jv, *msig_, *ter);
        else
            env_(jv, *msig_);
    }
    else if (seq && ter)
        env_(jv, *seq, *ter);
    else if (seq)
        env_(jv, *seq);
    else if (ter)
        env_(jv, *ter);
    else
        env_(jv);
    if (close_)
        env_.close();
}

void
CFTIssuance::create(
    std::optional<jtx::ter> const& ter,
    std::uint16_t tfee,
    std::uint32_t fee,
    std::uint32_t flags,
    std::optional<jtx::seq> seq)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = issuer_.human();
    jv[sfAssetCode.jsonName] = ripple::to_string(currency_);
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceCreate;
    if (tfee)
        jv[sfTransferFee.jsonName] = tfee;
    if (fee)
        jv[sfFee.jsonName] = tfee;
    if (flags)
        jv[sfFlags.jsonName] = tfee;
    submit(jv, seq, ter);
}

void
CFTIssuance::destroy(
    std::optional<Account> const& acct,
    std::optional<uint256> const id,
    std::optional<jtx::ter> const& ter,
    std::optional<jtx::seq> const& seq)
{
    Json::Value jv;
    if (acct)
        jv[sfAccount.jsonName] = acct->human();
    else
        jv[sfAccount.jsonName] = issuer_.human();
    if (id)
        jv[sfCFTokenIssuanceID.jsonName] = to_string(*id);
    else
        jv[sfCFTokenIssuanceID.jsonName] = to_string(cftID_.key);
    jv[sfTransactionType.jsonName] = jss::CFTokenIssuanceDestroy;
    submit(jv, seq, ter);
}

void
CFTIssuance::cftrust(
    std::optional<Account> const& acct,
    std::optional<jtx::ter> const& ter,
    std::uint32_t flags,
    std::optional<uint256> const id,
    std::optional<jtx::seq> const& seq)
{
    Json::Value jv;
    if (acct)
        jv[sfAccount.jsonName] = acct->human();
    else
        jv[sfAccount.jsonName] = issuer_.human();
    if (id)
        jv[sfCFTokenIssuanceID.jsonName] = to_string(*id);
    else
        jv[sfCFTokenIssuanceID.jsonName] = to_string(cftID_.key);
    if (flags != 0)
        jv[sfFlags.jsonName] = flags;
    jv[sfTransactionType.jsonName] = jss::CFTokenTrust;
    submit(jv, seq, ter);
}

Json::Value
CFTIssuance::ledgerEntry(
    std::optional<AccountID> const& acct,
    std::optional<uint256> const& id) const
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    auto const cftid = [&]() {
        auto const cftID = id ? keylet::cftIssuance(*id) : cftID_;
        if (acct)
            return keylet::cftoken(*acct, cftID.key).key;
        return cftID.key;
    }();
    jvParams[jss::index] = to_string(cftid);
    return env_.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

std::uint64_t
CFTIssuance::outstandingAmount() const
{
    if (auto const sle = env_.current()->read(cftID_))
        return sle->getFieldU64(sfOutstandingAmount);
    return 0;
}

std::uint64_t
CFTIssuance::holderAmount(Account const& acct) const
{
    if (auto const sle =
            env_.current()->read(keylet::cftoken(acct.id(), cftID_.key)))
        return sle->getFieldU64(sfCFTAmount);
    return 0;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
