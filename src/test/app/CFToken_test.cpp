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

namespace ripple {

class CFToken_test : public beast::unit_test::suite
{
    void
    testEnabled(FeatureBitset features)
    {
        testcase("Enabled");
        using namespace test::jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        auto const USDCFT = gw("USD");
        auto const USD = gw["USD"];

        {
            Env env(*this);
            env.fund(XRP(1000), gw);
            env.fund(XRP(1000), alice);
            env.fund(XRP(1000), carol);
            env(trust(alice, USD(30'000)));
            env(pay(gw, alice, USD(10'000)));
            env(trust(carol, USD(30'000)));
            env(pay(gw, carol, USD(10'000)));
            env(pay(alice, carol, USD(100)));
            env(offer(alice, XRP(100), USD(100)));
        }
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

        {
            // If the CFT amendment is not enabled, you should not be able to
            // specify an amount for a Payment using CFT amount notation
        }

        {
            // If the CFT amendment IS enabled, you should be able to make a
            // CFT Payment that doesn't cross
            Env env{*this, features | featureCFTokensV1};
            env.fund(XRP(10000), gw, alice, carol);
            env.close();

            CFTIssuance cft(env, gw, USD.currency);

            cft.cftrust(alice);
            env(pay(gw, alice, USDCFT(200)));
            env.close();
            BEAST_EXPECT(cft.holderAmount(alice) == 200);

            cft.cftrust(carol);
            env(pay(gw, carol, USDCFT(200)));
            env.close();
            BEAST_EXPECT(cft.holderAmount(carol) == 200);
            BEAST_EXPECT(cft.outstandingAmount() == 400);

            env(offer(alice, XRP(100), USDCFT(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{XRP(100), USDCFT(101)}}}));

            // Offer crossing
            env(offer(carol, USDCFT(101), XRP(100)));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(cft.outstandingAmount() == 400);
            BEAST_EXPECT(cft.holderAmount(alice) == 99);
            BEAST_EXPECT(cft.holderAmount(carol) == 301);
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};

        testEnabled(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CFToken, tx, ripple, 2);

}  // namespace ripple
