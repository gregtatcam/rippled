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
#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/app/paths/AMMContext.h>
#include <ripple/app/paths/AMMOffer.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/amount.h>
#include <test/jtx/sendmax.h>

#include <chrono>
#include <utility>
#include <vector>

namespace ripple {
namespace test {

struct AMM_test : public jtx::AMMTest
{
private:
    void
    testInstanceCreate()
    {
        testcase("Instance Create");

        using namespace jtx;

        // XRP to IOU
        testAMM([&](AMM& ammAlice, Env&) {
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), USD(10'000), IOUAmount{10'000'000, 0}));
        });

        // IOU to IOU
        testAMM(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(20'000), BTC(0.5), IOUAmount{100, 0}));
            },
            {{USD(20'000), BTC(0.5)}});

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000), BTC(0.5)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            // no transfer fee on create
            AMM ammAlice(env, alice, USD(20'000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20'000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
        }

        // Require authorization is set, account is authorized
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(USD(30'000), alice);
            env.close();
            env(trust(gw, alice["USD"](30'000)), txflags(tfSetfAuth));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10'000), USD(10'000));
        }

        // Cleared global freeze
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            env.trust(USD(30'000), alice);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            AMM ammAliceFail(
                env, alice, XRP(10'000), USD(10'000), ter(tecFROZEN));
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            AMM ammAlice(env, alice, XRP(10'000), USD(10'000));
        }

        // Trading fee
        testAMM(
            [&](AMM& amm, Env&) {
                BEAST_EXPECT(amm.expectTradingFee(1'000));
                BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{0}));
            },
            std::nullopt,
            1'000);
    }

    void
    testInvalidInstance()
    {
        testcase("Invalid Instance");

        using namespace jtx;

        // Can't have both XRP tokens
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10'000), XRP(10'000), ter(temBAD_AMM_TOKENS));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Can't have both tokens the same IOU
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(
                env, alice, USD(10'000), USD(10'000), ter(temBAD_AMM_TOKENS));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Can't have zero or negative amounts
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(env, alice, XRP(0), USD(10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
            AMM ammAlice1(env, alice, XRP(10'000), USD(0), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice1.ammExists());
            AMM ammAlice2(
                env, alice, XRP(10'000), USD(-10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice2.ammExists());
            AMM ammAlice3(
                env, alice, XRP(-10'000), USD(10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice3.ammExists());
        }

        // Bad currency
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10'000), BAD(10'000), ter(temBAD_CURRENCY));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient IOU balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10'000), USD(40'000), ter(tecUNFUNDED_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient XRP balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(40'000), USD(10'000), ter(tecUNFUNDED_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Invalid trading fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(
                env,
                alice,
                XRP(10'000),
                USD(10'000),
                false,
                65'001,
                10,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // AMM already exists
        testAMM([&](AMM& ammAlice, Env& env) {
            AMM ammCarol(
                env, carol, XRP(10'000), USD(10'000), ter(tecDUPLICATE));
        });

        // Invalid flags
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            AMM ammAlice(
                env,
                alice,
                XRP(10'000),
                USD(10'000),
                false,
                0,
                10,
                tfWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Invalid Account
        {
            Env env{*this};
            Account bad("bad");
            env.memoize(bad);
            AMM ammAlice(
                env,
                bad,
                XRP(10'000),
                USD(10'000),
                false,
                0,
                10,
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Require authorization is set
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, alice["USD"](30'000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10'000), USD(10'000), ter(tecNO_AUTH));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Globally frozen
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env(trust(gw, alice["USD"](30'000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10'000), USD(10'000), ter(tecFROZEN));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Individually frozen
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            env(trust(gw, alice["USD"](30'000)));
            env.close();
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            AMM ammAlice(env, alice, XRP(10'000), USD(10'000), ter(tecFROZEN));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient reserve, XRP/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                XRP(1'000) + reserve(env, 3) + env.current()->fees().base * 4;
            env.fund(starting_xrp, gw);
            env.fund(starting_xrp, alice);
            env.trust(USD(2'000), alice);
            env.close();
            env(pay(gw, alice, USD(2'000)));
            env.close();
            env(offer(alice, XRP(101), USD(100)));
            env(offer(alice, XRP(102), USD(100)));
            AMM ammAlice(
                env, alice, XRP(1'000), USD(1'000), ter(tecUNFUNDED_AMM));
        }

        // Insufficient reserve, IOU/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 5;
            env.fund(starting_xrp, gw);
            env.fund(starting_xrp, alice);
            env.trust(USD(2'000), alice);
            env.trust(EUR(2'000), alice);
            env.close();
            env(pay(gw, alice, USD(2'000)));
            env(pay(gw, alice, EUR(2'000)));
            env.close();
            env(offer(alice, EUR(101), USD(100)));
            env(offer(alice, EUR(102), USD(100)));
            AMM ammAlice(
                env, alice, EUR(1'000), USD(1'000), ter(tecINSUF_RESERVE_LINE));
        }

        // Insufficient fee
        {
            Env env(*this);
            fund(env, gw, {alice}, XRP(2'000), {USD(2'000), EUR(2'000)});
            AMM ammAlice(
                env,
                alice,
                EUR(1'000),
                USD(1'000),
                false,
                0,
                ammCrtFee(env).drops() - 1,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(telINSUF_FEE_P));
        }

        // AMM with LPTokens

        // AMM with one LPToken from another AMM.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10'000)}, Fund::IOUOnly);
            AMM ammAMMToken(
                env,
                alice,
                EUR(10'000),
                STAmount{ammAlice.lptIssue(), 1'000'000},
                ter(tecAMM_INVALID_TOKENS));
            AMM ammAMMToken1(
                env,
                alice,
                STAmount{ammAlice.lptIssue(), 1'000'000},
                EUR(10'000),
                ter(tecAMM_INVALID_TOKENS));
        });

        // AMM with two LPTokens from other AMMs.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10'000)}, Fund::IOUOnly);
            AMM ammAlice1(env, alice, XRP(10'000), EUR(10'000));
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env,
                alice,
                STAmount{token1, 1'000'000},
                STAmount{token2, 1'000'000},
                ter(tecAMM_INVALID_TOKENS));
        });

        // Issuer has DefaultRipple disabled
        {
            Env env(*this);
            env.fund(XRP(30'000), gw);
            env(fclear(gw, asfDefaultRipple));
            AMM ammGw(env, gw, XRP(10'000), USD(10'000), ter(terNO_RIPPLE));
            env.fund(XRP(30'000), alice);
            env.trust(USD(30'000), alice);
            env(pay(gw, alice, USD(30'000)));
            AMM ammAlice(
                env, alice, XRP(10'000), USD(10'000), ter(terNO_RIPPLE));
            Account const gw1("gw1");
            env.fund(XRP(30'000), gw1);
            env(fclear(gw1, asfDefaultRipple));
            env.trust(USD(30'000), gw1);
            env(pay(gw, gw1, USD(30'000)));
            auto const USD1 = gw1["USD"];
            AMM ammGwGw1(env, gw, USD(10'000), USD1(10'000), ter(terNO_RIPPLE));
            env.trust(USD1(30'000), alice);
            env(pay(gw1, alice, USD1(30'000)));
            AMM ammAlice1(
                env, alice, USD(10'000), USD1(10'000), ter(terNO_RIPPLE));
        }

        // Issuer has clawback enabled
        {
            Env env(*this);
            env.fund(XRP(1'000), gw);
            env(fset(gw, asfAllowTrustLineClawback));
            fund(env, gw, {alice}, XRP(1'000), {USD(1'000)}, Fund::Acct);
            env.close();
            AMM amm(env, gw, XRP(100), USD(100), ter(tecNO_PERMISSION));
            AMM amm1(env, alice, USD(100), XRP(100), ter(tecNO_PERMISSION));
            env(fclear(gw, asfAllowTrustLineClawback));
            env.close();
            // Can't be cleared
            AMM amm2(env, gw, XRP(100), USD(100), ter(tecNO_PERMISSION));
        }
    }

    void
    testInvalidDeposit()
    {
        testcase("Invalid Deposit");

        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            // Invalid flags
            ammAlice.deposit(
                alice,
                1'000'000,
                std::nullopt,
                tfWithdrawAll,
                ter(temINVALID_FLAG));

            // Invalid options
            std::vector<std::tuple<
                std::optional<std::uint32_t>,
                std::optional<std::uint32_t>,
                std::optional<STAmount>,
                std::optional<STAmount>,
                std::optional<STAmount>,
                std::optional<std::uint16_t>>>
                invalidOptions = {
                    // flags, tokens, asset1In, asset2in, EPrice, tfee
                    {tfLPToken,
                     1'000,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     std::nullopt},
                    {tfLPToken,
                     1'000,
                     XRP(100),
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfLPToken,
                     1'000,
                     std::nullopt,
                     std::nullopt,
                     STAmount{USD, 1, -1},
                     std::nullopt},
                    {tfLPToken,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     STAmount{USD, 1, -1},
                     std::nullopt},
                    {tfLPToken,
                     1'000,
                     XRP(100),
                     std::nullopt,
                     STAmount{USD, 1, -1},
                     std::nullopt},
                    {tfLPToken,
                     1'000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     1'000},
                    {tfSingleAsset,
                     1'000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfSingleAsset,
                     std::nullopt,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     std::nullopt},
                    {tfSingleAsset,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     STAmount{USD, 1, -1},
                     std::nullopt},
                    {tfSingleAsset,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     1'000},
                    {tfTwoAsset,
                     1'000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfTwoAsset,
                     std::nullopt,
                     XRP(100),
                     USD(100),
                     STAmount{USD, 1, -1},
                     std::nullopt},
                    {tfTwoAsset,
                     std::nullopt,
                     XRP(100),
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfTwoAsset,
                     std::nullopt,
                     XRP(100),
                     USD(100),
                     std::nullopt,
                     1'000},
                    {tfTwoAsset,
                     std::nullopt,
                     std::nullopt,
                     USD(100),
                     STAmount{USD, 1, -1},
                     std::nullopt},
                    {tfOneAssetLPToken,
                     1'000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfOneAssetLPToken,
                     std::nullopt,
                     XRP(100),
                     USD(100),
                     std::nullopt,
                     std::nullopt},
                    {tfOneAssetLPToken,
                     std::nullopt,
                     XRP(100),
                     std::nullopt,
                     STAmount{USD, 1, -1},
                     std::nullopt},
                    {tfOneAssetLPToken,
                     1'000,
                     XRP(100),
                     std::nullopt,
                     std::nullopt,
                     1'000},
                    {tfLimitLPToken,
                     1'000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfLimitLPToken,
                     1'000,
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfLimitLPToken,
                     std::nullopt,
                     USD(100),
                     XRP(100),
                     std::nullopt,
                     std::nullopt},
                    {tfLimitLPToken,
                     std::nullopt,
                     XRP(100),
                     std::nullopt,
                     STAmount{USD, 1, -1},
                     1'000},
                    {tfTwoAssetIfEmpty,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     1'000},
                    {tfTwoAssetIfEmpty,
                     1'000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt},
                    {tfTwoAssetIfEmpty,
                     std::nullopt,
                     XRP(100),
                     USD(100),
                     STAmount{USD, 1, -1},
                     std::nullopt},
                };
            for (auto const& it : invalidOptions)
            {
                ammAlice.deposit(
                    alice,
                    std::get<1>(it),
                    std::get<2>(it),
                    std::get<3>(it),
                    std::get<4>(it),
                    std::get<0>(it),
                    std::nullopt,
                    std::nullopt,
                    std::get<5>(it),
                    ter(temMALFORMED));
            }

            // Invalid tokens
            ammAlice.deposit(
                alice, 0, std::nullopt, std::nullopt, ter(temBAD_AMM_TOKENS));
            ammAlice.deposit(
                alice,
                IOUAmount{-1},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Invalid tokens - bogus currency
            {
                auto const iss1 = Issue{Currency(0xabc), gw.id()};
                auto const iss2 = Issue{Currency(0xdef), gw.id()};
                ammAlice.deposit(
                    alice,
                    1'000,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    {{iss1, iss2}},
                    std::nullopt,
                    std::nullopt,
                    ter(terNO_AMM));
            }

            // Depositing mismatched token, invalid Asset1In.issue
            ammAlice.deposit(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Depositing mismatched token, invalid Asset2In.issue
            ammAlice.deposit(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Depositing mismatched token, Asset1In.issue == Asset2In.issue
            ammAlice.deposit(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Invalid amount value
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.deposit(
                alice,
                USD(-1'000),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.deposit(
                alice,
                USD(10),
                std::nullopt,
                USD(-1),
                std::nullopt,
                ter(temBAD_AMOUNT));

            // Bad currency
            ammAlice.deposit(
                alice,
                BAD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_CURRENCY));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.deposit(
                bad,
                1'000'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));

            // Invalid AMM
            ammAlice.deposit(
                alice,
                1'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                std::nullopt,
                std::nullopt,
                ter(terNO_AMM));

            // Single deposit: 100000 tokens worth of USD
            // Amount to deposit exceeds Max
            ammAlice.deposit(
                carol,
                100'000,
                USD(200),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));

            // Single deposit: 100000 tokens worth of XRP
            // Amount to deposit exceeds Max
            ammAlice.deposit(
                carol,
                100'000,
                XRP(200),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));

            // Deposit amount is invalid
            // Calculated amount to deposit is 98,000,000
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                STAmount{USD, 1, -1},
                std::nullopt,
                ter(tecUNFUNDED_AMM));
            // Calculated amount is 0
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                STAmount{USD, 2'000, -6},
                std::nullopt,
                ter(tecAMM_FAILED));

            // Tiny deposit
            ammAlice.deposit(
                carol,
                IOUAmount{1, -4},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.deposit(
                carol,
                STAmount{USD, 1, -12},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));

            // Deposit non-empty AMM
            ammAlice.deposit(
                carol,
                XRP(100),
                USD(100),
                std::nullopt,
                tfTwoAssetIfEmpty,
                ter(tecAMM_NOT_EMPTY));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.deposit(
                alice, 10'000, std::nullopt, std::nullopt, ter(terNO_AMM));
        });

        // Globally frozen asset
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            // Can deposit non-frozen token
            ammAlice.deposit(carol, XRP(100));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
            ammAlice.deposit(
                carol, 1'000'000, std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Individually frozen (AMM) account
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, carol["USD"](0), tfSetFreeze));
            env.close();
            // Can deposit non-frozen token
            ammAlice.deposit(carol, XRP(100));
            ammAlice.deposit(
                carol, 1'000'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
            env(trust(gw, carol["USD"](0), tfClearFreeze));
            // Individually frozen AMM
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            env.close();
            // Can deposit non-frozen token
            ammAlice.deposit(carol, XRP(100));
            ammAlice.deposit(
                carol, 1'000'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
        });

        // Insufficient XRP balance
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1'000), bob);
            env.close();
            // Adds LPT trustline
            ammAlice.deposit(bob, XRP(10));
            ammAlice.deposit(
                bob,
                XRP(1'000),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient USD balance
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, {USD(1'000)}, Fund::Acct);
            env.close();
            ammAlice.deposit(
                bob,
                USD(1'001),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient USD balance by tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, {USD(1'000)}, Fund::Acct);
            env.close();
            ammAlice.deposit(
                bob,
                10'000'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient XRP balance by tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1'000), bob);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, bob, USD(90'000)));
            env.close();
            ammAlice.deposit(
                bob,
                10'000'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient reserve, XRP/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 4;
            env.fund(XRP(10'000), gw);
            env.fund(XRP(10'000), alice);
            env.fund(starting_xrp, carol);
            env.trust(USD(2'000), alice);
            env.trust(USD(2'000), carol);
            env.close();
            env(pay(gw, alice, USD(2'000)));
            env(pay(gw, carol, USD(2'000)));
            env.close();
            env(offer(carol, XRP(100), USD(101)));
            env(offer(carol, XRP(100), USD(102)));
            AMM ammAlice(env, alice, XRP(1'000), USD(1'000));
            ammAlice.deposit(
                carol,
                XRP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
        }

        // Insufficient reserve, IOU/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 4;
            env.fund(XRP(10'000), gw);
            env.fund(XRP(10'000), alice);
            env.fund(starting_xrp, carol);
            env.trust(USD(2'000), alice);
            env.trust(EUR(2'000), alice);
            env.trust(USD(2'000), carol);
            env.trust(EUR(2'000), carol);
            env.close();
            env(pay(gw, alice, USD(2'000)));
            env(pay(gw, alice, EUR(2'000)));
            env(pay(gw, carol, USD(2'000)));
            env(pay(gw, carol, EUR(2'000)));
            env.close();
            env(offer(carol, XRP(100), USD(101)));
            env(offer(carol, XRP(100), USD(102)));
            AMM ammAlice(env, alice, XRP(1'000), USD(1'000));
            ammAlice.deposit(
                carol,
                XRP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
        }

        // Invalid min
        testAMM([&](AMM& ammAlice, Env& env) {
            // min tokens can't be <= zero
            ammAlice.deposit(
                carol, 0, XRP(100), tfSingleAsset, ter(temBAD_AMM_TOKENS));
            ammAlice.deposit(
                carol, -1, XRP(100), tfSingleAsset, ter(temBAD_AMM_TOKENS));
            ammAlice.deposit(
                carol,
                0,
                XRP(100),
                USD(100),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
            // min amounts can't be <= zero
            ammAlice.deposit(
                carol,
                1'000,
                XRP(0),
                USD(100),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.deposit(
                carol,
                1'000,
                XRP(100),
                USD(-1),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            // min amount bad currency
            ammAlice.deposit(
                carol,
                1'000,
                XRP(100),
                BAD(100),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_CURRENCY));
            // min amount bad token pair
            ammAlice.deposit(
                carol,
                1'000,
                XRP(100),
                XRP(100),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
            ammAlice.deposit(
                carol,
                1'000,
                XRP(100),
                GBP(100),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Min deposit
        testAMM([&](AMM& ammAlice, Env& env) {
            // Equal deposit by tokens
            ammAlice.deposit(
                carol,
                1'000'000,
                XRP(1'000),
                USD(1'001),
                std::nullopt,
                tfLPToken,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
            ammAlice.deposit(
                carol,
                1'000'000,
                XRP(1'001),
                USD(1'000),
                std::nullopt,
                tfLPToken,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
            // Equal deposit by asset
            ammAlice.deposit(
                carol,
                100'001,
                XRP(100),
                USD(100),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
            // Single deposit by asset
            ammAlice.deposit(
                carol,
                488'090,
                XRP(1'000),
                std::nullopt,
                std::nullopt,
                tfSingleAsset,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
        });
    }

    void
    testDeposit()
    {
        testcase("Deposit");

        using namespace jtx;

        // Equal deposit: 1000000 tokens, 10% of the current pool
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1'000'000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));
            // 30,000 less deposited 1,000
            BEAST_EXPECT(expectLine(env, carol, USD(29'000)));
            // 30,000 less deposited 1,000 and 10 drops tx fee
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, carol, XRPAmount{28'999'999'990}));
        });

        // Equal limit deposit: deposit USD100 and XRP proportionally
        // to the pool composition not to exceed 100XRP. If the amount
        // exceeds 100XRP then deposit 100XRP and USD proportionally
        // to the pool composition not to exceed 100USD. Fail if exceeded.
        // Deposit 100USD/100XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(100), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'100), IOUAmount{10'100'000, 0}));
        });

        // Equal limit deposit.
        // Try to deposit 200USD/100XRP. Is truncated to 100USD/100XRP.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(200), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'100), IOUAmount{10'100'000, 0}));
        });
        // Try to deposit 100USD/200XRP. Is truncated to 100USD/100XRP.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(100), XRP(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'100), IOUAmount{10'100'000, 0}));
        });

        // Single deposit: 1000 USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1'000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), USD(11'000), IOUAmount{10'488'064'07608554, -8}));
        });

        // Single deposit: 1000 XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRP(1'000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(10'000), IOUAmount{10'488'064'07608554, -8}));
        });

        // Single deposit: 100000 tokens worth of USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, USD(205));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000),
                STAmount{USD, UINT64_C(10'201'0101010101), -10},
                IOUAmount{10'100'000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100'000, XRP(205));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(10'201'010'101),
                USD(10'000),
                IOUAmount{10'100'000, 0}));
        });

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.1 (AssetIn/TokensOut)
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(1'000), std::nullopt, STAmount{USD, 1, -1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), USD(11'000), IOUAmount{10'488'064'07608554, -8}));
        });

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(
                    carol, USD(100), std::nullopt, STAmount{USD, 2004, -6});
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    STAmount{USD, UINT64_C(10'079'95879839994), -11},
                    IOUAmount{10'039'899'59999997, -8}));
            },
            std::nullopt,
            1);

        // Single deposit with EP not exceeding specified:
        // 0USD with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(
                    carol, USD(0), std::nullopt, STAmount{USD, 2004, -6});
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    STAmount{USD, UINT64_C(10'079'95879839994), -11},
                    IOUAmount{10'039'899'59999997, -8}));
            },
            std::nullopt,
            1);

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000), BTC(0.5)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            AMM ammAlice(env, alice, USD(20'000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20'000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
            fund(env, gw, {carol}, {USD(2'000), BTC(0.05)}, Fund::Acct);
            // no transfer fee on deposit
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22'000), BTC(0.55), IOUAmount{110, 0}));
            BEAST_EXPECT(expectLine(env, carol, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0)));
        }

        // Tiny deposits
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, IOUAmount{1, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'000'000'001},
                STAmount{USD, UINT64_C(10'000'000001), -6},
                IOUAmount{10'000'000'001, -3}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1, -3}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRPAmount{1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'000'000'001},
                USD(10'000),
                IOUAmount{10'000'000'00049997, -8}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{49997, -8}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, STAmount{USD, 1, -10});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000),
                STAmount{USD, UINT64_C(10'000'00000000008), -11},
                IOUAmount{10'000'000'00000004, -8}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{4, -8}));
        });

        // Issuer create/deposit
        {
            Env env(*this);
            env.fund(XRP(30000), gw);
            AMM ammGw(env, gw, XRP(10'000), USD(10'000));
            BEAST_EXPECT(
                ammGw.expectBalances(XRP(10'000), USD(10'000), ammGw.tokens()));
            ammGw.deposit(gw, 1'000'000);
            BEAST_EXPECT(ammGw.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000}));
            ammGw.deposit(gw, USD(1'000));
            BEAST_EXPECT(ammGw.expectBalances(
                XRP(11'000),
                STAmount{USD, UINT64_C(11'999'99999999997), -11},
                IOUAmount{11'489'122'84743761, -8}));
        }

        // Issuer deposit
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(gw, 1'000'000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000}));
            ammAlice.deposit(gw, USD(1'000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000),
                STAmount{USD, UINT64_C(11'999'99999999998), -11},
                IOUAmount{11'489'100'83561455, -8}));
        });

        // Min deposit
        testAMM([&](AMM& ammAlice, Env& env) {
            // Equal deposit by tokens
            ammAlice.deposit(
                carol,
                1'000'000,
                XRP(1'000),
                USD(1'000),
                std::nullopt,
                tfLPToken,
                std::nullopt,
                std::nullopt);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));
        });
        testAMM([&](AMM& ammAlice, Env& env) {
            // Equal deposit by asset
            ammAlice.deposit(
                carol,
                1'000'000,
                XRP(1'000),
                USD(1'000),
                std::nullopt,
                tfTwoAsset,
                std::nullopt,
                std::nullopt);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));
        });
        testAMM([&](AMM& ammAlice, Env& env) {
            // Single deposit by asset
            ammAlice.deposit(
                carol,
                488'064,
                XRP(1'000),
                std::nullopt,
                std::nullopt,
                tfSingleAsset,
                std::nullopt,
                std::nullopt);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(10'000), IOUAmount{10'488'064'07608554, -8}));
        });
        testAMM([&](AMM& ammAlice, Env& env) {
            // Single deposit by asset
            ammAlice.deposit(
                carol,
                488'064,
                USD(1'000),
                std::nullopt,
                std::nullopt,
                tfSingleAsset,
                std::nullopt,
                std::nullopt);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), USD(11'000), IOUAmount{10'488'064'07608554, -8}));
        });
    }

    void
    testInvalidWithdraw()
    {
        testcase("Invalid Withdraw");

        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
#if 0
            // Invalid flags
            ammAlice.withdraw(
                alice,
                1'000'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                tfBurnable,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));
            ammAlice.withdraw(
                alice,
                1'000'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                tfTwoAssetIfEmpty,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));

            // Invalid options
            std::vector<std::tuple<
                std::optional<std::uint32_t>,
                std::optional<STAmount>,
                std::optional<STAmount>,
                std::optional<IOUAmount>,
                std::optional<std::uint32_t>,
                NotTEC>>
                invalidOptions = {
                    // tokens, asset1Out, asset2Out, EPrice, flags, ter
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfSingleAsset | tfTwoAsset,
                     temMALFORMED},
                    {1'000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     USD(0),
                     XRP(100),
                     std::nullopt,
                     tfWithdrawAll | tfLPToken,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfWithdrawAll | tfOneAssetWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfOneAssetWithdrawAll,
                     temMALFORMED},
                    {1'000,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     IOUAmount{250, 0},
                     tfWithdrawAll,
                     temMALFORMED},
                    {1'000,
                     std::nullopt,
                     std::nullopt,
                     IOUAmount{250, 0},
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     USD(100),
                     IOUAmount{250, 0},
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     XRP(100),
                     USD(100),
                     IOUAmount{250, 0},
                     std::nullopt,
                     temMALFORMED},
                    {1'000,
                     XRP(100),
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     XRP(100),
                     USD(100),
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED}};
            for (auto const& it : invalidOptions)
            {
                ammAlice.withdraw(
                    alice,
                    std::get<0>(it),
                    std::get<1>(it),
                    std::get<2>(it),
                    std::get<3>(it),
                    std::get<4>(it),
                    std::nullopt,
                    std::nullopt,
                    ter(std::get<5>(it)));
            }

            // Invalid tokens
            ammAlice.withdraw(
                alice, 0, std::nullopt, std::nullopt, ter(temBAD_AMM_TOKENS));
            ammAlice.withdraw(
                alice,
                IOUAmount{-1},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Mismatched token, invalid Asset1Out issue
            ammAlice.withdraw(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Mismatched token, invalid Asset2Out issue
            ammAlice.withdraw(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Mismatched token, Asset1Out.issue == Asset2Out.issue
            ammAlice.withdraw(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Invalid amount value
            ammAlice.withdraw(
                alice, USD(0), std::nullopt, std::nullopt, ter(temBAD_AMOUNT));
            ammAlice.withdraw(
                alice,
                USD(-100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.withdraw(
                alice,
                USD(10),
                std::nullopt,
                IOUAmount{-1},
                ter(temBAD_AMOUNT));

            // Invalid amount/token value, withdraw all tokens from one side
            // of the pool.
            ammAlice.withdraw(
                alice,
                USD(10'000),
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));
            ammAlice.withdraw(
                alice,
                XRP(10'000),
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));
            ammAlice.withdraw(
                alice,
                std::nullopt,
                USD(0),
                std::nullopt,
                std::nullopt,
                tfOneAssetWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));

            // Bad currency
            ammAlice.withdraw(
                alice,
                BAD(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_CURRENCY));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.withdraw(
                bad,
                1'000'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));

            // Invalid AMM
            ammAlice.withdraw(
                alice,
                1'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                std::nullopt,
                ter(terNO_AMM));

            // Carol is not a Liquidity Provider
            ammAlice.withdraw(
                carol, 10'000, std::nullopt, std::nullopt, ter(tecAMM_BALANCE));

            // Withdraw entire one side of the pool.
            // Equal withdraw but due to XRP precision limit,
            // this results in full withdraw of XRP pool only,
            // while leaving a tiny amount in USD pool.
            ammAlice.withdraw(
                alice,
                IOUAmount{9'999'999'9999, -4},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));
            // Withdrawing from one side.
            // XRP by tokens
            ammAlice.withdraw(
                alice,
                IOUAmount(9'999'999'9999, -4),
                XRP(0),
                std::nullopt,
                ter(tecAMM_BALANCE));
            // USD by tokens
            ammAlice.withdraw(
                alice,
                IOUAmount(9'999'999'99, -2),
                USD(0),
                std::nullopt,
                ter(tecAMM_BALANCE));
            // XRP
            ammAlice.withdraw(
                alice,
                XRP(10'000),
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));
            // USD
            ammAlice.withdraw(
                alice,
                STAmount{USD, UINT64_C(9'999'9999999999999), -13},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));
#endif
        });
#if 0
        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.withdraw(
                alice, 10'000, std::nullopt, std::nullopt, ter(terNO_AMM));
        });

        // Globally frozen asset
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            // Can withdraw non-frozen token
            ammAlice.withdraw(alice, XRP(100));
            ammAlice.withdraw(
                alice, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Individually frozen (AMM) account
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            // Can withdraw non-frozen token
            ammAlice.withdraw(alice, XRP(100));
            ammAlice.withdraw(
                alice, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));
            env(trust(gw, alice["USD"](0), tfClearFreeze));
            // Individually frozen AMM
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            // Can withdraw non-frozen token
            ammAlice.withdraw(alice, XRP(100));
            ammAlice.withdraw(
                alice, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Carol withdraws more than she owns
        testAMM([&](AMM& ammAlice, Env&) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1'000'000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));

            ammAlice.withdraw(
                carol,
                2'000'000,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));
        });

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are 0.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdraw(
                carol,
                USD(100),
                std::nullopt,
                IOUAmount{500, 0},
                ter(tecAMM_FAILED));
        });

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are greater than the LP shares.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdraw(
                carol,
                USD(100),
                std::nullopt,
                IOUAmount{600, 0},
                ter(tecAMM_INVALID_TOKENS));
        });

        // Withdraw with EPrice limit. Fails to withdraw, amount1
        // to withdraw is less than 1700USD.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdraw(
                carol,
                USD(1'700),
                std::nullopt,
                IOUAmount{520, 0},
                ter(tecAMM_FAILED));
        });

        // Deposit/Withdraw the same amount with the trading fee
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, USD(1'000));
                ammAlice.withdraw(
                    carol,
                    USD(1'000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
            },
            std::nullopt,
            1'000);
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, XRP(1'000));
                ammAlice.withdraw(
                    carol,
                    XRP(1'000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
            },
            std::nullopt,
            1'000);

        // Deposit/Withdraw the same amount fails due to the tokens adjustment
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, STAmount{USD, 1, -6});
            ammAlice.withdraw(
                carol,
                STAmount{USD, 1, -6},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Withdraw close to one side of the pool. Account's LP tokens
        // are rounded to all LP tokens.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice,
                STAmount{USD, UINT64_C(9'999'999999999999), -12},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_BALANCE));
        });

        // Tiny withdraw
        testAMM([&](AMM& ammAlice, Env&) {
            // XRP amount to withdraw is 0
            ammAlice.withdraw(
                alice,
                IOUAmount{1, -5},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
            // Calculated tokens to withdraw are 0
            ammAlice.withdraw(
                alice,
                std::nullopt,
                STAmount{USD, 1, -11},
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.deposit(carol, STAmount{USD, 1, -10});
            ammAlice.withdraw(
                carol,
                std::nullopt,
                STAmount{USD, 1, -9},
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.withdraw(
                carol,
                std::nullopt,
                XRPAmount{1},
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });
#endif
    }

    void
    testWithdraw()
    {
        testcase("Withdraw");

        using namespace jtx;

        // Equal withdrawal by Carol: 1000000 of tokens, 10% of the current
        // pool
        testAMM([&](AMM& ammAlice, Env& env) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1'000'000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount{1'000'000, 0}));
            // 30,000 less deposited 1,000
            BEAST_EXPECT(expectLine(env, carol, USD(29'000)));
            // 30,000 less deposited 1,000 and 10 drops tx fee
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, carol, XRPAmount{28'999'999'990}));

            // Carol withdraws all tokens
            ammAlice.withdraw(carol, 1'000'000);
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
            BEAST_EXPECT(expectLine(env, carol, USD(30'000)));
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, carol, XRPAmount{29'999'999'980}));
        });

        // Equal withdrawal by tokens 1000000, 10%
        // of the current pool
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 1'000'000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9'000), USD(9'000), IOUAmount{9'000'000, 0}));
        });

        // Equal withdrawal with a limit. Withdraw XRP200.
        // If proportional withdraw of USD is less than 100
        // then withdraw that amount, otherwise withdraw USD100
        // and proportionally withdraw XRP. It's the latter
        // in this case - XRP100/USD100.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(200), USD(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9'900), USD(9'900), IOUAmount{9'900'000, 0}));
        });

        // Equal withdrawal with a limit. XRP100/USD100.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(100), USD(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9'900), USD(9'900), IOUAmount{9'900'000, 0}));
        });

        // Single withdrawal by amount XRP1000
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(1'000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9'000), USD(10'000), IOUAmount{9'486'830'54632838, -8}));
        });

        // Single withdrawal by tokens 10000.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 10'000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000),
                STAmount{USD, UINT64_C(9'980'010099800101), -12},
                IOUAmount{9'990'000, 0}));
        });

        // Withdraw all tokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(carol, STAmount{ammAlice.lptIssue(), 10'000}));
            // Can SetTrust only for AMM LP tokens
            env(trust(
                    carol,
                    STAmount{
                        Issue{EUR.currency, ammAlice.ammAccount()}, 10'000}),
                ter(tecNO_PERMISSION));
            env.close();
            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());

            BEAST_EXPECT(!env.le(keylet::ownerDir(ammAlice.ammAccount())));

            // Can create AMM for the XRP/USD pair
            AMM ammCarol(env, carol, XRP(10'000), USD(10'000));
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(10'000), USD(10'000), IOUAmount{10'000'000, 0}));
        });

        // Single deposit 1000USD, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, USD(1'000));
            ammAlice.withdrawAll(carol, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000),
                STAmount{USD, UINT64_C(10'000'09307547123), -11},
                IOUAmount{10'000'000, 0}));
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
        });

        // Single deposit 1000USD, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1'000));
            ammAlice.withdrawAll(carol, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9'090'993'705),
                USD(11'000),
                IOUAmount{10'000'000, 0}));
        });

        // Single deposit/withdraw by the same account
        testAMM([&](AMM& ammAlice, Env&) {
            // Since a smaller amount might be deposited due to
            // the lp tokens adjustment, withdrawing by tokens
            // is generally preferred to withdrawing by amount.
            auto lpTokens = ammAlice.deposit(carol, USD(1'000));
            ammAlice.withdraw(carol, lpTokens, USD(0));
            lpTokens = ammAlice.deposit(carol, STAmount(USD, 1, -6));
            ammAlice.withdraw(carol, lpTokens, USD(0));
            lpTokens = ammAlice.deposit(carol, XRPAmount(1));
            ammAlice.withdraw(carol, lpTokens, XRPAmount(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000),
                STAmount{USD, UINT64_C(10'000'09307547133), -11},
                ammAlice.tokens()));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
        });

        // Single deposit by different accounts and then withdraw
        // in reverse.
        testAMM([&](AMM& ammAlice, Env&) {
            auto const carolTokens = ammAlice.deposit(carol, USD(1'000));
            auto const aliceTokens = ammAlice.deposit(alice, USD(1'000));
            ammAlice.withdraw(alice, aliceTokens, USD(0));
            ammAlice.withdraw(carol, carolTokens, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000),
                STAmount{USD, UINT64_C(10'000'10159013437), -11},
                ammAlice.tokens()));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
            BEAST_EXPECT(ammAlice.expectLPTokens(alice, ammAlice.tokens()));
        });

        // Equal deposit 10%, withdraw all tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdrawAll(carol);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), USD(10'000), IOUAmount{10'000'000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdrawAll(carol, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000),
                STAmount{USD, UINT64_C(9'090'991736288513), -12},
                IOUAmount{10'000'000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdrawAll(carol, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9'090'991'736),
                USD(11'000),
                IOUAmount{10'000'000, 0}));
        });

        // Withdraw with EPrice limit.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdraw(carol, USD(100), std::nullopt, IOUAmount{520, 0});
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount(11'000'000'000),
                    STAmount{USD, UINT64_C(9'374'583869679081), -12},
                    IOUAmount{10'154'783'61223313, -8}) &&
                ammAlice.expectLPTokens(
                    carol, IOUAmount{154'783'61223313, -8}));
            ammAlice.withdrawAll(carol);
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
        });

        // Withdraw with EPrice limit. AssetOut is 0.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.withdraw(carol, USD(0), std::nullopt, IOUAmount{520, 0});
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount(11'000'000'000),
                    STAmount{USD, UINT64_C(9'374'583869679081), -12},
                    IOUAmount{10'154'783'61223313, -8}) &&
                ammAlice.expectLPTokens(
                    carol, IOUAmount{154'783'61223313, -8}));
        });

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000), BTC(0.5)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            // no transfer fee on create
            AMM ammAlice(env, alice, USD(20'000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20'000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
            fund(env, gw, {carol}, {USD(2'000), BTC(0.05)}, Fund::Acct);
            // no transfer fee on deposit
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22'000), BTC(0.55), IOUAmount{110, 0}));
            BEAST_EXPECT(expectLine(env, carol, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0)));
            // no transfer fee on withdraw
            ammAlice.withdraw(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20'000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0, 0}));
            BEAST_EXPECT(expectLine(env, carol, USD(2'000)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0.05)));
        }

        // Tiny withdraw
        testAMM([&](AMM& ammAlice, Env&) {
            // By tokens
            ammAlice.withdraw(alice, IOUAmount{1, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9'999'999'999},
                STAmount{USD, UINT64_C(9'999'999999), -6},
                IOUAmount{9'999'999'999, -3}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            // Single XRP pool
            ammAlice.withdraw(alice, std::nullopt, XRPAmount{1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9'999'999'999},
                USD(10'000),
                IOUAmount{9'999'999'999499995, -9}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            // Single USD pool
            ammAlice.withdraw(alice, std::nullopt, STAmount{USD, 1, -10});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000),
                STAmount{USD, UINT64_C(9'999'9999999999), -10},
                IOUAmount{9'999'999'99999995, -8}));
        });

        // Withdraw close to entire pool
        // Equal by tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, IOUAmount{9'999'999'999, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{1}, STAmount{USD, 1, -6}, IOUAmount{1, -3}));
        });
        // USD by tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, IOUAmount{9'999'999}, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), STAmount{USD, 1, -10}, IOUAmount{1}));
        });
        // XRP by tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, IOUAmount{9'999'900}, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{1}, USD(10'000), IOUAmount{100}));
        });
        // USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice, STAmount{USD, UINT64_C(9'999'99999999999), -11});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), STAmount{USD, 1, -11}, IOUAmount{316227765, -9}));
        });
        // XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRPAmount{9'999'999'999});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{1}, USD(10'000), IOUAmount{99'999500005, -9}));
        });
    }

    void
    testInvalidFeeVote()
    {
        testcase("Invalid Fee Vote");
        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            // Invalid flags
            ammAlice.vote(
                std::nullopt,
                1'000,
                tfWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));

            // Invalid fee.
            ammAlice.vote(
                std::nullopt,
                1'001,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(ammAlice.expectTradingFee(10));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.vote(
                bad,
                1'000,
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));

            // Invalid AMM
            ammAlice.vote(
                alice,
                1'000,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                ter(terNO_AMM));

            // Account is not LP
            ammAlice.vote(
                carol,
                1'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.vote(
                alice,
                1'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(terNO_AMM));
        });
    }

    void
    testFeeVote()
    {
        testcase("Fee Vote");
        using namespace jtx;

        // One vote sets fee to 1%.
        testAMM([&](AMM& ammAlice, Env& env) {
            BEAST_EXPECT(ammAlice.expectAuctionSlot(1, 0, IOUAmount{0}));
            ammAlice.vote({}, 1'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
            // Discounted fee is 1/10 of trading fee.
            BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, IOUAmount{0}));
        });

        auto vote = [&](AMM& ammAlice,
                        Env& env,
                        int i,
                        int fundUSD = 100'000,
                        std::uint32_t tokens = 10'000'000,
                        std::vector<Account>* accounts = nullptr) {
            Account a(std::to_string(i));
            fund(env, gw, {a}, {USD(fundUSD)}, Fund::Acct);
            ammAlice.deposit(a, tokens);
            ammAlice.vote(a, 50 * (i + 1));
            if (accounts)
                accounts->push_back(std::move(a));
        };

        // Eight votes fill all voting slots, set fee 0.175%.
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 10'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(176));
        });

        // Eight votes fill all voting slots, set fee 0.175%.
        // New vote, same account, sets fee 0.225%
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(176));
            Account const a("0");
            ammAlice.vote(a, 450);
            BEAST_EXPECT(ammAlice.expectTradingFee(226));
        });

        // Eight votes fill all voting slots, set fee 0.175%.
        // New vote, new account, higher vote weight, set higher fee 0.244%
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(176));
            vote(ammAlice, env, 7, 100'000, 20'000'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(244));
        });

        // Eight votes fill all voting slots, set fee 0.219%.
        // New vote, new account, higher vote weight, set smaller fee 0.206%
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 7; i > 0; --i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(220));
            vote(ammAlice, env, 0, 100'000, 20'000'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(206));
        });

        // Eight votes fill all voting slots. The accounts then withdraw all
        // tokens. An account sets a new fee and the previous slots are
        // deleted.
        testAMM([&](AMM& ammAlice, Env& env) {
            std::vector<Account> accounts;
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 100'000, 10'000'000, &accounts);
            BEAST_EXPECT(ammAlice.expectTradingFee(176));
            for (int i = 0; i < 7; ++i)
                ammAlice.withdrawAll(accounts[i]);
            ammAlice.deposit(carol, 10'000'000);
            ammAlice.vote(carol, 1'000);
            // The initial LP set the fee to 1000. Carol gets 50% voting
            // power, and the new fee is 500.
            BEAST_EXPECT(ammAlice.expectTradingFee(505));
        });

        // Eight votes fill all voting slots. The accounts then withdraw some
        // tokens. The new vote doesn't get the voting power but
        // the slots are refreshed and the fee is updated.
        testAMM([&](AMM& ammAlice, Env& env) {
            std::vector<Account> accounts;
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 100'000, 10'000'000, &accounts);
            BEAST_EXPECT(ammAlice.expectTradingFee(176));
            for (int i = 0; i < 7; ++i)
                ammAlice.withdraw(accounts[i], 9'000'000);
            ammAlice.deposit(carol, 1'000);
            // The vote is not added to the slots
            ammAlice.vote(carol, 1'000);
            auto const info = ammAlice.ammRpcInfo()[jss::amm][jss::vote_slots];
            for (std::uint16_t i = 0; i < info.size(); ++i)
                BEAST_EXPECT(info[i][jss::account] != carol.human());
            // But the slots are refreshed and the fee is changed
            BEAST_EXPECT(ammAlice.expectTradingFee(88));
        });
    }

    void
    testInvalidBid()
    {
        testcase("Invalid Bid");
        using namespace jtx;
        using namespace std::chrono;

        testAMM([&](AMM& ammAlice, Env& env) {
            // Invalid flags
            ammAlice.bid(
                carol,
                0,
                std::nullopt,
                {},
                tfWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));

            ammAlice.deposit(carol, 1'000'000);
            // Invalid Bid price <= 0
            for (auto bid : {0, -100})
            {
                ammAlice.bid(
                    carol,
                    bid,
                    std::nullopt,
                    {},
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
                ammAlice.bid(
                    carol,
                    std::nullopt,
                    bid,
                    {},
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
            }

            // Invlaid Min/Max combination
            ammAlice.bid(
                carol,
                200,
                100,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.bid(
                bad,
                std::nullopt,
                100,
                {},
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));

            // Account is not LP
            Account const dan("dan");
            env.fund(XRP(1'000), dan);
            ammAlice.bid(
                dan,
                100,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(
                dan,
                std::nullopt,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));

            // Auth account is invalid.
            ammAlice.bid(
                carol,
                100,
                std::nullopt,
                {bob},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(terNO_ACCOUNT));

            // Invalid Assets
            ammAlice.bid(
                alice,
                std::nullopt,
                100,
                {},
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                ter(terNO_AMM));

            // Invalid Min/Max issue
            ammAlice.bid(
                alice,
                std::nullopt,
                STAmount{USD, 100},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
            ammAlice.bid(
                alice,
                STAmount{USD, 100},
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.bid(
                alice,
                std::nullopt,
                100,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(terNO_AMM));
        });

        // More than four Auth accounts.
        testAMM([&](AMM& ammAlice, Env& env) {
            Account ed("ed");
            Account bill("bill");
            Account scott("scott");
            Account james("james");
            env.fund(XRP(1'000), bob, ed, bill, scott, james);
            env.close();
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.bid(
                carol,
                100,
                std::nullopt,
                {bob, ed, bill, scott, james},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temMALFORMED));
        });

        // Bid price exceeds LP owned tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, XRP(1'000), {USD(100)}, Fund::Acct);
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.deposit(bob, 10);
            ammAlice.bid(
                carol,
                1'000'001,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(
                carol,
                std::nullopt,
                1'000'001,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(carol, 1'000);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(1, 0, IOUAmount{1'000}));
            // Slot purchase price is more than 1000 but bob only has 10 tokens
            ammAlice.bid(
                bob,
                std::nullopt,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Bid all tokens, still own the slot
        {
            Env env(*this);
            fund(env, gw, {alice, bob}, XRP(1'000), {USD(1'000)});
            AMM amm(env, gw, XRP(10), USD(1'000));
            auto const lpIssue = amm.lptIssue();
            env.trust(STAmount{lpIssue, 100}, alice);
            env.trust(STAmount{lpIssue, 50}, bob);
            env(pay(gw, alice, STAmount{lpIssue, 100}));
            env(pay(gw, bob, STAmount{lpIssue, 50}));
            amm.bid(alice, 100);
            // Alice doesn't have any more tokens, but
            // she still owns the slot.
            amm.bid(
                bob,
                std::nullopt,
                50,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
        }
    }

    void
    testBid()
    {
        testcase("Bid");
        using namespace jtx;
        using namespace std::chrono;

        // Auction slot initially is owned by AMM creator, who pays 0 price.

        // Bid 110 tokens. Pay bidMin.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(1, 0, IOUAmount{110}));
            // 110 tokens are burnt.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{10'999'890, 0}));
        });

        // Bid with min/max when the pay price is less than min.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1'000'000);
            // Bid exactly 110. Pay 110 because the pay price is < 110.
            ammAlice.bid(carol, 110, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(1, 0, IOUAmount{110}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{10'999'890}));
            // Bid exactly 180-200. Pay 180 because the pay price is < 180.
            ammAlice.bid(alice, 180, 200);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(1, 0, IOUAmount{180}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11'000), USD(11'000), IOUAmount{10'999'814'5, -1}));
        });

        // Start bid at bidMin 110.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1'000'000);
            // Bid, pay bidMin.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(1, 0, IOUAmount{110}));

            fund(env, gw, {bob}, {USD(10'000)}, Fund::Acct);
            ammAlice.deposit(bob, 1'000'000);
            // Bid, pay the computed price.
            ammAlice.bid(bob);
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(1, 0, IOUAmount(163'49956, -5)));
            // Bid bidMax fails because the computed price is higher.
            ammAlice.bid(
                carol,
                std::nullopt,
                120,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
            // Bid MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, std::nullopt, 600);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                1, 0, IOUAmount{219'67386200176, -11}));

            // Bid Min/MaxSlotPrice fails because the computed price is not in
            // range
            ammAlice.bid(
                carol,
                10,
                100,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED));
            // Bid Min/MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, 100, 600);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                1, 0, IOUAmount{278'656621706488, -12}));
        });

        // Slot states.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1'000'000);

            fund(env, gw, {bob}, {USD(10'000)}, Fund::Acct);
            ammAlice.deposit(bob, 1'000'000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12'000), USD(12'000), IOUAmount{12'000'000, 0}));

            // Initial state. Pay bidMin.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(1, 0, IOUAmount{110}));

            // 1st Interval after close, price for 0th interval.
            ammAlice.bid(bob);
            env.close(seconds(AUCTION_SLOT_INTERVAL_DURATION + 1));
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(1, 1, IOUAmount{163'49956, -5}));

            // 10th Interval after close, price for 1st interval.
            ammAlice.bid(carol);
            env.close(seconds(10 * AUCTION_SLOT_INTERVAL_DURATION + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                1, 10, IOUAmount{219'67386200176, -11}));

            // 20th Interval (expired) after close, price for 10th interval.
            ammAlice.bid(bob);
            env.close(seconds(
                AUCTION_SLOT_TIME_INTERVALS * AUCTION_SLOT_INTERVAL_DURATION +
                1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                1, std::nullopt, IOUAmount{278'656589006576, -12}));

            // 0 Interval.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(1, std::nullopt, IOUAmount{110}));
            // ~531.37 tokens are burnt on bidding fees.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12'000), USD(12'000), IOUAmount{11'999'468'6728309, -7}));
        });

        // Pool's fee 1%. Bid bidMin.
        // Auction slot owner and auth account trade at discounted fee -
        // 1/10 of the trading fee.
        // Other accounts trade at 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const dan("dan");
                Account const ed("ed");
                fund(env, gw, {bob, dan, ed}, {USD(20'000)}, Fund::Acct);
                ammAlice.deposit(bob, 1'000'000);
                ammAlice.deposit(ed, 1'000'000);
                ammAlice.deposit(carol, 500'000);
                ammAlice.deposit(dan, 500'000);
                auto ammTokens = ammAlice.getLPTokensBalance();
                ammAlice.bid(carol, 120, std::nullopt, {bob, ed});
                auto const slotPrice = IOUAmount{5'200};
                ammTokens -= slotPrice;
                BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, slotPrice));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(13'000), USD(13'000), ammTokens));
                // Discounted trade
                for (int i = 0; i < 10; ++i)
                {
                    auto tokens = ammAlice.deposit(carol, USD(100));
                    ammAlice.withdraw(carol, tokens, USD(0));
                    tokens = ammAlice.deposit(bob, USD(100));
                    ammAlice.withdraw(bob, tokens, USD(0));
                    tokens = ammAlice.deposit(ed, USD(100));
                    ammAlice.withdraw(ed, tokens, USD(0));
                }
                // carol, bob, and ed pay ~0.99USD in fees.
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(29'499'00572620545), -11));
                BEAST_EXPECT(
                    env.balance(bob, USD) ==
                    STAmount(USD, UINT64_C(18'999'00572616195), -11));
                BEAST_EXPECT(
                    env.balance(ed, USD) ==
                    STAmount(USD, UINT64_C(18'999'00572611841), -11));
                // USD pool is slightly higher because of the fees.
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(13'000),
                    STAmount(USD, UINT64_C(13'002'98282151419), -11),
                    ammTokens));
                ammTokens = ammAlice.getLPTokensBalance();
                // Trade with the fee
                for (int i = 0; i < 10; ++i)
                {
                    auto const tokens = ammAlice.deposit(dan, USD(100));
                    ammAlice.withdraw(dan, tokens, USD(0));
                }
                // dan pays ~9.94USD, which is ~10 times more in fees than
                // carol, bob, ed. the discounted fee is 10 times less
                // than the trading fee.
                BEAST_EXPECT(
                    env.balance(dan, USD) ==
                    STAmount(USD, UINT64_C(19'490'056722744), -9));
                // USD pool gains more in dan's fees.
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(13'000),
                    STAmount{USD, UINT64_C(13'012'92609877019), -11},
                    ammTokens));
                // Discounted fee payment
                ammAlice.deposit(carol, USD(100));
                ammTokens = ammAlice.getLPTokensBalance();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(13'000),
                    STAmount{USD, UINT64_C(13'112'92609877019), -11},
                    ammTokens));
                env(pay(carol, bob, USD(100)), path(~USD), sendmax(XRP(110)));
                env.close();
                // carol pays 100000 drops in fees
                // 99900668XRP swapped in for 100USD
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{13'100'000'668},
                    STAmount{USD, UINT64_C(13'012'92609877019), -11},
                    ammTokens));
                // Payment with the trading fee
                env(pay(alice, carol, XRP(100)), path(~XRP), sendmax(USD(110)));
                env.close();
                // alice pays ~1.011USD in fees, which is ~10 times more
                // than carol's fee
                // 100.099431529USD swapped in for 100XRP
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{13'000'000'668},
                    STAmount{USD, UINT64_C(13'114'03663047264), -11},
                    ammTokens));
                // Auction slot expired, no discounted fee
                env.close(seconds(TOTAL_TIME_SLOT_SECS + 1));
                // clock is parent's based
                env.close();
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(29'399'00572620545), -11));
                ammTokens = ammAlice.getLPTokensBalance();
                for (int i = 0; i < 10; ++i)
                {
                    auto const tokens = ammAlice.deposit(carol, USD(100));
                    ammAlice.withdraw(carol, tokens, USD(0));
                }
                // carol pays ~9.94USD in fees, which is ~10 times more in
                // trading fees vs discounted fee.
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(29'389'06197177128), -11));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{13'000'000'668},
                    STAmount{USD, UINT64_C(13'123'98038490681), -11},
                    ammTokens));
                env(pay(carol, bob, USD(100)), path(~USD), sendmax(XRP(110)));
                env.close();
                // carol pays ~1.008XRP in trading fee, which is
                // ~10 times more than the discounted fee.
                // 99.815876XRP is swapped in for 100USD
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(13'100'824'790),
                    STAmount{USD, UINT64_C(13'023'98038490681), -11},
                    ammTokens));
            },
            std::nullopt,
            1'000);

        // Bid tiny amount with 0.001% trading fee
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // Bid a tiny amount
                auto const tiny =
                    Number{STAmount::cMinValue, STAmount::cMinOffset};
                ammAlice.bid(alice, IOUAmount{tiny});
                // The fee is not 0, therefore the pay price is not tiny
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{4}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000), USD(10'000), IOUAmount{9999996}));
                // Bid the tiny amount
                ammAlice.bid(
                    alice,
                    IOUAmount{STAmount::cMinValue, STAmount::cMinOffset});
                // Pay slightly higher price
                BEAST_EXPECT(
                    ammAlice.expectAuctionSlot(0, 0, IOUAmount{8'1999984, -7}));
                // About 8.4 tokens are burnt
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    USD(10'000),
                    IOUAmount{9'999'991'6000016, -7}));
            },
            std::nullopt,
            1);

        // Reset auth account
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.bid(alice, IOUAmount{100}, std::nullopt, {carol});
            BEAST_EXPECT(ammAlice.expectAuctionSlot({carol}));
            ammAlice.bid(alice, IOUAmount{100});
            BEAST_EXPECT(ammAlice.expectAuctionSlot({}));
            Account bob("bob");
            Account dan("dan");
            fund(env, {bob, dan}, XRP(1'000));
            ammAlice.bid(alice, IOUAmount{100}, std::nullopt, {bob, dan});
            BEAST_EXPECT(ammAlice.expectAuctionSlot({bob, dan}));
        });

        // Bid all tokens, still own the slot and trade at a discount
        {
            Env env(*this);
            fund(env, gw, {alice, bob}, XRP(2'000), {USD(2'000)});
            AMM amm(env, gw, XRP(1'000), USD(1'010), false, 1'000);
            auto const lpIssue = amm.lptIssue();
            env.trust(STAmount{lpIssue, 500}, alice);
            env.trust(STAmount{lpIssue, 50}, bob);
            env(pay(gw, alice, STAmount{lpIssue, 500}));
            env(pay(gw, bob, STAmount{lpIssue, 50}));
            // Alice doesn't have anymore lp tokens
            amm.bid(alice, 500);
            BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{500}));
            BEAST_EXPECT(expectLine(env, alice, STAmount{lpIssue, 0}));
            // But trades with the discounted fee since she still owns the slot.
            // Alice pays 10011 drops in fees
            env(pay(alice, bob, USD(10)), path(~USD), sendmax(XRP(11)));
            BEAST_EXPECT(amm.expectBalances(
                XRPAmount{1'010'010'011},
                USD(1'000),
                IOUAmount{1'004'487'562112089, -9}));
            // Bob pays the full fee ~0.1USD
            env(pay(bob, alice, XRP(10)), path(~XRP), sendmax(USD(11)));
            BEAST_EXPECT(amm.expectBalances(
                XRPAmount{1'000'010'011},
                STAmount{USD, UINT64_C(1'010'10090898081), -11},
                IOUAmount{1'004'487'562112089, -9}));
        }
    }

    void
    testInvalidAMMPayment()
    {
        testcase("Invalid AMM Payment");
        using namespace jtx;
        using namespace std::chrono;
        using namespace std::literals::chrono_literals;

        // Can't pay into AMM account.
        // Can't pay out since there is no keys
        for (auto const& acct : {gw, alice})
        {
            {
                Env env(*this);
                fund(env, gw, {alice, carol}, XRP(1'000), {USD(100)});
                // XRP balance is below reserve
                AMM ammAlice(env, acct, XRP(10), USD(10));
                // Pay below reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                    ter(tecNO_PERMISSION));
                // Pay above reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(300)),
                    ter(tecNO_PERMISSION));
                // Pay IOU
                env(pay(carol, ammAlice.ammAccount(), USD(10)),
                    ter(tecNO_PERMISSION));
            }
            {
                Env env(*this);
                fund(env, gw, {alice, carol}, XRP(10'000'000), {USD(10'000)});
                // XRP balance is above reserve
                AMM ammAlice(env, acct, XRP(1'000'000), USD(100));
                // Pay below reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                    ter(tecNO_PERMISSION));
                // Pay above reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(1'000'000)),
                    ter(tecNO_PERMISSION));
            }
        }

        // Can't pay into AMM with escrow.
        testAMM([&](AMM& ammAlice, Env& env) {
            env(escrow(carol, ammAlice.ammAccount(), XRP(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s),
                fee(1'500),
                ter(tecNO_PERMISSION));
        });

        // Can't pay into AMM with paychan.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const pk = carol.pk();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current()->info().parentCloseTime + 200s;
            env(create(
                    carol,
                    ammAlice.ammAccount(),
                    XRP(1'000),
                    settleDelay,
                    pk,
                    cancelAfter),
                ter(tecNO_PERMISSION));
        });

        // Can't pay into AMM with checks.
        testAMM([&](AMM& ammAlice, Env& env) {
            env(check::create(env.master.id(), ammAlice.ammAccount(), XRP(100)),
                ter(tecNO_PERMISSION));
        });

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Can't consume whole pool
                env(pay(alice, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, XRP(100)),
                    path(~XRP),
                    sendmax(USD(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                // Overflow
                env(pay(alice,
                        carol,
                        STAmount{USD, UINT64_C(99'999999999), -9}),
                    path(~USD),
                    sendmax(XRP(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice,
                        carol,
                        STAmount{USD, UINT64_C(999'99999999), -8}),
                    path(~USD),
                    sendmax(XRP(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{xrpIssue(), 99'999'999}),
                    path(~XRP),
                    sendmax(USD(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                // Sender doesn't have enough funds
                env(pay(alice, carol, USD(99.99)),
                    path(~USD),
                    sendmax(XRP(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{xrpIssue(), 99'990'000}),
                    path(~XRP),
                    sendmax(USD(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
            },
            {{XRP(100), USD(100)}});

        // Globally frozen
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env(pay(alice, carol, USD(1)),
                path(~USD),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(XRP(10)),
                ter(tecPATH_DRY));
            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(USD(10)),
                ter(tecPATH_DRY));
        });

        // Individually frozen AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            env.close();
            env(pay(alice, carol, USD(1)),
                path(~USD),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(XRP(10)),
                ter(tecPATH_DRY));
            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(USD(10)),
                ter(tecPATH_DRY));
        });

        // Individually frozen accounts
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, carol["USD"](0), tfSetFreeze));
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                sendmax(USD(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(tecPATH_DRY));
        });
    }

    void
    testBasicPaymentEngine()
    {
        testcase("Basic Payment");
        using namespace jtx;

        // Payment 100USD for 100.010002XRP.
        // Force one path with tfNoRippleDirect.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(110)),
                    txflags(tfNoRippleDirect));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(10'100'010'002), USD(10'000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30'100)));
                // Initial balance 30,000 - 100010002(sendmax with tfee) - 10(tx
                // fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    bob,
                    XRP(30'000) - XRPAmount(100'010'002) - txfee(env, 1)));
            },
            {{XRP(10'000), USD(10'100)}});

        // Payment 100USD for 100.010002XRP, use default path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, USD(100)), sendmax(XRP(110)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{10'100'010'002}, USD(10'000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30'100)));
                // Initial balance 30,000 - 100010002(sendmax with tfee) - 10(tx
                // fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    bob,
                    XRP(30'000) - XRPAmount(100'010'002) - txfee(env, 1)));
            },
            {{XRP(10'000), USD(10'100)}});

        // This payment is identical to above. While it has
        // both default path and path, activeStrands has one path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, USD(100)), path(~USD), sendmax(XRP(110)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(10'100'010'002), USD(10'000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30'100)));
                // Initial balance 30,000 - 100010002(sendmax with tfee) - 10(tx
                // fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    bob,
                    XRP(30'000) - XRPAmount(100'010'002) - txfee(env, 1)));
            },
            {{XRP(10'000), USD(10'100)}});

        // Payment with limitQuality set.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                // Pays 10USD for 10XRP. A larger payment of ~99.11USD/100XRP
                // would have been sent has it not been for limitQuality.
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(10'008'999'900),
                    STAmount{USD, UINT64_C(10'001'00010001), -8},
                    ammAlice.tokens()));
                // Initial balance 30,000 + 8.99989999(limited by limitQuality)
                BEAST_EXPECT(expectLine(
                    env, carol, STAmount{USD, UINT64_C(30'008'99989999), -8}));
                // Initial balance 30,000 - 8999910(limited by limitQuality) -
                // 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    bob,
                    XRP(30'000) - XRPAmount(8'999'900) - txfee(env, 1)));

                // Fails because of limitQuality.
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality),
                    ter(tecPATH_DRY));
                env.close();
            },
            {{XRP(10'000), USD(10'010)}});

        // Payment with limitQuality and transfer fee set.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.1));
                env.close();
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                // Pays ~8.9USD for ~8.9XRP. A larger payment
                // would have been sent has it not been for limitQuality and
                // the transfer fee.
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(110)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{10'008'999'900},
                    STAmount{USD, UINT64_C(10'001'00010001), -8},
                    ammAlice.tokens()));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(30'008'18172726364), -11}));
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    bob,
                    XRP(30'000) - XRPAmount(8'999'900) - txfee(env, 1)));
            },
            {{XRP(10'000), USD(10'010)}});

        // Fail when partial payment is not set.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(tfNoRippleDirect),
                    ter(tecPATH_PARTIAL));
            },
            {{XRP(10'000), USD(10'000)}});

        // Non-default path (with AMM) has a better quality than default path.
        // The max possible liquidity is taken out of non-default
        // path ~20XRP/20EUR, ~20EUR/~20USD. The rest
        // is taken from the offer.
        {
            Env env(*this);
            fund(
                env, gw, {alice, carol}, {USD(30'000), EUR(30'000)}, Fund::All);
            env.close();
            env.fund(XRP(1'000), bob);
            env.close();
            auto ammEUR_XRP = AMM(env, alice, XRP(10'000), EUR(10'000));
            auto ammUSD_EUR = AMM(env, alice, EUR(10'000), USD(10'000));
            env(offer(alice, XRP(101), USD(100)), txflags(tfPassive));
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammEUR_XRP.expectBalances(
                XRPAmount(10'020'038'073),
                STAmount(EUR, UINT64_C(9'980'0039994001), -10),
                ammEUR_XRP.tokens()));
            BEAST_EXPECT(ammUSD_EUR.expectBalances(
                STAmount(USD, UINT64_C(9'980'045924135274), -12),
                STAmount(EUR, UINT64_C(10'019'99600059992), -11),
                ammUSD_EUR.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{Amounts{
                    XRPAmount(20'153'616),
                    STAmount(USD, UINT64_C(19'95407586472642), -14)}}}));
            // Initial 30,000 + ~100
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(30'099'99999999999), -11}));
            // Initial 1,000 - 20.038073(AMM pool) - 80.846384(offer) - 10(tx
            // fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1'000) - XRPAmount{20'038'073} - XRPAmount{80'846'384} -
                    txfee(env, 1)));
        }

        // Default path (with AMM) has a better quality than a non-default path.
        // The max possible liquidity is taken out of default
        // path ~54XRP/54USD. The rest is taken from the offer.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1'000), bob);
            env.close();
            env.trust(EUR(2'000), alice);
            env.close();
            env(pay(gw, alice, EUR(1'000)));
            env(offer(alice, XRP(101), EUR(100)), txflags(tfPassive));
            env.close();
            env(offer(alice, EUR(100), USD(100)), txflags(tfPassive));
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(10'054'287'711),
                STAmount(USD, UINT64_C(9'946'01079838027), -11),
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                2,
                {{Amounts{
                      XRPAmount(54'529'093),
                      STAmount(EUR, UINT64_C(53'98920161973), -11)},
                  Amounts{
                      STAmount(EUR, UINT64_C(53'98920161973), -11),
                      STAmount(USD, UINT64_C(53'98920161973), -11)}}}));
            // Initial 30,000 + ~100
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(30'100'00000000003), -11}));
            // Initial 1,000 - 54.287711(AMM pool) - 46.470907(offer) - 10(tx
            // fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1'000) - XRPAmount{54'287'711} - XRPAmount{46'470'907} -
                    txfee(env, 1)));
        });

        // Default path with AMM and Order Book offer. AMM is consumed first,
        // remaining amount is consumed by the offer.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(100)}, Fund::Acct);
                env.close();
                env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
                env.close();
                env(pay(alice, carol, USD(200)),
                    sendmax(XRP(200)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100),
                    STAmount{USD, UINT64_C(10'000'00099256204), -11},
                    ammAlice.tokens()));
                // Initial 30,000 + ~200
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(30'199'99900743796), -11}));
                // Initial 30,000 - 10000(AMM pool LP) - 100(AMM offer) -
                // - 100(offer) - 10(tx fee) - one reserve
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice,
                    XRP(30'000) - XRP(10'000) - XRP(100) - XRP(100) -
                        ammCrtFee(env) - txfee(env, 1)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{XRP(10'000), USD(10'100)}});

        // Default path with AMM and Order Book offer.
        // Order Book offer is consumed first.
        // Remaining amount is consumed by AMM.
        {
            Env env(*this);
            fund(env, gw, {alice, bob, carol}, XRP(20'000), {USD(2'000)});
            env(offer(bob, XRP(50), USD(150)), txflags(tfPassive));
            AMM ammAlice(env, alice, XRP(1'000), USD(1'050));
            env(pay(alice, carol, USD(200)),
                sendmax(XRP(200)),
                txflags(tfPartialPayment));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{1'050'000'501}, USD(1'000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(2'200)));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        // Offer crossing XRP/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(1'000)}, Fund::Acct);
                env.close();
                env(offer(bob, USD(100), XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{10'099'960'021}, USD(10'005), ammAlice.tokens()));
                // Initial 1,000 + 100
                BEAST_EXPECT(expectLine(env, bob, USD(1'100)));
                // Initial 30,000 - 99.960021(offer) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    bob,
                    XRP(30'000) - XRPAmount{99'960'021} - txfee(env, 1)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{XRP(10'000), USD(10'105)}});

        // Offer crossing IOU/IOU and transfer rate
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();
                env(offer(carol, EUR(100), GBP(100)));
                env.close();
                // No transfer fee, but trading fee
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{GBP, UINT64_C(1'090'918182727364), -12},
                    EUR(1'100),
                    ammAlice.tokens()));
                // Initial 30,000 - 90.918182727364(offer) - 25% transfer fee
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{GBP, UINT64_C(29'886'3522715908), -10}));
                // Initial 30,000 + 100(offer)
                BEAST_EXPECT(expectLine(env, carol, EUR(30'100)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{GBP(1'000), EUR(1'200)}});

        // Payment and transfer fee
        // Scenario:
        // Bob sends ~125GBP to pay ~79.99EUR to Carol
        // Payment execution:
        // bob's 125GBP/1.25 = 100GBP
        // ~100GBP/99.9EUR AMM offer
        // 99.99EUR/1.25 = 79.99EUR paid to carol
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {GBP(200), EUR(200)}, Fund::Acct);
                env(rate(gw, 1.25));
                env.close();
                env(pay(bob, carol, EUR(100)),
                    path(~EUR),
                    sendmax(GBP(125)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    GBP(1'100),
                    STAmount{EUR, UINT64_C(1'000'009090991736), -12},
                    ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, bob, GBP(75)));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{EUR, UINT64_C(30'079'99272720661), -11}));
            },
            {{GBP(1'000), EUR(1'100)}});

        // Payment and transfer fee, multiple steps
        // Scenario:
        // Dan's offer 200CAN/200GBP
        // AMM 1000GBP/10125EUR
        // Ed's offer 200EUR/200USD
        // Bob sends 195.3125CAN to pay 100USD to Carol
        // Payment execution:
        // bob's 195.3125CAN/1.25 = 156.25CAN -> dan's offer
        // 156.25CAN/156.25GBP 156.25GBP/1.25 = 125GBP -> AMM's offer
        // ~125GBP/124.98EUR 124.98EUR/1.25 = 99.9EUR -> ed's offer
        // 99.99EUR/99.99USD 99.99USD/1.25 = 79.99USD paid to carol
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const dan("dan");
                Account const ed("ed");
                auto const CAN = gw["CAN"];
                fund(env, gw, {dan}, {CAN(200), GBP(200)}, Fund::Acct);
                fund(env, gw, {ed}, {EUR(200), USD(200)}, Fund::Acct);
                fund(env, gw, {bob}, {CAN(195.3125)}, Fund::Acct);
                env(trust(carol, USD(100)));
                env(rate(gw, 1.25));
                env.close();
                env(offer(dan, CAN(200), GBP(200)));
                env(offer(ed, EUR(200), USD(200)));
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~GBP, ~EUR, ~USD),
                    sendmax(CAN(195.3125)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(expectLine(env, bob, CAN(0)));
                BEAST_EXPECT(expectLine(env, dan, CAN(356.25), GBP(43.75)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    GBP(10'125),
                    STAmount{EUR, UINT64_C(10'000'01234569425), -11},
                    ammAlice.tokens()));
                BEAST_EXPECT(expectLine(
                    env,
                    ed,
                    STAmount{EUR, UINT64_C(299'9901234446), -10},
                    STAmount{USD, UINT64_C(100'0098765554), -10}));
                BEAST_EXPECT(expectLine(
                    env, carol, STAmount{USD, UINT64_C(79'99209875568), -11}));
            },
            {{GBP(10'000), EUR(10'125)}});

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(pay(alice, carol, USD(99.99)),
                    path(~USD),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, XRP(100)),
                    path(~XRP),
                    sendmax(USD(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, STAmount{xrpIssue(), 99'999'900}),
                    path(~XRP),
                    sendmax(USD(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
            },
            {{XRP(100), USD(100)}});

        // Multiple paths/steps
        {
            Env env(*this);
            auto const ETH = gw["ETH"];
            fund(
                env,
                gw,
                {alice},
                XRP(100'000),
                {EUR(50'000), BTC(50'000), ETH(50'000), USD(50'000)});
            fund(env, gw, {carol, bob}, XRP(1'000), {USD(200)}, Fund::Acct);
            AMM xrp_eur(env, alice, XRP(10'100), EUR(10'000));
            AMM eur_btc(env, alice, EUR(10'000), BTC(10'200));
            AMM btc_usd(env, alice, BTC(10'100), USD(10'000));
            AMM xrp_usd(env, alice, XRP(10'150), USD(10'200));
            AMM xrp_eth(env, alice, XRP(10'000), ETH(10'100));
            AMM eth_eur(env, alice, ETH(10'900), EUR(11'000));
            AMM eur_usd(env, alice, EUR(10'100), USD(10'000));
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~BTC, ~USD),
                path(~USD),
                path(~ETH, ~EUR, ~USD),
                sendmax(XRP(200)));
            // XRP-ETH-EUR-USD
            // This path provides ~27.99USD/27.98XRP
            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10'027'979'800),
                STAmount{ETH, UINT64_C(10'071'82216995056), -11},
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                STAmount{ETH, UINT64_C(10'928'17783004944), -11},
                STAmount{EUR, UINT64_C(10'971'64344329585), -11},
                eth_eur.tokens()));
            BEAST_EXPECT(eur_usd.expectBalances(
                STAmount{EUR, UINT64_C(10'128'35655670415), -11},
                STAmount{USD, UINT64_C(9'972'00559916014), -11},
                eur_usd.tokens()));

            // XRP-USD path
            // This path provides ~72USD/72.21XRP
            BEAST_EXPECT(xrp_usd.expectBalances(
                XRPAmount(10'222'218'263),
                STAmount{USD, UINT64_C(10'127'99440083986), -11},
                xrp_usd.tokens()));

            // XRP-EUR-BTC-USD
            // This path doesn't provide any liquidity due to how
            // offers are generated in multi-path.
            BEAST_EXPECT(xrp_eur.expectBalances(
                XRP(10'100), EUR(10'000), xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                EUR(10'000), BTC(10'200), eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                BTC(10'100), USD(10'000), btc_usd.tokens()));

            BEAST_EXPECT(expectLine(env, carol, USD(300)));
        }

        // Dependent AMM
        {
            Env env(*this);
            auto const ETH = gw["ETH"];
            fund(
                env,
                gw,
                {alice},
                XRP(40'000),
                {EUR(50'000), BTC(50'000), ETH(50'000), USD(50'000)});
            fund(env, gw, {carol, bob}, XRP(1000), {USD(200)}, Fund::Acct);
            AMM xrp_eur(env, alice, XRP(10'100), EUR(10'000));
            AMM eur_btc(env, alice, EUR(10'000), BTC(10'200));
            AMM btc_usd(env, alice, BTC(10'100), USD(10'000));
            AMM xrp_eth(env, alice, XRP(10'000), ETH(10'100));
            AMM eth_eur(env, alice, ETH(10'900), EUR(11'000));
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~BTC, ~USD),
                path(~ETH, ~EUR, ~BTC, ~USD),
                sendmax(XRP(200)));
            // XRP-EUR-BTC-USD path provides ~13.1USD/~13.1XRP
            // XRP-ETH-EUR-BTC-USD path provides ~86.9USD/88XRP
            BEAST_EXPECT(xrp_eur.expectBalances(
                XRPAmount(10'113'127'436),
                STAmount{EUR, UINT64_C(9'987'075303306347), -12},
                xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                STAmount{EUR, UINT64_C(10'101'16409966271), -11},
                STAmount{BTC, UINT64_C(10'097'91268546083), -11},
                eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                STAmount{BTC, UINT64_C(10'202'08731453917), -11},
                USD(9'900),
                btc_usd.tokens()));
            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10'088'074'724),
                STAmount{ETH, UINT64_C(10'011'83050977531), -11},
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                STAmount{ETH, UINT64_C(10'988'16949022469), -11},
                STAmount{EUR, UINT64_C(10'911'76059703094), -11},
                eth_eur.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(300)));
        }

        // AMM offers limit
        // Consuming 30 CLOB offers, results in hitting 30 AMM offers limit.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1'000), bob);
            fund(env, gw, {bob}, {EUR(400)}, Fund::IOUOnly);
            env(trust(alice, EUR(200)));
            for (int i = 0; i < 30; ++i)
                env(offer(alice, EUR(1.0 + 0.01 * i), XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, EUR(140), XRP(100)));
            env(pay(bob, carol, USD(100)),
                path(~XRP, ~USD),
                sendmax(EUR(400)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            // Carol gets ~29.9USD because of the AMM offers limit
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'030),
                STAmount{USD, UINT64_C(9'970'092717208272), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(30'029'90728279172), -11}));
            BEAST_EXPECT(expectOffers(env, alice, 1, {{{EUR(140), XRP(100)}}}));
        });
        // This payment is fulfilled
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1'000), bob);
            fund(env, gw, {bob}, {EUR(400)}, Fund::IOUOnly);
            env(trust(alice, EUR(200)));
            for (int i = 0; i < 29; ++i)
                env(offer(alice, EUR(1.0 + 0.01 * i), XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, EUR(140), XRP(100)));
            env(pay(bob, carol, USD(100)),
                path(~XRP, ~USD),
                sendmax(EUR(400)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'101'020'228}, USD(9'900), ammAlice.tokens()));
            // Carol gets ~100USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(30'099'99999999999), -11}));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{{STAmount{EUR, 39'1716808, -7}, XRPAmount{27'979'772}}}}));
        });

        // Offer crossing with AMM and another offer. AMM has a better
        // quality and is consumed first.
        {
            Env env(*this);
            fund(env, gw, {alice, carol, bob}, XRP(30'000), {USD(30'000)});
            env(offer(bob, XRP(100), USD(100.001)));
            AMM ammAlice(env, alice, XRP(10'000), USD(10'100));
            env(offer(carol, USD(100), XRP(100)));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'049'827'864},
                STAmount{USD, UINT64_C(10'049'92836130495), -11},
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                bob,
                1,
                {{{XRPAmount{50'072'137},
                   STAmount{USD, UINT64_C(50'07263869505), -11}}}}));
            BEAST_EXPECT(expectLine(env, carol, USD(30'100)));
        }

        // Individually frozen account
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, carol["USD"](0), tfSetFreeze));
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(pay(alice, carol, USD(1)),
                path(~USD),
                sendmax(XRP(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(tesSUCCESS));
        });
    }

    void
    testAMMTokens()
    {
        testcase("AMM Tokens");
        using namespace jtx;

        // Offer crossing with AMM LPTokens and XRP.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const token1 = ammAlice.lptIssue();
            auto priceXRP = withdrawByTokens(
                STAmount{XRPAmount{10'000'000'000}},
                STAmount{token1, 10'000'000},
                STAmount{token1, 5'000'000},
                0);
            // Carol places an order to buy LPTokens
            env(offer(carol, STAmount{token1, 5'000'000}, priceXRP));
            // Alice places an order to sell LPTokens
            env(offer(alice, priceXRP, STAmount{token1, 5'000'000}));
            // Pool's LPTokens balance doesn't change
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), USD(10'000), IOUAmount{10'000'000}));
            // Carol is Liquidity Provider
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{5'000'000}));
            BEAST_EXPECT(ammAlice.expectLPTokens(alice, IOUAmount{5'000'000}));
            // Carol votes
            ammAlice.vote(carol, 1'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(505));
            ammAlice.vote(carol, 1);
            BEAST_EXPECT(ammAlice.expectTradingFee(6));
            // Carol bids
            ammAlice.bid(carol, 100);
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{4'999'900}));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{100}));
            BEAST_EXPECT(accountBalance(env, carol) == "22499999960");
            priceXRP = withdrawByTokens(
                STAmount{XRPAmount{10'000'000'000}},
                STAmount{token1, 9'999'900},
                STAmount{token1, 4'999'900},
                0);
            // Carol withdraws
            ammAlice.withdrawAll(carol, XRP(0));
            BEAST_EXPECT(accountBalance(env, carol) == "29999949949");
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'000'000'000} - priceXRP,
                USD(10'000),
                IOUAmount{5'000'000}));
            BEAST_EXPECT(ammAlice.expectLPTokens(alice, IOUAmount{5'000'000}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
        });

        // Offer crossing with two AMM LPTokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1'000'000);
            fund(env, gw, {alice, carol}, {EUR(10'000)}, Fund::IOUOnly);
            AMM ammAlice1(env, alice, XRP(10'000), EUR(10'000));
            ammAlice1.deposit(carol, 1'000'000);
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            env(offer(alice, STAmount{token1, 100}, STAmount{token2, 100}),
                txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(
                expectLine(env, alice, STAmount{token1, 10'000'100}) &&
                expectLine(env, alice, STAmount{token2, 9'999'900}));
            BEAST_EXPECT(
                expectLine(env, carol, STAmount{token2, 1'000'100}) &&
                expectLine(env, carol, STAmount{token1, 999'900}));
            BEAST_EXPECT(
                expectOffers(env, alice, 0) && expectOffers(env, carol, 0));
        });

        // LPs pay LPTokens directly. Must trust set because the trust line
        // is checked for the limit, which is 0 in the AMM auto-created
        // trust line.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const token1 = ammAlice.lptIssue();
            env.trust(STAmount{token1, 2'000'000}, carol);
            env.close();
            ammAlice.deposit(carol, 1'000'000);
            BEAST_EXPECT(
                ammAlice.expectLPTokens(alice, IOUAmount{10'000'000, 0}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{1'000'000, 0}));
            // Pool balance doesn't change, only tokens moved from
            // one line to another.
            env(pay(alice, carol, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(
                // Alice initial token1 10,000,000 - 100
                ammAlice.expectLPTokens(alice, IOUAmount{9'999'900, 0}) &&
                // Carol initial token1 1,000,000 + 100
                ammAlice.expectLPTokens(carol, IOUAmount{1'000'100, 0}));

            env.trust(STAmount{token1, 20'000'000}, alice);
            env.close();
            env(pay(carol, alice, STAmount{token1, 100}));
            env.close();
            // Back to the original balance
            BEAST_EXPECT(
                ammAlice.expectLPTokens(alice, IOUAmount{10'000'000, 0}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{1'000'000, 0}));
        });
    }

    void
    testAmendment()
    {
        testcase("Amendment");
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const noAMM{all - featureAMM};
        FeatureBitset const noNumber{all - fixUniversalNumber};
        FeatureBitset const noAMMAndNumber{
            all - featureAMM - fixUniversalNumber};

        for (auto const& feature : {noAMM, noNumber, noAMMAndNumber})
        {
            Env env{*this, feature};
            fund(env, gw, {alice}, {USD(1'000)}, Fund::All);
            AMM amm(env, alice, XRP(1'000), USD(1'000), ter(temDISABLED));
        }
    }

    void
    testFlags()
    {
        testcase("Flags");
        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            auto const info = env.rpc(
                "json",
                "account_info",
                std::string(
                    "{\"account\": \"" + to_string(ammAlice.ammAccount()) +
                    "\"}"));
            auto const flags =
                info[jss::result][jss::account_data][jss::Flags].asUInt();
            BEAST_EXPECT(
                flags ==
                (lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth));
        });
    }

    void
    testRippling()
    {
        testcase("Rippling");
        using namespace jtx;

        // Rippling via AMM fails because AMM trust line has 0 limit.
        // Set up two issuers, A and B. Have each issue a token called TST.
        // Have another account C hold TST from both issuers,
        //   and create an AMM for this pair.
        // Have a fourth account, D, create a trust line to the AMM for TST.
        // Send a payment delivering TST.AMM from C to D, using SendMax in
        //   TST.A (or B) and a path through the AMM account. By normal
        //   rippling rules, this would have caused the AMM's balances
        //   to shift at a 1:1 rate with no fee applied has it not been
        //   for 0 limit.
        {
            Env env(*this);
            auto const A = Account("A");
            auto const B = Account("B");
            auto const TSTA = A["TST"];
            auto const TSTB = B["TST"];
            auto const C = Account("C");
            auto const D = Account("D");

            env.fund(XRP(10'000), A);
            env.fund(XRP(10'000), B);
            env.fund(XRP(10'000), C);
            env.fund(XRP(10'000), D);

            env.trust(TSTA(10'000), C);
            env.trust(TSTB(10'000), C);
            env(pay(A, C, TSTA(10'000)));
            env(pay(B, C, TSTB(10'000)));
            AMM amm(env, C, TSTA(5'000), TSTB(5'000));
            auto const ammIss = Issue(TSTA.currency, amm.ammAccount());

            // Can SetTrust only for AMM LP tokens
            env(trust(D, STAmount{ammIss, 10'000}), ter(tecNO_PERMISSION));
            env.close();

            // The payment would fail because of above, but check just in case
            env(pay(C, D, STAmount{ammIss, 10}),
                sendmax(TSTA(100)),
                path(amm.ammAccount()),
                txflags(tfPartialPayment | tfNoRippleDirect),
                ter(tecPATH_DRY));
        }
    }

    void
    testAMMAndCLOB()
    {
        testcase("AMMAndCLOB, offer quality change");
        using namespace jtx;
        auto const gw = Account("gw");
        auto const TST = gw["TST"];
        auto const LP1 = Account("LP1");
        auto const LP2 = Account("LP2");

        auto prep = [&](auto const& offerCb, auto const& expectCb) {
            Env env(*this);
            env.fund(XRP(30'000'000'000), gw);
            env(offer(gw, XRP(11'500'000'000), TST(1'000'000'000)));

            env.fund(XRP(10'000), LP1);
            env.fund(XRP(10'000), LP2);
            env(offer(LP1, TST(25), XRPAmount(287'500'000)));

            // Either AMM or CLOB offer
            offerCb(env);

            env(offer(LP2, TST(25), XRPAmount(287'500'000)));

            expectCb(env);
        };

        // If we replace AMM with equivalent CLOB offer, which
        // AMM generates when it is consumed, then the
        // result must be identical.
        std::string lp2TSTBalance;
        std::string lp2TakerGets;
        std::string lp2TakerPays;
        // Execute with AMM first
        prep(
            [&](Env& env) { AMM amm(env, LP1, TST(25), XRP(250)); },
            [&](Env& env) {
                lp2TSTBalance =
                    getAccountLines(env, LP2, TST)["lines"][0u]["balance"]
                        .asString();
                auto const offer = getAccountOffers(env, LP2)["offers"][0u];
                lp2TakerGets = offer["taker_gets"].asString();
                lp2TakerPays = offer["taker_pays"]["value"].asString();
            });
        // Execute with CLOB offer
        prep(
            [&](Env& env) {
                env(offer(
                        LP1,
                        XRPAmount{18'096'038},
                        STAmount{TST, UINT64_C(1'68730118738883), -14}),
                    txflags(tfPassive));
            },
            [&](Env& env) {
                BEAST_EXPECT(
                    lp2TSTBalance ==
                    getAccountLines(env, LP2, TST)["lines"][0u]["balance"]
                        .asString());
                auto const offer = getAccountOffers(env, LP2)["offers"][0u];
                BEAST_EXPECT(lp2TakerGets == offer["taker_gets"].asString());
                BEAST_EXPECT(
                    lp2TakerPays == offer["taker_pays"]["value"].asString());
            });
    }

    void
    testTradingFee()
    {
        testcase("Trading Fee");
        using namespace jtx;

        // Single Deposit, 0.01-1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // 0.01% fee
                ammAlice.deposit(carol, USD(3'000));
                // Carol gets fewer LPToken ~999.9, because of the single
                // deposit fee
                BEAST_EXPECT(ammAlice.expectLPTokens(
                    carol, IOUAmount{999'949998124907, -12}));
                ammAlice.withdrawAll(carol, USD(2'995));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'999'89999249953), -11}));
                // Set fee to 1%
                ammAlice.vote(alice, 1'000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
                // Carol gets fewer LPToken ~994.9, because of the single
                // deposit fee
                ammAlice.deposit(carol, USD(3'000));
                BEAST_EXPECT(ammAlice.expectLPTokens(
                    carol, IOUAmount{994'906532333892, -12}));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(26'999'89999249953), -11}));
                // Set fee to 0.01%
                ammAlice.vote(alice, 10);
                BEAST_EXPECT(ammAlice.expectTradingFee(10));
                ammAlice.withdrawAll(carol, USD(0));
                // Carol gets back less than the original deposit
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'994'81174769434), -11}));
            },
            {{USD(1'000), EUR(1'000)}});

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.1 (AssetIn/TokensOut). 0.01-1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const balance = env.balance(carol, USD);
                auto tokensFee = ammAlice.deposit(
                    carol, USD(1'000), std::nullopt, STAmount{USD, 1, -1});
                auto const deposit = balance - env.balance(carol, USD);
                ammAlice.withdrawAll(carol, USD(0));
                ammAlice.vote(alice, 10);
                BEAST_EXPECT(ammAlice.expectTradingFee(10));
                auto const tokensSmallFee = ammAlice.deposit(carol, deposit);
                BEAST_EXPECT(tokensFee == IOUAmount(485'636'0611129, -7));
                BEAST_EXPECT(tokensSmallFee == IOUAmount(487'620'47557731, -8));
            },
            std::nullopt,
            1'000);

        // Single deposit with EP not exceeding specified:
        // 200USD with EP not to exceed 0.002020 (AssetIn/TokensOut). 0.01-1%
        // fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const balance = env.balance(carol, USD);
                auto const tokensFee = ammAlice.deposit(
                    carol, USD(200), std::nullopt, STAmount{USD, 2020, -6});
                auto const deposit = balance - env.balance(carol, USD);
                ammAlice.withdrawAll(carol, USD(0));
                ammAlice.vote(alice, 10);
                BEAST_EXPECT(ammAlice.expectTradingFee(10));
                auto const tokensSmallFee = ammAlice.deposit(carol, deposit);
                BEAST_EXPECT(tokensFee == IOUAmount(98'000'00000002, -8));
                BEAST_EXPECT(tokensSmallFee == IOUAmount(98'470'89467951, -8));
            },
            std::nullopt,
            1'000);

        // Single Withdrawal, 0.01-1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // 0.01% fee
                ammAlice.deposit(carol, USD(3'000));

                BEAST_EXPECT(ammAlice.expectLPTokens(
                    carol, IOUAmount{999'949998124907, -12}));
                BEAST_EXPECT(expectLine(env, carol, USD(27'000)));
                // Set fee to 1%
                ammAlice.vote(alice, 1'000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
                // Single withdrawal. Carol gets ~5USD less than deposited.
                ammAlice.withdrawAll(carol, USD(0));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'994'92474562006), -11}));
            },
            {{USD(1'000), EUR(1'000)}});

        // Withdraw with EPrice limit, 0.01-1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000);
                auto const tokensFee = ammAlice.withdraw(
                    carol, USD(100), std::nullopt, IOUAmount{520, 0});
                // carol withdraws ~1,443.44USD
                auto const balanceAfterWithdraw =
                    STAmount(USD, UINT64_C(30'443'43891402715), -11);
                BEAST_EXPECT(env.balance(carol, USD) == balanceAfterWithdraw);
                // Set to original pool size
                auto const deposit = balanceAfterWithdraw - USD(29'000);
                ammAlice.deposit(carol, deposit);
                // fee 0.01%
                ammAlice.vote(alice, 10);
                BEAST_EXPECT(ammAlice.expectTradingFee(10));
                auto const tokensSmallFee = ammAlice.withdraw(carol, deposit);
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(30'443'43891402717), -11));
                BEAST_EXPECT(tokensSmallFee == IOUAmount(746'614'60318839, -8));
                BEAST_EXPECT(tokensFee == IOUAmount(750'588'23529411, -8));
            },
            std::nullopt,
            1'000);

        // Payment, 0.01-1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(
                    env,
                    gw,
                    {bob},
                    XRP(1'000),
                    {USD(1'000), EUR(1'000)},
                    Fund::Acct);
                // Alice contributed 1010EUR and 1000USD to the pool
                BEAST_EXPECT(expectLine(env, alice, EUR(28'990)));
                BEAST_EXPECT(expectLine(env, alice, USD(29'000)));
                BEAST_EXPECT(expectLine(env, carol, USD(30'000)));
                // Carol pays to Alice with 0.01% fee
                env(pay(carol, alice, EUR(10)),
                    path(~EUR),
                    sendmax(USD(11)),
                    txflags(tfNoRippleDirect));
                env.close();
                // Alice has 10EUR more and Carol has ~10USD less
                BEAST_EXPECT(expectLine(env, alice, EUR(29'000)));
                BEAST_EXPECT(expectLine(env, alice, USD(29'000)));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'989'99899989999), -11}));

                // Set fee to 1%
                ammAlice.vote(alice, 1'000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
                // Bob pays to Carol with 1% fee
                env(pay(bob, carol, USD(10)),
                    path(~USD),
                    sendmax(EUR(15)),
                    txflags(tfNoRippleDirect));
                env.close();
                // Bob sends 10.1~EUR to pay ~10USD
                BEAST_EXPECT(expectLine(
                    env, bob, STAmount{EUR, UINT64_C(989'899000001), -9}));
                // Carol got ~10USD
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'999'99899989999), -11}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(1'000'00100010001), -11},
                    STAmount{EUR, UINT64_C(1'010'100999999), -9},
                    ammAlice.tokens()));
            },
            {{USD(1'000), EUR(1'010)}});

        // Offer crossing, 0.01-0.5% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // 0.01% fee
                env(offer(carol, EUR(10), USD(10)));
                env.close();
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'990'01098671593), -11}));
                BEAST_EXPECT(expectLine(env, carol, EUR(30'010)));
                // Change pool composition back
                env(offer(carol, USD(10), EUR(10)));
                env.close();
                // Set fee to 0.5%
                ammAlice.vote(alice, 500);
                BEAST_EXPECT(ammAlice.expectTradingFee(500));
                env(offer(carol, EUR(10), USD(10)));
                env.close();
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'995'14277567372), -11}));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{EUR, UINT64_C(30'004'86821104221), -11}));
                BEAST_EXPECT(expectOffers(
                    env,
                    carol,
                    1,
                    {{Amounts{
                        STAmount{EUR, UINT64_C(6'44290568672), -11},
                        STAmount{USD, UINT64_C(6'44290568672), -11}}}}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(1'004'85722432628), -11},
                    STAmount{EUR, UINT64_C(1'006'331788957789), -12},
                    ammAlice.tokens()));
            },
            {{USD(1'000), STAmount{EUR, 1'011'2, -1}}});

        // Payment with AMM and CLOB offer, 0.001% fee
        // AMM liquidity is consumed first up to CLOB offer quality
        // CLOB offer is fully consumed next
        // Remaining amount is consumed via AMM liquidity
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(2'000), EUR(2'000)});
            env(offer(carol, EUR(5), USD(5)));
            AMM ammAlice(env, alice, USD(1'005), EUR(1'000), false, 1);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2'010)));
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{EUR, UINT64_C(1'989'999949937154), -12}));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1'000),
                STAmount{EUR, UINT64_C(1'005'000050062846), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. Same as above but with 0.25% fee.
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(2'000), EUR(2'000)});
            env(offer(carol, EUR(5), USD(5)));
            // Set 0.25% fee
            AMM ammAlice(env, alice, USD(1'005), EUR(1'000), false, 250);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2'010)));
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{EUR, UINT64_C(1'989'987453007618), -12}));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1'000),
                STAmount{EUR, UINT64_C(1'005'012546992382), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. AMM has a better
        // spot price quality, but 1% fee offsets that. As the result
        // the entire trade is executed via LOB.
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(2'000), EUR(2'000)});
            env(offer(carol, EUR(10), USD(10)));
            // Set 1% fee
            AMM ammAlice(env, alice, USD(1'005), EUR(1'000), false, 1'000);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2'010)));
            BEAST_EXPECT(expectLine(env, bob, EUR(1'990)));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1'005), EUR(1'000), ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. AMM has a better
        // spot price quality, but 1% fee offsets that.
        // The CLOB offer is consumed first and the remaining
        // amount is consumed via AMM liquidity.
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(2'000), EUR(2'000)});
            env(offer(carol, EUR(9), USD(9)));
            // Set 1% fee
            AMM ammAlice(env, alice, USD(1'005), EUR(1'000), false, 1'000);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2'010)));
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{EUR, UINT64_C(1'989'993923296712), -12}));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1'004),
                STAmount{EUR, UINT64_C(1'001'006076703288), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }
    }

    void
    testAdjustedTokens()
    {
        testcase("Adjusted Deposit/Withdraw Tokens");

        using namespace jtx;

        // Deposit/Withdraw in USD
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const bob("bob");
                Account const ed("ed");
                Account const paul("paul");
                Account const dan("dan");
                Account const chris("chris");
                Account const simon("simon");
                Account const ben("ben");
                Account const nataly("nataly");
                fund(
                    env,
                    gw,
                    {bob, ed, paul, dan, chris, simon, ben, nataly},
                    {USD(1'500'000)},
                    Fund::Acct);
                for (int i = 0; i < 10; ++i)
                {
                    ammAlice.deposit(ben, STAmount{USD, 1, -10});
                    ammAlice.withdrawAll(ben, USD(0));
                    ammAlice.deposit(simon, USD(0.1));
                    ammAlice.withdrawAll(simon, USD(0));
                    ammAlice.deposit(chris, USD(1));
                    ammAlice.withdrawAll(chris, USD(0));
                    ammAlice.deposit(dan, USD(10));
                    ammAlice.withdrawAll(dan, USD(0));
                    ammAlice.deposit(bob, USD(100));
                    ammAlice.withdrawAll(bob, USD(0));
                    ammAlice.deposit(carol, USD(1'000));
                    ammAlice.withdrawAll(carol, USD(0));
                    ammAlice.deposit(ed, USD(10'000));
                    ammAlice.withdrawAll(ed, USD(0));
                    ammAlice.deposit(paul, USD(100'000));
                    ammAlice.withdrawAll(paul, USD(0));
                    ammAlice.deposit(nataly, USD(1'000'000));
                    ammAlice.withdrawAll(nataly, USD(0));
                }
                // Due to round off some accounts have a tiny gain, while
                // other have a tiny loss. The last account to withdraw
                // gets everything in the pool.
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    STAmount{USD, UINT64_C(10'003'888487233), -9},
                    IOUAmount{10'000'000}));
                BEAST_EXPECT(expectLine(env, ben, USD(1'500'000)));
                BEAST_EXPECT(expectLine(
                    env, simon, STAmount{USD, UINT64_C(1'499'999'99999), -5}));
                BEAST_EXPECT(expectLine(
                    env,
                    chris,
                    STAmount{USD, UINT64_C(1'499'999'99990001), -8}));
                BEAST_EXPECT(expectLine(
                    env, dan, STAmount{USD, UINT64_C(1'499'999'99900075), -8}));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(29'999'90692399385), -11}));
                BEAST_EXPECT(expectLine(
                    env, ed, STAmount{USD, UINT64_C(1'499'999'414170119), -9}));
                BEAST_EXPECT(expectLine(
                    env,
                    paul,
                    STAmount{USD, UINT64_C(1'499'998'60280384), -8}));
                BEAST_EXPECT(expectLine(
                    env,
                    nataly,
                    STAmount{USD, UINT64_C(1'499'998'19864969), -8}));
                ammAlice.withdrawAll(alice);
                BEAST_EXPECT(!ammAlice.ammExists());
                BEAST_EXPECT(expectLine(
                    env, alice, STAmount{USD, UINT64_C(30'003'888487233), -9}));
                // alice XRP balance is 30,000initial - 50 ammcreate fee -
                // 10drops fee
                BEAST_EXPECT(accountBalance(env, alice) == "29949999990");
            },
            std::nullopt,
            1);

        // Same as above but deposit/withdraw in XRP
        testAMM([&](AMM& ammAlice, Env& env) {
            Account const bob("bob");
            Account const ed("ed");
            Account const paul("paul");
            Account const dan("dan");
            Account const chris("chris");
            Account const simon("simon");
            Account const ben("ben");
            Account const nataly("nataly");
            fund(
                env,
                gw,
                {bob, ed, paul, dan, chris, simon, ben, nataly},
                XRP(2'000'000),
                {},
                Fund::Acct);
            for (int i = 0; i < 10; ++i)
            {
                ammAlice.deposit(ben, XRPAmount{1});
                ammAlice.withdrawAll(ben, XRP(0));
                ammAlice.deposit(simon, XRPAmount(1'000));
                ammAlice.withdrawAll(simon, XRP(0));
                ammAlice.deposit(chris, XRP(1));
                ammAlice.withdrawAll(chris, XRP(0));
                ammAlice.deposit(dan, XRP(10));
                ammAlice.withdrawAll(dan, XRP(0));
                ammAlice.deposit(bob, XRP(100));
                ammAlice.withdrawAll(bob, XRP(0));
                ammAlice.deposit(carol, XRP(1'000));
                ammAlice.withdrawAll(carol, XRP(0));
                ammAlice.deposit(ed, XRP(10'000));
                ammAlice.withdrawAll(ed, XRP(0));
                ammAlice.deposit(paul, XRP(100'000));
                ammAlice.withdrawAll(paul, XRP(0));
                ammAlice.deposit(nataly, XRP(1'000'000));
                ammAlice.withdrawAll(nataly, XRP(0));
            }
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'038'940'724}, USD(10'000), IOUAmount{10'000'000}));
            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());
            BEAST_EXPECT(env.balance(ben, XRP) == XRPAmount{1'999'999'999'800});
            BEAST_EXPECT(
                env.balance(simon, XRP) == XRPAmount{1'999'999'999'800});
            BEAST_EXPECT(
                env.balance(chris, XRP) == XRPAmount{1'999'999'998'800});
            BEAST_EXPECT(env.balance(dan, XRP) == XRPAmount{1'999'999'989'810});
            BEAST_EXPECT(env.balance(carol, XRP) == XRPAmount{29'999'068935});
            BEAST_EXPECT(env.balance(ed, XRP) == XRPAmount{1'999'994'137592});
            BEAST_EXPECT(env.balance(paul, XRP) == XRPAmount{1'999'986'008130});
            BEAST_EXPECT(
                env.balance(nataly, XRP) == XRPAmount{1'999'981'954069});
            BEAST_EXPECT(env.balance(alice, XRP) == XRPAmount{29'988'940714});
        });
    }

    void
    testAutoDelete()
    {
        testcase("Auto Delete");

        using namespace jtx;
        FeatureBitset const all{supported_amendments()};

        {
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.reference_fee = XRPAmount(1);
                    return cfg;
                }),
                all);
            fund(env, gw, {alice}, XRP(20'000), {USD(10'000)});
            AMM amm(env, gw, XRP(10'000), USD(10'000));
            for (auto i = 0; i < maxDeletableAMMTrustLines + 10; ++i)
            {
                Account const a{std::to_string(i)};
                env.fund(XRP(1'000), a);
                env(trust(a, STAmount{amm.lptIssue(), 10'000}));
                env.close();
            }
            // The trustlines are partially deleted,
            // AMM is set to an empty state.
            amm.withdrawAll(gw);
            BEAST_EXPECT(amm.ammExists());

            // Bid,Vote,Deposit,Withdraw,SetTrust failing with
            // tecAMM_EMPTY. Deposit succeeds with tfTwoAssetIfEmpty option.
            amm.bid(
                alice,
                1000,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_EMPTY));
            amm.vote(
                std::nullopt,
                100,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_EMPTY));
            amm.withdraw(
                alice, 100, std::nullopt, std::nullopt, ter(tecAMM_EMPTY));
            amm.deposit(
                alice,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_EMPTY));
            env(trust(alice, STAmount{amm.lptIssue(), 10'000}),
                ter(tecAMM_EMPTY));

            // Can deposit with tfTwoAssetIfEmpty option
            amm.deposit(
                alice,
                std::nullopt,
                XRP(10'000),
                USD(10'000),
                std::nullopt,
                tfTwoAssetIfEmpty,
                std::nullopt,
                std::nullopt,
                1'000);
            BEAST_EXPECT(
                amm.expectBalances(XRP(10'000), USD(10'000), amm.tokens()));
            BEAST_EXPECT(amm.expectTradingFee(1'000));
            BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{0}));

            // Withdrawing all tokens deletes AMM since the number
            // of remaining trustlines is less than max
            amm.withdrawAll(alice);
            BEAST_EXPECT(!amm.ammExists());
            BEAST_EXPECT(!env.le(keylet::ownerDir(amm.ammAccount())));
        }

        {
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.reference_fee = XRPAmount(1);
                    return cfg;
                }),
                all);
            fund(env, gw, {alice}, XRP(20'000), {USD(10'000)});
            AMM amm(env, gw, XRP(10'000), USD(10'000));
            for (auto i = 0; i < maxDeletableAMMTrustLines * 2 + 10; ++i)
            {
                Account const a{std::to_string(i)};
                env.fund(XRP(1'000), a);
                env(trust(a, STAmount{amm.lptIssue(), 10'000}));
                env.close();
            }
            // The trustlines are partially deleted.
            amm.withdrawAll(gw);
            BEAST_EXPECT(amm.ammExists());

            // AMMDelete has to be called twice to delete AMM.
            amm.ammDelete(alice, ter(tecINCOMPLETE));
            BEAST_EXPECT(amm.ammExists());
            // Deletes remaining trustlines and deletes AMM.
            amm.ammDelete(alice);
            BEAST_EXPECT(!amm.ammExists());
            BEAST_EXPECT(!env.le(keylet::ownerDir(amm.ammAccount())));
        }
    }

    void
    testClawback()
    {
        testcase("Clawback");
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(2'000), gw);
        env.fund(XRP(2'000), alice);
        AMM amm(env, gw, XRP(1'000), USD(1'000));
        env(fset(gw, asfAllowTrustLineClawback), ter(tecOWNERS));
    }

    void
    testAMMID()
    {
        testcase("AMMID");
        using namespace jtx;
        testAMM([&](AMM& amm, Env& env) {
            amm.setClose(false);
            auto const info = env.rpc(
                "json",
                "account_info",
                std::string(
                    "{\"account\": \"" + to_string(amm.ammAccount()) + "\"}"));
            try
            {
                BEAST_EXPECT(
                    info[jss::result][jss::account_data][jss::AMMID]
                        .asString() == to_string(amm.ammID()));
            }
            catch (...)
            {
                fail();
            }
            amm.deposit(carol, 1'000);
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            try
            {
                bool found = false;
                for (auto const& node : affected)
                {
                    if (node.isMember(sfModifiedNode.fieldName) &&
                        node[sfModifiedNode.fieldName]
                            [sfLedgerEntryType.fieldName]
                                .asString() == "AccountRoot" &&
                        node[sfModifiedNode.fieldName][sfFinalFields.fieldName]
                            [jss::Account]
                                .asString() == to_string(amm.ammAccount()))
                    {
                        found = node[sfModifiedNode.fieldName]
                                    [sfFinalFields.fieldName][jss::AMMID]
                                        .asString() == to_string(amm.ammID());
                        break;
                    }
                }
                BEAST_EXPECT(found);
            }
            catch (...)
            {
                fail();
            }
        });
    }

    void
    testSelection()
    {
        testcase("Offer/Strand Selection");
        using namespace jtx;
        Account const ed("ed");
        Account const gw1("gw1");
        auto const ETH = gw1["ETH"];
        auto const CAN = gw1["CAN"];

        // These tests are expected to fail if the OwnerPaysFee feature
        // is ever supported. Updates will need to be made to AMM handling
        // in the payment engine, and these tests will need to be updated.

        auto prep = [&](Env& env, auto gwRate, auto gw1Rate) {
            fund(env, gw, {alice, carol, bob, ed}, XRP(2'000), {USD(2'000)});
            env.fund(XRP(2'000), gw1);
            fund(
                env,
                gw1,
                {alice, carol, bob, ed},
                {ETH(2'000), CAN(2'000)},
                Fund::IOUOnly);
            env(rate(gw, gwRate));
            env(rate(gw1, gw1Rate));
            env.close();
        };

        for (auto const& rates :
             {std::make_pair(1.5, 1.9), std::make_pair(1.9, 1.5)})
        {
            // Offer Selection

            // Cross-currency payment: AMM has the same spot price quality
            // as CLOB's offer and can't generate a better quality offer.
            // The transfer fee in this case doesn't change the CLOB quality
            // because trIn is ignored on adjustment and trOut on payment is
            // also ignored because ownerPaysTransferFee is false in this case.
            // Run test for 0) offer, 1) AMM, 2) offer and AMM
            // to verify that the quality is better in the first case,
            // and CLOB is selected in the second case.
            {
                std::array<Quality, 3> q;
                for (auto i = 0; i < 3; ++i)
                {
                    Env env(*this);
                    prep(env, rates.first, rates.second);
                    std::optional<AMM> amm;
                    if (i == 0 || i == 2)
                    {
                        env(offer(ed, ETH(400), USD(400)), txflags(tfPassive));
                        env.close();
                    }
                    if (i > 0)
                        amm.emplace(env, ed, USD(1'000), ETH(1'000));
                    env(pay(carol, bob, USD(100)),
                        path(~USD),
                        sendmax(ETH(500)));
                    env.close();
                    // CLOB and AMM, AMM is not selected
                    if (i == 2)
                    {
                        BEAST_EXPECT(amm->expectBalances(
                            USD(1'000), ETH(1'000), amm->tokens()));
                    }
                    BEAST_EXPECT(expectLine(env, bob, USD(2'100)));
                    q[i] = Quality(Amounts{
                        ETH(2'000) - env.balance(carol, ETH),
                        env.balance(bob, USD) - USD(2'000)});
                }
                // CLOB is better quality than AMM
                BEAST_EXPECT(q[0] > q[1]);
                // AMM is not selected with CLOB
                BEAST_EXPECT(q[0] == q[2]);
            }
            // Offer crossing: AMM has the same spot price quality
            // as CLOB's offer and can't generate a better quality offer.
            // The transfer fee in this case doesn't change the CLOB quality
            // because the quality adjustment is ignored for the offer crossing.
            for (auto i = 0; i < 3; ++i)
            {
                Env env(*this);
                prep(env, rates.first, rates.second);
                std::optional<AMM> amm;
                if (i == 0 || i == 2)
                {
                    env(offer(ed, ETH(400), USD(400)), txflags(tfPassive));
                    env.close();
                }
                if (i > 0)
                    amm.emplace(env, ed, USD(1'000), ETH(1'000));
                env(offer(alice, USD(400), ETH(400)));
                env.close();
                // AMM is not selected
                if (i > 0)
                {
                    BEAST_EXPECT(amm->expectBalances(
                        USD(1'000), ETH(1'000), amm->tokens()));
                }
                if (i == 0 || i == 2)
                {
                    // Fully crosses
                    BEAST_EXPECT(expectOffers(env, alice, 0));
                }
                // Fails to cross because AMM is not selected
                else
                {
                    BEAST_EXPECT(expectOffers(
                        env, alice, 1, {Amounts{USD(400), ETH(400)}}));
                }
                BEAST_EXPECT(expectOffers(env, ed, 0));
            }

            // Show that the CLOB quality reduction
            // results in AMM offer selection.

            // Same as the payment but reduced offer quality
            {
                std::array<Quality, 3> q;
                for (auto i = 0; i < 3; ++i)
                {
                    Env env(*this);
                    prep(env, rates.first, rates.second);
                    std::optional<AMM> amm;
                    if (i == 0 || i == 2)
                    {
                        env(offer(ed, ETH(400), USD(300)), txflags(tfPassive));
                        env.close();
                    }
                    if (i > 0)
                        amm.emplace(env, ed, USD(1'000), ETH(1'000), false, 1);
                    env(pay(carol, bob, USD(100)),
                        path(~USD),
                        sendmax(ETH(500)));
                    env.close();
                    // AMM and CLOB are selected
                    if (i > 0)
                    {
                        BEAST_EXPECT(!amm->expectBalances(
                            USD(1'000), ETH(1'000), amm->tokens()));
                    }
                    if (i == 2)
                    {
                        if (rates.first == 1.5)
                        {
                            BEAST_EXPECT(expectOffers(
                                env,
                                ed,
                                1,
                                {{Amounts{
                                    STAmount{
                                        ETH, UINT64_C(378'6320214472632), -13},
                                    STAmount{
                                        USD,
                                        UINT64_C(283'9740160854474),
                                        -13}}}}));
                        }
                        else
                        {
                            BEAST_EXPECT(expectOffers(
                                env,
                                ed,
                                1,
                                {{Amounts{
                                    STAmount{
                                        ETH, UINT64_C(325'2986881139299), -13},
                                    STAmount{
                                        USD,
                                        UINT64_C(243'9740160854474),
                                        -13}}}}));
                        }
                    }
                    BEAST_EXPECT(expectLine(env, bob, USD(2'100)));
                    q[i] = Quality(Amounts{
                        ETH(2'000) - env.balance(carol, ETH),
                        env.balance(bob, USD) - USD(2'000)});
                }
                // AMM is better quality
                BEAST_EXPECT(q[1] > q[0]);
                // AMM and CLOB produce better quality
                BEAST_EXPECT(q[2] > q[1]);
            }

            // Same as the offer-crossing but reduced offer quality
            for (auto i = 0; i < 3; ++i)
            {
                Env env(*this);
                prep(env, rates.first, rates.second);
                std::optional<AMM> amm;
                if (i == 0 || i == 2)
                {
                    env(offer(ed, ETH(400), USD(255)), txflags(tfPassive));
                    env.close();
                }
                if (i > 0)
                    amm.emplace(env, ed, USD(1'000), ETH(1'000), false, 1);
                env(offer(alice, USD(255), ETH(400)));
                env.close();
                // AMM is selected in both cases
                if (i > 0)
                {
                    BEAST_EXPECT(!amm->expectBalances(
                        USD(1'000), ETH(1'000), amm->tokens()));
                }
                // Partially crosses, AMM is selected, CLOB fails limitQuality
                if (i == 2)
                {
                    if (rates.first == 1.5)
                    {
                        // Ed offer is partially crossed.
                        BEAST_EXPECT(expectOffers(
                            env,
                            ed,
                            1,
                            {{Amounts{
                                STAmount{ETH, UINT64_C(316'1776065602351), -13},
                                STAmount{
                                    USD, UINT64_C(201'5632241821499), -13}}}}));
                        BEAST_EXPECT(expectOffers(env, alice, 0));
                    }
                    else
                    {
                        BEAST_EXPECT(expectOffers(
                            env, ed, 1, {{Amounts{ETH(400), USD(255)}}}));
                        // Alice offer is partially crossed
                        BEAST_EXPECT(expectOffers(
                            env,
                            alice,
                            1,
                            {{Amounts{
                                STAmount{USD, UINT64_C(53'4367758178501), -13},
                                STAmount{
                                    ETH, UINT64_C(83'82239343976486), -14}}}}));
                    }
                }
            }

            // Strand selection

            // Two book steps strand quality is 1.
            // AMM strand's best quality is equal to AMM's spot price
            // quality, which is 1. Both strands (steps) are adjusted
            // for the transfer fee in qualityUpperBound. In case
            // of two strands, AMM offers have better quality and are consumed
            // first, remaining liquidity is generated by CLOB offers.
            // Liquidity from two strands is better in this case than in case
            // of one strand with two book steps. Liquidity from one strand
            // with AMM has better quality than either one strand with two book
            // steps or two strands. It may appear unintuitive, but one strand
            // with AMM is optimized and generates one AMM offer, while in case
            // of two strands, multiple AMM offers are generated, which results
            // in slightly worse overall quality in case of 1.5 transfer rate.
            {
                std::array<Quality, 3> q;
                for (auto i = 0; i < 3; ++i)
                {
                    Env env(*this);
                    prep(env, rates.first, rates.second);
                    std::optional<AMM> amm;

                    if (i == 0 || i == 2)
                    {
                        env(offer(ed, ETH(400), CAN(400)), txflags(tfPassive));
                        env(offer(ed, CAN(400), USD(400))), txflags(tfPassive);
                        env.close();
                    }

                    if (i > 0)
                        amm.emplace(env, ed, ETH(1'000), USD(1'000), false, 1);

                    env(pay(carol, bob, USD(100)),
                        path(~USD),
                        path(~CAN, ~USD),
                        sendmax(ETH(600)));
                    env.close();

                    if (i == 2 && rates.first == 1.9)
                        // round-off
                        BEAST_EXPECT(expectLine(
                            env,
                            bob,
                            STAmount{USD, UINT64_C(2'099'999999999999), -12}));
                    else
                        BEAST_EXPECT(expectLine(env, bob, USD(2'100)));

                    if (i == 2)
                    {
                        if (rates.first == 1.5)
                        {
                            // Liquidity is consumed from AMM strand only
                            BEAST_EXPECT(amm->expectBalances(
                                STAmount{
                                    ETH, UINT64_C(1'178'236492329882), -12},
                                USD(850),
                                amm->tokens()));
                        }
                        else
                        {
                            BEAST_EXPECT(amm->expectBalances(
                                STAmount{
                                    ETH, UINT64_C(1'215'211566809362), -12},
                                STAmount{USD, UINT64_C(822'90354194687), -11},
                                amm->tokens()));
                            BEAST_EXPECT(expectOffers(
                                env,
                                ed,
                                2,
                                {{Amounts{
                                      STAmount{
                                          ETH, UINT64_C(380'644687079695), -12},
                                      STAmount{
                                          CAN, UINT64_C(380'644687079695), -12},
                                  },
                                  Amounts{
                                      STAmount{
                                          CAN, UINT64_C(387'09645805313), -11},
                                      STAmount{
                                          USD, UINT64_C(387'09645805313), -11},
                                  }}}));
                        }
                    }
                    q[i] = Quality(Amounts{
                        ETH(2'000) - env.balance(carol, ETH),
                        env.balance(bob, USD) - USD(2'000)});
                }
                BEAST_EXPECT(q[1] > q[0]);
                if (rates.first == 1.5)
                    BEAST_EXPECT(q[2] > q[0] && q[2] < q[1]);
                else
                    BEAST_EXPECT(q[2] > q[0] && q[2] > q[1]);
            }
        }
    }

    void
    testCore()
    {
        testInvalidInstance();
        testInstanceCreate();
        testInvalidDeposit();
        testDeposit();
        testInvalidWithdraw();
        testWithdraw();
        testInvalidFeeVote();
        testFeeVote();
        testInvalidBid();
        testBid();
        testInvalidAMMPayment();
        testBasicPaymentEngine();
        testAMMTokens();
        testAmendment();
        testFlags();
        testRippling();
        testAMMAndCLOB();
        testTradingFee();
        testAdjustedTokens();
        testAutoDelete();
        testClawback();
        testAMMID();
        testSelection();
    }

    void
    run() override
    {
        testCore();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMM, app, ripple, 1);

}  // namespace test
}  // namespace ripple
