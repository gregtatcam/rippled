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
        {
            Env env(*this);
            Account const gw = Account("gw");
            Account const alice = Account("alice");
            Account const carol = Account("carol");
            auto const USD = gw["USD"];
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
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            env(cft::create(master, "USD"), ter(temDISABLED));
            env.close();

            BEAST_EXPECT(env.ownerCount(master) == 0);
        }
        {
            // If the CFT amendment IS enabled, you should be able to create
            // CFTokenIssuances
            Env env{*this, features | featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            env(cft::create(master, "USD"));
            env.close();

            BEAST_EXPECT(env.ownerCount(master) == 1);
        }
        {
            // If the CFT amendment is not enabled, you should not be able to
            // destroy CFTokenIssuances
            Env env{*this, features - featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            auto const id =
                keylet::cftIssuance(master.id(), to_currency("USD"));
            env(cft::destroy(master, ripple::to_string(id.key)),
                ter(temDISABLED));
            env.close();

            BEAST_EXPECT(env.ownerCount(master) == 0);
        }
        {
            // If the CFT amendment IS enabled, you should be able to destroy
            // CFTokenIssuances
            Env env{*this, features | featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            env(cft::create(master, "USD"));
            env.close();
            BEAST_EXPECT(env.ownerCount(master) == 1);

            auto const id =
                keylet::cftIssuance(master.id(), to_currency("USD"));

            env(cft::destroy(master, ripple::to_string(id.key)));
            env.close();
            BEAST_EXPECT(env.ownerCount(master) == 0);
        }
        {
            // If the CFT amendment is not enabled, you should not be able to
            // destroy CFTokenIssuances
            Env env{*this, features - featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            // TODO why not working with short codes?
            auto const id =
                keylet::cftIssuance(master.id(), to_currency("USD"));
            env(cft::destroy(master, ripple::to_string(id.key)),
                ter(temDISABLED));
            env.close();

            BEAST_EXPECT(env.ownerCount(master) == 0);
        }
        {
            // If the CFT amendment IS enabled, you should be able to destroy
            // CFTokenIssuances
            Env env{*this, features | featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            env(cft::create(master, "USD"));
            env.close();
            BEAST_EXPECT(env.ownerCount(master) == 1);

            auto const id =
                keylet::cftIssuance(master.id(), to_currency("USD"));

            env(cft::destroy(master, ripple::to_string(id.key)));
            env.close();
            BEAST_EXPECT(env.ownerCount(master) == 0);
        }

        {
            // If the CFT amendment is not enabled, you should not be able to
            // specify an amount for a Payment using CFT amount notation
        }

        {
            // If the CFT amendment IS enabled, you should be able to make a
            // CFT Payment that doesn't cross
            Env env{*this, features | featureCFTokensV1};
            Account const gw("gw");
            Account const alice("alice");
            Account const carol("carol");
            env.fund(XRP(10000), gw, alice);
            env.close();

            // TODO why not working with short codes?
            env(cft::create(gw, "USD"));
            env.close();

            BEAST_EXPECT(env.ownerCount(gw) == 1);

            Json::Value amt;
            amt["issuer"] = gw.human();
            amt["cft_asset"] = "USD";
            amt["value"] = "1";
            STAmount const yo = amountFromJson(sfAmount, amt);
            auto const id = keylet::cftIssuance(gw.id(), to_currency("USD"));
            env(cft::cftrust(alice, ripple::to_string(id.key)));
            env.close();
            BEAST_EXPECT(env.ownerCount(alice) == 1);
            env(pay(gw, alice, yo));
            env.close();
            auto const sle = env.le(keylet::cftoken(alice.id(), id.key));
            BEAST_EXPECT(sle->getFieldU64(sfCFTAmount) == 1);
            env(offer(alice, XRP(100), USD(100)));
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
