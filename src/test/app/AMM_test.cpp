//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <boost/regex.hpp>
#include <test/jtx.h>
#include <test/jtx/AMM.h>

#include <chrono>
#include <utility>

namespace ripple {
namespace test {

template <typename E>
Json::Value
rpc(E& env, std::string const& command, Json::Value const& v)
{
    return env.rpc("json", command, to_string(v));
}

using idmap_t = std::map<std::string, std::string>;

/** Wrapper class. Maintains a map of account id -> name.
 * The map is used to output a user-friendly account name
 * instead of the hash.
 */
class AccountX : public jtx::Account
{
    static inline idmap_t idmap_;

public:
    AccountX(std::string const& name) : jtx::Account(name)
    {
        idmap_[to_string(id())] = name;
    }
    idmap_t const
    idmap() const
    {
        return idmap_;
    }
};

/** Map accound id to name.
 */
std::string
domap(std::string const& s, std::optional<idmap_t> const& idmap)
{
    if (!idmap)
        return s;
    std::string str = s;
    for (auto [id, name] : *idmap)
    {
        boost::regex re(id.c_str());
        str = boost::regex_replace(str, re, name);
    }
    return str;
}

std::vector<std::pair<STAmount, STAmount>>
offersFromJson(Json::Value const& j)
{
    if (!j.isMember("result") || !j["result"].isMember("status") ||
        j["result"]["status"].asString() != "success" ||
        !j["result"].isMember("offers") || !j["result"]["offers"].isArray())
        return {};
    Json::Value offers = j["result"]["offers"];
    std::vector<std::pair<STAmount, STAmount>> res{};
    for (auto it = offers.begin(); it != offers.end(); ++it)
    {
        STAmount gets;
        STAmount pays;
        if (!amountFromJsonNoThrow(gets, (*it)["taker_gets"]) ||
            !amountFromJsonNoThrow(pays, (*it)["taker_pays"]))
            return {};
        res.emplace_back(std::make_pair(gets, pays));
    }
    return res;
}

template <typename E>
Json::Value
readOffers(
    E& env,
    AccountID const& acct,
    std::optional<idmap_t> const& idmap = {})
{
    Json::Value jv;
    jv[jss::account] = to_string(acct);
    return rpc(env, "account_offers", jv);
}

template <typename E>
Json::Value
readOffers(E& env, AccountX const& acct)
{
    return readOffers(env, acct.id(), acct.idmap());
}

template <typename E>
Json::Value
readLines(
    E& env,
    AccountID const& acctId,
    std::string const& name,
    std::optional<idmap_t> const& idmap = {})
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    return rpc(env, "account_lines", jv);
}

template <typename E>
Json::Value
readLines(E& env, AccountX const& acct)
{
    return readLines(env, acct.id(), acct.name(), acct.idmap());
}

class Test : public beast::unit_test::suite
{
protected:
    AccountX const gw;
    AccountX const carol;
    AccountX const alice;
    AccountX const bob;
    jtx::IOU const USD;
    jtx::IOU const EUR;
    jtx::IOU const GBP;
    jtx::IOU const BTC;
    jtx::IOU const BAD;

public:
    Test()
        : gw("gateway")
        , carol("carol")
        , alice("alice")
        , bob("bob")
        , USD(gw["USD"])
        , EUR(gw["EUR"])
        , GBP(gw["GBP"])
        , BTC(gw["BTC"])
        , BAD(jtx::IOU(gw, badCurrency()))
    {
    }

protected:
    void
    fund(
        jtx::Env& env,
        jtx::Account const& gw,
        std::vector<jtx::Account> const& accounts,
        std::vector<STAmount> const& amts,
        bool fundXRP)
    {
        if (fundXRP)
            env.fund(jtx::XRP(30000), gw);
        for (auto const& account : accounts)
        {
            if (fundXRP)
                env.fund(jtx::XRP(30000), account);
            for (auto const& amt : amts)
            {
                env.trust(amt + amt, account);
                env(pay(gw, account, amt));
            }
        }
    }

    template <typename F>
    void
    proc(
        F&& cb,
        std::optional<std::pair<STAmount, STAmount>> const& pool = {},
        std::optional<IOUAmount> const& lpt = {},
        std::uint32_t fee = 0)
    {
        using namespace jtx;
        Env env{*this};

        auto [asset1, asset2] = [&]() -> std::pair<STAmount, STAmount> {
            if (pool)
                return *pool;
            return {XRP(10000), USD(10000)};
        }();

        fund(
            env,
            gw,
            {alice, carol},
            {STAmount{asset2.issue(), 30000, 0}},
            true);
        if (!asset1.native())
            fund(
                env,
                gw,
                {alice, carol},
                {STAmount{asset1.issue(), 30000, 0}},
                false);
        auto tokens = [&]() {
            if (lpt)
                return *lpt;
            return IOUAmount{10000000, 0};
        }();
        AMM ammAlice(env, alice, asset1, asset2, false, 50, fee);
        BEAST_EXPECT(ammAlice.expectBalances(asset1, asset2, tokens));
        cb(ammAlice, env);
    }

    template <typename C>
    void
    stats(C const& t, std::string const& msg)
    {
        auto const sum = std::accumulate(t.begin(), t.end(), 0.0);
        auto const avg = sum / static_cast<double>(t.size());
        auto sd = std::accumulate(
            t.begin(), t.end(), 0.0, [&](auto const init, auto const r) {
                return init + pow((r - avg), 2);
            });
        sd = sqrt(sd / t.size());
        std::cout << msg << " exec time: avg " << avg << " "
                  << " sd " << sd << std::endl;
    }
};

struct AMM_test : public Test
{
public:
    AMM_test() : Test()
    {
    }

private:
    void
    testInstanceCreate()
    {
        testcase("Instance Create");

        using namespace jtx;

        // XRP to IOU
        proc([&](AMM& ammAlice, Env&) {
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // IOU to IOU
        proc(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                    USD(20000), BTC(0.5), IOUAmount{100, 0}));
            },
            std::make_pair(USD(20000), BTC(0.5)),
            IOUAmount{100, 0});

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(25000), BTC(0.625)}, true);
            env(rate(gw, 1.25));
            AMM ammAlice(env, alice, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            // Charging the AMM's LP the transfer fee.
            env.require(balance(alice, USD(0)));
            env.require(balance(alice, BTC(0)));
        }
    }

    void
    testInvalidInstance()
    {
        testcase("Invalid Instance");

        using namespace jtx;

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, true);
            // Can't have both XRP tokens
            AMM ammAlice(env, alice, XRP(10000), XRP(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, true);
            // Can't have both tokens the same IOU
            AMM ammAlice(env, alice, USD(10000), USD(10000), ter(temBAD_AMM));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, true);
            // Can't have zero amounts
            AMM ammAlice(env, alice, XRP(0), USD(10000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, true);
            // Bad currency
            AMM ammAlice(
                env, alice, XRP(10000), BAD(10000), ter(temBAD_CURRENCY));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, true);
            // Insufficient IOU balance
            AMM ammAlice(
                env, alice, XRP(10000), USD(40000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, true);
            // Insufficient XRP balance
            AMM ammAlice(
                env, alice, XRP(40000), USD(10000), ter(tecUNFUNDED_PAYMENT));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, true);
            // Invalid trading fee
            AMM ammAlice(
                env,
                alice,
                XRP(10000),
                USD(10000),
                false,
                50,
                70001,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.accountRootExists());
        }

        proc([&](AMM& ammAlice, Env& env) {
            AMM ammCarol(
                env, carol, XRP(10000), USD(10000), ter(tecAMM_EXISTS));
        });
    }

    void
    testDeposit()
    {
        testcase("Deposit");

        using namespace jtx;

        // Equal deposit: 1000000 tokens, 10% of the current pool
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Equal limit deposit: deposit USD100 and XRP proportionally
        // to the pool composition not to exceed 100XRP. If the amount
        // exceeds 100XRP then deposit 100XRP and USD proportionally
        // to the pool composition not to exceed 100USD. Fail if exceeded.
        // Deposit 100USD/100XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(100), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // Equal limit deposit. Deposit 100USD/100XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(200), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // TODO. Equal limit deposit. Constraint fails.

        // Single deposit: 1000 USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit: 1000 XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(10000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit: 100000 tokens worth of USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10201), IOUAmount{10100000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10201), USD(10000), IOUAmount{10100000, 0}));
        });

        // Single deposit with SP not exceeding specified:
        // 100USD with EP not to exceed 0.1 (AssetIn/TokensOut)
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(1000), std::nullopt, STAmount{USD.issue(), 1, -1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{1048808848170152, -8}));
        });

        // Single deposit with SP not exceeding specified:
        // 100USD with EP not to exceed 0.1 (AssetIn/TokensOut)
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(0), std::nullopt, STAmount{USD.issue(), 10, 0});
            std::cout << ammAlice.ammRpcInfo()->toStyledString();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(11000), IOUAmount{1048808848170152, -8}));
        });
    }

    void
    testWithdraw()
    {
        testcase("Withdraw");

        using namespace jtx;

        // Should fail - Carol is not a Liquidity Provider.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                carol, 10000, std::nullopt, std::optional<ter>(tecAMM_BALANCE));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Should fail - Carol withdraws more than deposited
        proc([&](AMM& ammAlice, Env&) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));

            ammAlice.withdraw(
                carol,
                2000000,
                std::nullopt,
                std::optional<ter>(tecAMM_INVALID_TOKENS));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Equal withdraw by Carol: 1000000 of tokens, 10% of the current pool
        proc([&](AMM& ammAlice, Env&) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(1000), USD(1000), IOUAmount{1000000, 0}, carol));

            // Carol withdraws all tokens
            ammAlice.withdraw(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(0), USD(0), IOUAmount{0, 0}, carol));
        });

        // Equal withdraw by tokens 1000000, 10%
        // of the current pool
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(9000), IOUAmount{9000000, 0}));
        });

        // Equal withdraw with a limit. Withdraw XRP200.
        // If proportional withdraw of USD is less than 100
        // the withdraw that amount, otherwise withdraw USD100
        // and proportionally withdraw XRP. It's the latter
        // in this case - XRP100/USD100.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(200), USD(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Equal withdraw with a limit. XRP100/USD100.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(100), USD(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Single withdraw by amount XRP1000
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(10000), IOUAmount{948683298050514, -8}));
        });

        // Single withdraw by tokens 10000.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 10000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9980.01), IOUAmount{9990000, 0}));
        });

#if 0  // specs in works
       // Single withdraw maxSP limit. SP after the trade is 1111111.111,
       // less than 1200000.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice, USD(1000), std::nullopt, XRPAmount{1200000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9000), IOUAmount{948683298050513, -8}));
        });

        // Single withdraw maxSP limit. SP after the trade is 1111111.111,
        // greater than 1100000, the withdrawl amount is changed to ~USD488.088
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice, USD(1000), std::nullopt, XRPAmount{1100000});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD.issue(), static_cast<std::uint64_t>(95119115182985), -10},
                IOUAmount{9752902910568, -6}));
        });

#endif

        // Withdraw all tokens. 0 is a special case to withdraw all tokens.
        proc([&](AMM& ammAlice, Env& env) {
            ammAlice.withdraw(alice, 0);
            BEAST_EXPECT(
                ammAlice.expectBalances(XRP(0), USD(0), IOUAmount{0, 0}));

            // Can create AMM for the XRP/USD pair
            AMM ammCarol(env, carol, XRP(10000), USD(10000));
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Single deposit 1000USD, withdraw all tokens in USD
        proc([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, 0, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(0), USD(0), IOUAmount{0, 0}, carol));
        });

        // Single deposit 1000USD, withdraw all tokens in XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, 0, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
        });

        // Single deposit/withdraw 1000USD
        // TODO There is a round-off error. The number of
        // tokens to withdraw exceeds the LP tokens balance.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(10000));
            ammAlice.withdraw(
                carol,
                USD(10000),
                std::nullopt,
                std::nullopt,
                std::optional<ter>(tecAMM_INVALID_TOKENS));
        });

        // Single deposit/withdraw 1000USD
        // TODO There is a round-off error. There remains
        // a dust amount of tokens
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(0), STAmount{USD, 63, -13}, IOUAmount{63, -10}, carol));
        });

        // Single deposit by different accounts and then withdraw
        // in reverse must result in all balances back to the original
        // state.
        // TODO There is a round-off error. There remains
        // a dust amount of tokens.
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.deposit(alice, USD(1000));
            ammAlice.withdraw(alice, USD(1000));
            ammAlice.withdraw(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(0), STAmount{USD, 63, -13}, IOUAmount{63, -10}, carol));
        });

        // Equal deposit 10%, withdraw all tokens
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in USD
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000),
                STAmount{
                    USD.issue(),
                    static_cast<std::uint64_t>(90909090909091),
                    -10},
                IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in XRP
        proc([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, 0, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
        });

        // TODO there should be a limit on a single withdrawal amount.
        // For instance, in 10000USD and 10000XRP amm with all liquidity
        // provided by one LP, LP can not withdraw all tokens in USD.
        // Withdrawing 90% in USD is also invalid. Besides the impact
        // on the pool there should be a max threshold for single
        // deposit.
    }

    void
    testRequireAuth()
    {
        testcase("Require Authorization");
        using namespace jtx;

        Env env{*this};
        auto const aliceUSD = alice["USD"];
        env.fund(XRP(20000), alice, gw);
        env(fset(gw, asfRequireAuth));
        env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
        env(trust(alice, USD(10000)));
        env(pay(gw, alice, USD(10000)));
        AMM ammAlice(env, alice, XRP(10000), USD(10000));
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10000), USD(10000), IOUAmount{10000000, 0}, alice));
    }

    void
    testPayment()
    {
        testcase("Payment");
        using namespace jtx;
        // one path XRP/USD
        proc([&](AMM& ammAlice, Env& env) {
            env(pay(carol, alice, USD(100)),
                // path(~USD),
                sendmax(XRP(200)),
                txflags(tfPartialPayment));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10101010101},
                USD(9900),
                IOUAmount{10000000, 0},
                alice));
        });
        // two paths XRP/USD, AMM is not used
        proc([&](AMM& ammAlice, Env& env) {
            env.fund(jtx::XRP(30000), bob);
            fund(env, gw, {bob}, {USD(200), GBP(200)}, false);
            env(offer(bob, XRP(90), GBP(100)));
            env(offer(bob, GBP(100), USD(100)));
            env(pay(carol, alice, USD(100)),
                path(~USD),
                path(~GBP, ~USD),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(offersFromJson(readOffers(env, bob)).size() == 0);
        });

        // Multiple AMM with the last limiting step. This results
        // in a partial payment.
        proc([&](AMM& ammAlice, Env& env) {
            env.fund(jtx::XRP(30000), bob);
            fund(env, gw, {bob, carol}, {EUR(20000), GBP(20000)}, false);
            env(offer(bob, EUR(45), GBP(30)));
            AMM ammBob(env, bob, XRP(10000), GBP(7000));
            env(pay(carol, alice, USD(50)),
                path(~GBP, ~XRP, ~USD),
                sendmax(EUR(40)),
                txflags(
                    tfPartialPayment |
                    tfNoRippleDirect));  // force no default path
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10037950664},
                STAmount{
                    USD.issue(),
                    static_cast<std::uint64_t>(996219281677076),
                    -11},
                IOUAmount{10000000, 0}));
            BEAST_EXPECT(ammBob.expectBalances(
                XRPAmount{9962049336},
                STAmount{
                    GBP.issue(),
                    static_cast<std::uint64_t>(7026666666666666),
                    -12},
                IOUAmount{8366600265340755, -9}));
            auto const res = offersFromJson(readOffers(env, bob));
            BEAST_EXPECT(
                res.size() == 1 &&
                res[0].first ==
                    STAmount(
                        GBP.issue(),
                        static_cast<std::uint64_t>(333333333333333),
                        -14) &&
                res[0].second == STAmount(EUR.issue(), 5, 0));
        });

        // Offer and AMM. AMM has a better quality up to around 2.5XRP/2.5USD.
        // AMM offer is consumed first. Then the offer is partially
        // consumed.
        {
            Env env(*this);
            fund(env, gw, {alice, carol, bob}, {USD(2000)}, true);
            AMM ammAlice(env, alice, XRP(1000), USD(1005));
            env(offer(bob, XRP(10), USD(10)), txflags(tfPassive));
            env(pay(carol, alice, USD(10)),
                sendmax(XRP(10)),
                txflags(tfPartialPayment));
            auto const offers = offersFromJson(readOffers(env, bob));
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount{1002496882},
                    STAmount{
                        USD.issue(),
                        static_cast<std::uint64_t>(1002496883576343),
                        -12},
                    IOUAmount{1002496882788171, -9}) &&
                offers[0].first ==
                    STAmount(
                        USD.issue(),
                        static_cast<std::uint64_t>(250311642365788),
                        -14) &&
                offers[0].second == XRPAmount{2503116});
        }

        // Offer crossing
        proc(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                fund(env, gw, {bob}, {USD(200)}, false);
                env(offer(bob, XRP(100), USD(100)));
                BEAST_EXPECT(
                    ammAlice.expectBalances(
                        XRP(10000),
                        USD(10100),
                        IOUAmount{1004987562112089, -8}) &&
                    offersFromJson(readOffers(env, bob)).size() == 0);
            },
            std::make_pair(XRP(10100), USD(10000)),
            IOUAmount{1004987562112089, -8});
    }

    void
    testAmendment()
    {
        testcase("Amendment");
    }

    void
    testFees()
    {
        testcase("Fees");
    }

    void
    run() override
    {
        testInvalidInstance();
        testInstanceCreate();
        testDeposit();
        testWithdraw();
        testRequireAuth();
        testPayment();
    }
};

struct AMM_manual_test : public Test
{
    void
    testFibonnaciPerf()
    {
        testcase("Performance Fibonnaci");
        using namespace std::chrono;
        auto const start = high_resolution_clock::now();

        auto const fee = Number(1) / 100;
        auto const c1_fee = 1 - fee;
        Number poolPays = 1000000;
        Number poolGets = 1000000;
        auto SP = poolPays / (poolGets * c1_fee);
        auto ftakerPays = (Number(5) / 10000) * poolGets / 2;
        auto ftakerGets = SP * ftakerPays;
        poolGets += ftakerPays;
        poolPays -= ftakerGets;
        auto product = poolPays * poolGets;
        Number x(0);
        Number y = ftakerGets;
        Number ftotal(0);
        for (int i = 0; i < 100; ++i)
        {
            ftotal = x + y;
            ftakerGets = ftotal;
            auto ftakerPaysPrime = product / (poolPays - ftakerGets) - poolGets;
            ftakerPays = ftakerPaysPrime / c1_fee;
            poolGets += ftakerPays;
            poolPays -= ftakerGets;
            x = y;
            y = ftotal;
            product = poolPays * poolGets;
        }
        auto const elapsed = high_resolution_clock::now() - start;

        std::cout << "100 fibonnaci "
                  << duration_cast<std::chrono::microseconds>(elapsed).count()
                  << std::endl;
        BEAST_EXPECT(true);
    }

    void
    testOffersPerf()
    {
        testcase("Performance Offers");

        auto const N = 10;
        std::array<std::uint64_t, N> t;

        for (auto i = 0; i < N; i++)
        {
            using namespace jtx;
            Env env(*this);

            env.fund(XRP(1000), alice, carol, bob, gw);
            env.trust(USD(1000), carol);
            env.trust(EUR(1000), alice);
            env.trust(USD(1000), bob);

            env(pay(gw, alice, EUR(1000)));
            env(pay(gw, bob, USD(1000)));

            env(offer(bob, EUR(1000), USD(1000)));

            auto start = std::chrono::high_resolution_clock::now();
            env(pay(alice, carol, USD(1000)), path(~USD), sendmax(EUR(1000)));
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            std::uint64_t microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
                    .count();
            t[i] = microseconds;
        }
        stats(t, "single offer");

        for (auto i = 0; i < N; i++)
        {
            using namespace jtx;
            Env env(*this);

            env.fund(XRP(1000), alice, carol, bob, gw);
            env.trust(USD(1000), carol);
            env.trust(EUR(1100), alice);
            env.trust(USD(1000), bob);

            env(pay(gw, alice, EUR(1100)));
            env(pay(gw, bob, USD(1000)));

            for (auto j = 0; j < 10; j++)
                env(offer(bob, EUR(100 + j), USD(100)));

            auto start = std::chrono::high_resolution_clock::now();
            env(pay(alice, carol, USD(1000)), path(~USD), sendmax(EUR(1100)));
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            std::uint64_t microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
                    .count();
            t[i] = microseconds;
        }
        stats(t, "multiple offers");
    }

    void
    testPaymentPerf()
    {
        testcase("Payment Performance");
        using namespace jtx;
        using namespace std::chrono;

        auto constexpr N = 10;
        auto constexpr Ntests = 8;

        std::array<std::uint64_t, N> t[Ntests];
        for (int i = 0; i < N; ++i)
        {
            std::uint16_t Nt = 0;
            // one path XRP/USD
            proc([&](AMM& ammAlice, Env& env) {
                auto const start = high_resolution_clock::now();
                env(pay(carol, alice, USD(100)),
                    // path(~USD),
                    sendmax(XRP(200)),
                    txflags(tfPartialPayment));
                auto const elapsed = high_resolution_clock::now() - start;
                t[Nt++][i] =
                    duration_cast<std::chrono::microseconds>(elapsed).count();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{10101010101},
                    USD(9900),
                    IOUAmount{10000000, 0},
                    alice));
            });
            // one path IOU/IOU
            proc(
                [&](AMM& ammAlice, Env& env) {
                    auto const start = high_resolution_clock::now();
                    env(pay(carol, alice, USD(100)),
                        path(~USD),
                        sendmax(EUR(200)),
                        txflags(tfPartialPayment));
                    auto const elapsed = high_resolution_clock::now() - start;
                    t[Nt++][i] =
                        duration_cast<std::chrono::microseconds>(elapsed)
                            .count();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        STAmount{
                            EUR.issue(),
                            static_cast<std::uint64_t>(101010101010101),
                            -10},
                        USD(9900),
                        IOUAmount{10000, 0},
                        alice));
                },
                std::make_pair(USD(10000), EUR(10000)),
                IOUAmount{10000, 0});
            // One path with multiple AMM
            proc([&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(300000), bob);
                fund(
                    env,
                    gw,
                    {bob},
                    {USD(30000), EUR(30000), GBP(30000)},
                    false);
                AMM ammEUR_GBP(env, bob, EUR(10000), GBP(10000));
                AMM ammGBP_XRP(env, bob, GBP(10000), XRP(10000));
                fund(env, gw, {carol, alice}, {EUR(1000)}, false);
                auto const start = high_resolution_clock::now();
                env(pay(carol, alice, USD(100)),
                    path(~GBP, ~XRP, ~USD),
                    sendmax(EUR(100)),
                    txflags(tfPartialPayment | tfNoRippleDirect));
                auto const elapsed = high_resolution_clock::now() - start;
                t[Nt++][i] =
                    duration_cast<std::chrono::microseconds>(elapsed).count();
                BEAST_EXPECT(ammEUR_GBP.expectBalances(
                    EUR(10100),
                    STAmount{
                        GBP.issue(),
                        static_cast<std::uint64_t>(990099009900991),
                        -11},
                    IOUAmount{10000, 0}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{10098039215},
                    STAmount{
                        USD.issue(),
                        static_cast<std::uint64_t>(990291262203224),
                        -11},
                    IOUAmount{10000000, 0}));
            });
            {
                Env env{*this};
                env.fund(jtx::XRP(30000), alice, carol, gw);

                auto const start = high_resolution_clock::now();
                env(pay(carol, alice, XRP(100)));
                auto const elapsed = high_resolution_clock::now() - start;
                t[Nt++][i] =
                    duration_cast<std::chrono::microseconds>(elapsed).count();
            }
            {
                Env env{*this};
                env.fund(jtx::XRP(30000), alice, carol, gw);
                env.trust(USD(30000), alice);
                env.trust(USD(30000), carol);

                env(pay(gw, alice, USD(10000)));
                env(pay(gw, carol, USD(10000)));

                auto const start = high_resolution_clock::now();
                env(pay(carol, alice, USD(100)));
                auto const elapsed = high_resolution_clock::now() - start;
                t[Nt++][i] =
                    duration_cast<std::chrono::microseconds>(elapsed).count();
            }
            // Two paths, order book offer
            {
                Env env{*this};
                fund(env, gw, {alice, carol, bob}, {USD(200), GBP(200)}, true);
                env(offer(alice, XRP(10), GBP(10)));
                env(offer(alice, GBP(10), USD(1)));
                env(offer(carol, XRP(100), USD(100)));
                auto const start = high_resolution_clock::now();
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    path(~GBP, ~USD),
                    sendmax(XRP(100)),
                    txflags(tfPartialPayment));
                auto const elapsed = high_resolution_clock::now() - start;
                t[Nt++][i] =
                    duration_cast<std::chrono::microseconds>(elapsed).count();
            }
        }
        stats(t[0], "AMM XRP/IOU Payment");
        stats(t[1], "AMM IOU/IOU Payment");
        stats(t[2], "AMM IOU/IOU one path multiple AMM Payment");
        stats(t[3], "XRP Payment");
        stats(t[4], "IOU Payment");
        stats(t[5], "XRP/IOU Payment, order book");
    }

    void
    run() override
    {
        testFibonnaciPerf();
        testPaymentPerf();
    }
};

BEAST_DEFINE_TESTSUITE(AMM, app, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(AMM_manual, tx, ripple);

}  // namespace test
}  // namespace ripple