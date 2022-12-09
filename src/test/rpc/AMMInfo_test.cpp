//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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
#include <test/jtx/AMMTest.h>

#include <unordered_map>

namespace ripple {
namespace test {

class AMMInfo_test : public jtx::AMMTest
{
public:
    AMMInfo_test() : jtx::AMMTest()
    {
    }
    void
    testErrors()
    {
        testcase("Errors");

        using namespace jtx;
        // Invalid tokens pair
        testAMM([&](AMM& ammAlice, Env&) {
            Account const gw("gw");
            auto const USD = gw["USD"];
            auto const jv =
                ammAlice.ammRpcInfo({}, {}, {{USD.issue(), USD.issue()}});
            BEAST_EXPECT(
                jv.has_value() &&
                (*jv)[jss::error_message] == "Account not found.");
        });

        // Invalid LP account id
        testAMM([&](AMM& ammAlice, Env&) {
            Account bogie("bogie");
            auto const jv = ammAlice.ammRpcInfo(bogie.id());
            BEAST_EXPECT(
                jv.has_value() &&
                (*jv)[jss::error_message] == "Account malformed.");
        });
    }

    void
    testSimpleRpc()
    {
        testcase("RPC simple");

        using namespace jtx;
        testAMM([&](AMM& ammAlice, Env&) {
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });
    }

    void
    testVoteAndBid()
    {
        testcase("Vote and Bid");

        using namespace jtx;
        testAMM([&](AMM& ammAlice, Env& env) {
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            std::unordered_map<std::string, std::uint16_t> votes;
            for (int i = 0; i < 8; ++i)
            {
                Account a(std::to_string(i));
                votes.insert({a.human(), 50 * (i + 1)});
                fund(env, gw, {a}, {USD(1000)}, Fund::Acct);
                ammAlice.deposit(a, 10000);
                ammAlice.vote(a, 50 * (i + 1));
            }
            BEAST_EXPECT(ammAlice.expectTradingFee(225));
            Account ed("ed");
            Account bill("bill");
            env.fund(XRP(1000), bob, ed, bill);
            ammAlice.bid(alice, 100, std::nullopt, {carol, bob, ed, bill});
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10080), USD(10080), IOUAmount{10079900}));
            std::unordered_set<std::string> authAccounts = {
                carol.human(), bob.human(), ed.human(), bill.human()};
            auto const ammInfo = ammAlice.ammRpcInfo();
            auto const expectAmmInfo = [&](Json::Value const& amm) {
                try
                {
                    // votes
                    auto const voteSlots = amm[jss::vote_slots];
                    for (std::uint8_t i = 0; i < 8; ++i)
                    {
                        if (votes[voteSlots[i][jss::account].asString()] !=
                                voteSlots[i][jss::trading_fee].asUInt() ||
                            voteSlots[i][jss::vote_weight].asUInt() != 99)
                            return false;
                        votes.erase(voteSlots[i][jss::account].asString());
                    }
                    if (!votes.empty())
                        return false;

                    // bid
                    auto const auctionSlot = amm[jss::auction_slot];
                    for (std::uint8_t i = 0; i < 4; ++i)
                    {
                        if (!authAccounts.contains(
                                auctionSlot[jss::auth_accounts][i][jss::account]
                                    .asString()))
                            return false;
                        authAccounts.erase(
                            auctionSlot[jss::auth_accounts][i][jss::account]
                                .asString());
                    }
                    if (!authAccounts.empty())
                        return false;
                    return auctionSlot[jss::account].asString() ==
                        alice.human() &&
                        auctionSlot[jss::discounted_fee].asUInt() == 0 &&
                        auctionSlot[jss::price][jss::value].asString() ==
                        "100" &&
                        auctionSlot[jss::price][jss::currency].asString() ==
                        to_string(ammAlice.lptIssue().currency) &&
                        auctionSlot[jss::price][jss::issuer].asString() ==
                        to_string(ammAlice.lptIssue().account);
                }
                catch (...)
                {
                    return false;
                }
            };
            BEAST_EXPECT(ammInfo && expectAmmInfo((*ammInfo)["amm"]));
        });
    }

    void
    testFreeze()
    {
        using namespace jtx;
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            auto test = [&](bool freeze) {
                auto const info = ammAlice.ammRpcInfo();
                BEAST_EXPECT(
                    info &&
                    (*info)[jss::amm][jss::asset2_frozen].asBool() == freeze);
            };
            test(true);
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            test(false);
        });
    }

    void
    run() override
    {
        testErrors();
        testSimpleRpc();
        testVoteAndBid();
        testFreeze();
    }
};

BEAST_DEFINE_TESTSUITE(AMMInfo, app, ripple);

}  // namespace test
}  // namespace ripple
