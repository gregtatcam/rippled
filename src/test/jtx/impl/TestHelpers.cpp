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

#include <test/jtx/TestHelpers.h>

#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {
namespace test {
namespace jtx {

// Functions used in debugging
Json::Value
getAccountOffers(Env& env, AccountID const& acct, bool current)
{
    Json::Value jv;
    jv[jss::account] = to_string(acct);
    return env.rpc("json", "account_offers", to_string(jv))[jss::result];
}

Json::Value
getAccountLines(Env& env, AccountID const& acctId)
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    return env.rpc("json", "account_lines", to_string(jv))[jss::result];
}

bool
checkArraySize(Json::Value const& val, unsigned int size)
{
    return val.isArray() && val.size() == size;
}

/* Path finding */
/******************************************************************************/
void
stpath_append_one(STPath& st, Account const& account)
{
    st.push_back(STPathElement(
        {account.id(),
         std::nullopt,
         std::nullopt,
         STPathElement::PathAssetTag{}}));
}

void
stpath_append_one(STPath& st, STPathElement const& pe)
{
    st.push_back(pe);
}

bool
equal(STAmount const& sa1, STAmount const& sa2)
{
    return sa1 == sa2 && sa1.issue().account == sa2.issue().account;
}

// Issue path element
STPathElement
IPE(Issue const& iss)
{
    return STPathElement(
        STPathElement::typeCurrency | STPathElement::typeIssuer,
        xrpAccount(),
        PathAsset{iss.currency},
        iss.account);
}
STPathElement
IPE(MPTIssue const& iss)
{
    return STPathElement(
        STPathElement::typeMPT | STPathElement::typeIssuer,
        xrpAccount(),
        PathAsset{iss.getMptID()},
        iss.getIssuer());
}

Json::Value
rpf(jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& dstAmount,
    std::optional<STAmount> const& sendMax,
    std::optional<Currency> const& srcCurrency)
{
    Json::Value jv = Json::objectValue;
    jv[jss::command] = "ripple_path_find";
    jv[jss::source_account] = toBase58(src);
    jv[jss::destination_account] = toBase58(dst);
    jv[jss::destination_amount] = dstAmount.getJson(JsonOptions::none);
    if (sendMax)
        jv[jss::send_max] = sendMax->getJson(JsonOptions::none);
    if (srcCurrency)
    {
        auto& sc = jv[jss::source_currencies] = Json::arrayValue;
        Json::Value j = Json::objectValue;
        j[jss::currency] = to_string(srcCurrency.value());
        sc.append(j);
    }

    return jv;
}

jtx::Env
pathTestEnv(beast::unit_test::suite& suite)
{
    // These tests were originally written with search parameters that are
    // different from the current defaults. This function creates an env
    // with the search parameters that the tests were written for.
    using namespace jtx;
    return Env(suite, envconfig([](std::unique_ptr<Config> cfg) {
                   cfg->PATH_SEARCH_OLD = 7;
                   cfg->PATH_SEARCH = 7;
                   cfg->PATH_SEARCH_MAX = 10;
                   return cfg;
               }));
}

Json::Value
find_paths_request(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<Currency> const& saSrcCurrency)
{
    using namespace jtx;

    auto& app = env.app();
    Resource::Charge loadType = Resource::feeReferenceRPC;
    Resource::Consumer c;

    RPC::JsonContext context{
        {env.journal,
         app,
         loadType,
         app.getOPs(),
         app.getLedgerMaster(),
         c,
         Role::USER,
         {},
         {},
         RPC::apiVersionIfUnspecified},
        {},
        {}};

    Json::Value params = Json::objectValue;
    params[jss::command] = "ripple_path_find";
    params[jss::source_account] = toBase58(src);
    params[jss::destination_account] = toBase58(dst);
    params[jss::destination_amount] = saDstAmount.getJson(JsonOptions::none);
    if (saSendMax)
        params[jss::send_max] = saSendMax->getJson(JsonOptions::none);
    if (saSrcCurrency)
    {
        auto& sc = params[jss::source_currencies] = Json::arrayValue;
        Json::Value j = Json::objectValue;
        j[jss::currency] = to_string(saSrcCurrency.value());
        sc.append(j);
    }

    Json::Value result;
    gate g;
    app.getJobQueue().postCoro(jtCLIENT, "RPC-Client", [&](auto const& coro) {
        context.params = std::move(params);
        context.coro = coro;
        RPC::doCommand(context, result);
        g.signal();
    });

    using namespace std::chrono_literals;
    using namespace beast::unit_test;
    g.wait_for(5s);
    return result;
}

std::tuple<STPathSet, STAmount, STAmount>
find_paths(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<Currency> const& saSrcCurrency)
{
    Json::Value result = find_paths_request(
        env, src, dst, saDstAmount, saSendMax, saSrcCurrency);
    if (result.isMember(jss::error))
        return std::make_tuple(STPathSet{}, STAmount{}, STAmount{});

    STAmount da;
    if (result.isMember(jss::destination_amount))
        da = amountFromJson(sfGeneric, result[jss::destination_amount]);

    STAmount sa;
    STPathSet paths;
    if (result.isMember(jss::alternatives))
    {
        auto const& alts = result[jss::alternatives];
        if (alts.size() > 0)
        {
            auto const& path = alts[0u];

            if (path.isMember(jss::source_amount))
                sa = amountFromJson(sfGeneric, path[jss::source_amount]);

            if (path.isMember(jss::destination_amount))
                da = amountFromJson(sfGeneric, path[jss::destination_amount]);

            if (path.isMember(jss::paths_computed))
            {
                Json::Value p;
                p["Paths"] = path[jss::paths_computed];
                STParsedJSONObject po("generic", p);
                paths = po.object->getFieldPathSet(sfPaths);
            }
        }
    }

    return std::make_tuple(std::move(paths), std::move(sa), std::move(da));
}

/******************************************************************************/

XRPAmount
txfee(Env const& env, std::uint16_t n)
{
    return env.current()->fees().base * n;
}

PrettyAmount
xrpMinusFee(Env const& env, std::int64_t xrpAmount)
{
    auto feeDrops = env.current()->fees().base;
    return drops(dropsPerXRP * xrpAmount - feeDrops);
};

[[nodiscard]] bool
expectLine(
    Env& env,
    AccountID const& account,
    STAmount const& value,
    bool defaultLimits)
{
    if (auto const sle = env.le(keylet::line(account, value.issue())))
    {
        Issue const issue = value.issue();
        bool const accountLow = account < issue.account;

        bool expectDefaultTrustLine = true;
        if (defaultLimits)
        {
            STAmount low{issue};
            STAmount high{issue};

            low.setIssuer(accountLow ? account : issue.account);
            high.setIssuer(accountLow ? issue.account : account);

            expectDefaultTrustLine = sle->getFieldAmount(sfLowLimit) == low &&
                sle->getFieldAmount(sfHighLimit) == high;
        }

        auto amount = sle->getFieldAmount(sfBalance);
        amount.setIssuer(value.issue().account);
        if (!accountLow)
            amount.negate();
        return amount == value && expectDefaultTrustLine;
    }
    return false;
}

[[nodiscard]] bool
expectLine(Env& env, AccountID const& account, None const& value)
{
    return !env.le(keylet::line(account, value.issue));
}

[[nodiscard]] bool
expectOffers(
    Env& env,
    AccountID const& account,
    std::uint16_t size,
    std::vector<Amounts> const& toMatch)
{
    std::uint16_t cnt = 0;
    std::uint16_t matched = 0;
    forEachItem(
        *env.current(), account, [&](std::shared_ptr<SLE const> const& sle) {
            if (!sle)
                return false;
            if (sle->getType() == ltOFFER)
            {
                ++cnt;
                if (std::find_if(
                        toMatch.begin(), toMatch.end(), [&](auto const& a) {
                            return a.in == sle->getFieldAmount(sfTakerPays) &&
                                a.out == sle->getFieldAmount(sfTakerGets);
                        }) != toMatch.end())
                    ++matched;
            }
            return true;
        });
    return size == cnt && matched == toMatch.size();
}

Json::Value
ledgerEntryRoot(Env& env, Account const& acct)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::account_root] = acct.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

Json::Value
ledgerEntryState(
    Env& env,
    Account const& acct_a,
    Account const& acct_b,
    std::string const& currency)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::ripple_state][jss::currency] = currency;
    jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
    jvParams[jss::ripple_state][jss::accounts].append(acct_a.human());
    jvParams[jss::ripple_state][jss::accounts].append(acct_b.human());
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

Json::Value
accountBalance(Env& env, Account const& acct)
{
    auto const jrr = ledgerEntryRoot(env, acct);
    return jrr[jss::node][sfBalance.fieldName];
}

[[nodiscard]] bool
expectLedgerEntryRoot(
    Env& env,
    Account const& acct,
    STAmount const& expectedValue)
{
    return accountBalance(env, acct) == to_string(expectedValue.xrp());
}

/* Escrow */
/******************************************************************************/

Json::Value
escrow(AccountID const& account, AccountID const& to, STAmount const& amount)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowCreate;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    return jv;
}

Json::Value
finish(AccountID const& account, AccountID const& from, std::uint32_t seq)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowFinish;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[sfOwner.jsonName] = to_string(from);
    jv[sfOfferSequence.jsonName] = seq;
    return jv;
}

Json::Value
cancel(AccountID const& account, Account const& from, std::uint32_t seq)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::EscrowCancel;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[sfOwner.jsonName] = from.human();
    jv[sfOfferSequence.jsonName] = seq;
    return jv;
}

/* Payment Channel */
/******************************************************************************/
Json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter,
    std::optional<std::uint32_t> const& dstTag)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelCreate;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv[jss::SettleDelay] = settleDelay.count();
    jv[sfPublicKey.fieldName] = strHex(pk.slice());
    if (cancelAfter)
        jv[sfCancelAfter.fieldName] = cancelAfter->time_since_epoch().count();
    if (dstTag)
        jv[sfDestinationTag.fieldName] = *dstTag;
    return jv;
}

Json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelFund;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv[sfChannel.fieldName] = to_string(channel);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    if (expiration)
        jv[sfExpiration.fieldName] = expiration->time_since_epoch().count();
    return jv;
}

Json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance,
    std::optional<STAmount> const& amount,
    std::optional<Slice> const& signature,
    std::optional<PublicKey> const& pk)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelClaim;
    jv[jss::Flags] = tfUniversal;
    jv[jss::Account] = to_string(account);
    jv["Channel"] = to_string(channel);
    if (amount)
        jv[jss::Amount] = amount->getJson(JsonOptions::none);
    if (balance)
        jv["Balance"] = balance->getJson(JsonOptions::none);
    if (signature)
        jv["Signature"] = strHex(*signature);
    if (pk)
        jv["PublicKey"] = strHex(pk->slice());
    return jv;
}

uint256
channel(
    AccountID const& account,
    AccountID const& dst,
    std::uint32_t seqProxyValue)
{
    auto const k = keylet::payChan(account, dst, seqProxyValue);
    return k.key;
}

STAmount
channelBalance(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    if (!slep)
        return XRPAmount{-1};
    return (*slep)[sfBalance];
}

bool
channelExists(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    return bool(slep);
}

/* Crossing Limits */
/******************************************************************************/

void
n_offers(
    Env& env,
    std::size_t n,
    Account const& account,
    STAmount const& in,
    STAmount const& out)
{
    auto const ownerCount = env.le(account)->getFieldU32(sfOwnerCount);
    for (std::size_t i = 0; i < n; i++)
    {
        env(offer(account, in, out));
        env.close();
    }
    env.require(owners(account, ownerCount + n));
}

/* Pay Strand */
/***************************************************************/

// Currency path element
STPathElement
cpe(Currency const& c)
{
    return STPathElement(
        STPathElement::typeCurrency, xrpAccount(), c, xrpAccount());
};

// All path element
STPathElement
allpe(AccountID const& a, Issue const& iss)
{
    return STPathElement(
        STPathElement::typeAccount | STPathElement::typeCurrency |
            STPathElement::typeIssuer,
        a,
        iss.currency,
        iss.account);
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple