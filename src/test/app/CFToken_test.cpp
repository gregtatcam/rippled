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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/AMM.h>

namespace ripple {
namespace test {

class CFToken_test : public beast::unit_test::suite
{
    void
    testBasic(FeatureBitset features)
    {
        testcase("Basic");
        using namespace jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        Account const bob = Account("bob");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        {
            // If the CFT amendment is not enabled, you should not be able to
            // create CFTokenIssuances
            Env env{*this, features - featureCFTokensV1};

            CFTIssuance cft(env, env.master, USD.currency, ter(temDISABLED));
        }

        {
            // If the CFT amendment IS enabled, you should be able to create
            // CFTokenIssuances
            Env env{*this, features | featureCFTokensV1};

            CFTIssuance cft(env, env.master, USD.currency);
        }

        {
            // If the CFT amendment is not enabled, you should not be able to
            // destroy CFTokenIssuances
            Env env{*this, features - featureCFTokensV1};
            CFTIssuance cft(env);
            cft.destroy(env.master, uint256{0}, ter(temDISABLED));
        }

        {
            // If the CFT amendment IS enabled, you should be able to destroy
            // CFTokenIssuances
            Env env{*this, features | featureCFTokensV1};
            CFTIssuance cft(env, env.master, USD.currency);
            cft.destroy();
        }
    }

    void
    testOfferCrossing(FeatureBitset features)
    {
        testcase("Offer Crossing");
        using namespace jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        Account const bob = Account("bob");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        // XRP/CFT offer crossing
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10000), gw, alice, carol);
            env.close();

            CFTIssuance cft(env, gw, USD.currency);

            cft.cftrust(alice);
            env(pay(gw, alice, cft.cft(200)));
            env.close();
            BEAST_EXPECT(cft.holderAmount(alice) == 200);

            cft.cftrust(carol);
            env(pay(gw, carol, cft.cft(200)));
            env.close();
            BEAST_EXPECT(cft.holderAmount(carol) == 200);
            BEAST_EXPECT(cft.outstandingAmount() == 400);

            env(offer(alice, XRP(100), cft.cft(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{XRP(100), cft.cft(101)}}}));

            // Offer crossing
            env(offer(carol, cft.cft(101), XRP(100)));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(cft.outstandingAmount() == 400);
            BEAST_EXPECT(cft.holderAmount(alice) == 99);
            BEAST_EXPECT(cft.holderAmount(carol) == 301);
        }

        // USD/CFT offer crossing
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10000), gw, alice, carol);
            env.close();

            env(trust(alice, EUR(30'000)));
            env(pay(gw, alice, EUR(10'000)));
            env.close();

            env(trust(carol, EUR(30'000)));
            env(pay(gw, carol, EUR(10'000)));
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);

            cftUsd.cftrust(alice);
            env(pay(gw, alice, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 200);

            cftUsd.cftrust(carol);
            env(pay(gw, carol, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 200);
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);

            env(offer(alice, EUR(100), cftUsd.cft(101)));
            env.close();

            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{EUR(100), cftUsd.cft(101)}}}));

            env(offer(carol, cftUsd.cft(101), EUR(100)));
            env.close();

            BEAST_EXPECT(env.balance(alice, EUR) == EUR(10100));
            BEAST_EXPECT(env.balance(carol, EUR) == EUR(9900));
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 99);
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 301);
        }

        // CFT/CFT offer crossing
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10000), gw, alice, carol);
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);
            CFTIssuance cftEur(env, gw, EUR.currency);

            cftUsd.cftrust(alice);
            cftEur.cftrust(alice);
            env(pay(gw, alice, cftUsd.cft(200)));
            env(pay(gw, alice, cftEur.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 200);
            BEAST_EXPECT(cftEur.holderAmount(alice) == 200);

            cftUsd.cftrust(carol);
            cftEur.cftrust(carol);
            env(pay(gw, carol, cftUsd.cft(200)));
            env(pay(gw, carol, cftEur.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 200);
            BEAST_EXPECT(cftEur.holderAmount(carol) == 200);
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);
            BEAST_EXPECT(cftEur.outstandingAmount() == 400);

            env(offer(alice, cftEur.cft(100), cftUsd.cft(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{cftEur.cft(100), cftUsd.cft(101)}}}));

            env(offer(carol, cftUsd.cft(101), cftEur.cft(100)));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 99);
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 301);
            BEAST_EXPECT(cftEur.outstandingAmount() == 400);
            BEAST_EXPECT(cftEur.holderAmount(alice) == 300);
            BEAST_EXPECT(cftEur.holderAmount(carol) == 100);
        }
    }

    void
    testPayments(FeatureBitset features)
    {
        testcase("Payments");
        using namespace jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        Account const bob = Account("bob");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        // CFT/XRP cross asset payment
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10000), gw, alice, carol, bob);
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);

            cftUsd.cftrust(alice);
            env(pay(gw, alice, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 200);

            cftUsd.cftrust(carol);
            env(pay(gw, carol, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 200);
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);

            cftUsd.cftrust(bob);

            env(offer(alice, XRP(100), cftUsd.cft(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{XRP(100), cftUsd.cft(101)}}}));

            env(pay(carol, bob, cftUsd.cft(101)),
                jtx::path(~cftUsd.cft()),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 99);
            BEAST_EXPECT(cftUsd.holderAmount(bob) == 101);
        }

        // CFT/IOU cross asset payment
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10000), gw, alice, carol, bob);
            env(trust(alice, EUR(30'000)));
            env(pay(gw, alice, EUR(10'000)));
            env(trust(bob, EUR(30'000)));
            env(pay(gw, bob, EUR(10'000)));
            env(trust(carol, EUR(30'000)));
            env(pay(gw, carol, EUR(10'000)));
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);

            cftUsd.cftrust(alice);
            env(pay(gw, alice, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 200);

            cftUsd.cftrust(carol);
            env(pay(gw, carol, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 200);
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);

            cftUsd.cftrust(bob);

            env(offer(alice, EUR(100), cftUsd.cft(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{EUR(100), cftUsd.cft(101)}}}));

            env(pay(carol, bob, cftUsd.cft(101)),
                jtx::path(~cftUsd.cft()),
                sendmax(EUR(100)),
                txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(env.balance(carol, EUR) == EUR(9900));
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 99);
            BEAST_EXPECT(cftUsd.holderAmount(bob) == 101);
        }

        // IOU/CFT cross asset payment
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10'000), gw);
            env.fund(XRP(10'000), alice);
            env.fund(XRP(10'000), carol);
            env.fund(XRP(10'000), bob);
            env(trust(alice, EUR(30'000)), txflags(tfClearNoRipple));
            env(pay(gw, alice, EUR(10'000)));
            env(trust(bob, EUR(30'000)), txflags(tfClearNoRipple));
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);

            cftUsd.cftrust(alice);
            env(pay(gw, alice, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 200);

            cftUsd.cftrust(carol);
            env(pay(gw, carol, cftUsd.cft(200)));
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 200);
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);

            env(offer(alice, cftUsd.cft(101), EUR(100)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{cftUsd.cft(101), EUR(100)}}}));

            env(pay(carol, bob, EUR(100)),
                jtx::path(~EUR),
                sendmax(cftUsd.cft(101)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(env.balance(alice, EUR) == EUR(9900));
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 301);
            BEAST_EXPECT(cftUsd.outstandingAmount() == 400);
            BEAST_EXPECT(cftUsd.holderAmount(carol) == 99);
            BEAST_EXPECT(env.balance(bob, EUR) == EUR(100));
        }

        // CFT/CFT cross asset payment
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10000), gw, alice, carol, bob);
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);
            CFTIssuance cftEur(env, gw, EUR.currency);

            cftUsd.cftrust(alice);
            env(pay(gw, alice, cftUsd.cft(200)));
            cftEur.cftrust(alice);
            env.close();
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 200);

            cftEur.cftrust(carol);
            env(pay(gw, carol, cftEur.cft(200)));
            env.close();
            BEAST_EXPECT(cftEur.holderAmount(carol) == 200);
            BEAST_EXPECT(cftUsd.outstandingAmount() == 200);

            cftUsd.cftrust(bob);

            env(offer(alice, cftEur.cft(100), cftUsd.cft(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{cftEur.cft(100), cftUsd.cft(101)}}}));

            env(pay(carol, bob, cftUsd.cft(101)),
                jtx::path(~cftUsd.cft()),
                sendmax(cftEur.cft(100)),
                txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(cftUsd.outstandingAmount() == 200);
            BEAST_EXPECT(cftEur.holderAmount(alice) == 100);
            BEAST_EXPECT(cftUsd.holderAmount(alice) == 99);
            BEAST_EXPECT(cftUsd.holderAmount(bob) == 101);
        }

        // XRP/CFT AMM cross-asset payment
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(20'000), gw, alice, carol, bob);
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);
            cftUsd.cftrust(alice);
            cftUsd.cftrust(bob);
            env(pay(gw, alice, cftUsd.cft(10'100)));
            env.close();

            AMM amm(env, alice, XRP(10'000), cftUsd.cft(10'100));

            env(pay(carol, bob, cftUsd.cft(100)),
                jtx::path(~cftUsd.cft()),
                sendmax(XRP(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                XRP(10'100), cftUsd.cft(10'000), amm.tokens()));
            BEAST_EXPECT(cftUsd.holderAmount(bob) == 100);
        }

        // IOU/CFT AMM cross-asset payment
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(20'000), gw, alice, carol, bob);
            env.close();

            env(trust(alice, EUR(30'000)));
            env(trust(carol, EUR(30'000)));
            env(pay(gw, alice, EUR(10'000)));
            env(pay(gw, carol, EUR(10'000)));
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);
            cftUsd.cftrust(alice);
            cftUsd.cftrust(bob);
            env(pay(gw, alice, cftUsd.cft(10'100)));
            env.close();

            AMM amm(env, alice, EUR(10'000), cftUsd.cft(10'100));

            env(pay(carol, bob, cftUsd.cft(100)),
                jtx::path(~cftUsd.cft()),
                sendmax(EUR(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                EUR(10'100), cftUsd.cft(10'000), amm.tokens()));
            BEAST_EXPECT(cftUsd.holderAmount(bob) == 100);
        }

        // CFT/CFT AMM cross-asset payment
        {
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(20'000), gw, alice, carol, bob);
            env.close();

            CFTIssuance cftUsd(env, gw, USD.currency);
            cftUsd.cftrust(alice);
            cftUsd.cftrust(bob);
            env(pay(gw, alice, cftUsd.cft(10'100)));
            env.close();

            CFTIssuance cftEur(env, gw, EUR.currency);
            cftEur.cftrust(alice);
            cftEur.cftrust(bob);
            cftEur.cftrust(carol);
            env(pay(gw, alice, cftEur.cft(10'100)));
            env(pay(gw, carol, cftEur.cft(100)));
            env.close();

            AMM amm(env, alice, cftEur.cft(10'000), cftUsd.cft(10'100));

            env(pay(carol, bob, cftUsd.cft(100)),
                jtx::path(~cftUsd.cft()),
                sendmax(cftEur.cft(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                cftEur.cft(10'100), cftUsd.cft(10'000), amm.tokens()));
            BEAST_EXPECT(cftUsd.holderAmount(bob) == 100);
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};

        testBasic(all);
        testOfferCrossing(all);
        testPayments(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CFToken, tx, ripple, 2);

}  // namespace test
}  // namespace ripple
