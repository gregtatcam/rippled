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
#include <test/jtx/Oracle.h>

namespace ripple {
namespace test {

class GetAggregatePrice_test : public beast::unit_test::suite
{
public:
    void
    testErrors()
    {
        testcase("Errors");
        using namespace jtx;
        Env env(*this);
        Account const owner{"owner"};
        Account const some{"some"};
        static std::vector<std::pair<AccountID, std::uint32_t>> oracles = {
            {owner, 1}};

        // missing symbol
        auto ret = Oracle::aggregatePrice(env, std::nullopt, "USD", oracles);
        BEAST_EXPECT(
            ret[jss::error_message].asString() == "Missing field 'symbol'.");

        // missing price_unit
        ret = Oracle::aggregatePrice(env, "XRP", std::nullopt, oracles);
        BEAST_EXPECT(
            ret[jss::error_message].asString() ==
            "Missing field 'price_unit'.");

        // missing oracles array
        ret = Oracle::aggregatePrice(env, "XRP", "USD");
        BEAST_EXPECT(
            ret[jss::error_message].asString() == "Missing field 'oracles'.");

        // empty oracles array
        ret = Oracle::aggregatePrice(env, "XRP", "USD", {{}});
        BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

        // invalid oracle sequence
        ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, 2}}});
        BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");

        // invalid owner
        ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{some, 1}}});
        BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");

        // oracles have wrong asset pair
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, owner, 1, {{"XRP", "EUR", 740, 1}}, ter(tesSUCCESS));
        Oracle oracle1(
            env, owner, 2, {{"XRP", "USD", 740, 1}}, ter(tesSUCCESS));
        ret = Oracle::aggregatePrice(
            env,
            "XRP",
            "USD",
            {{{owner, oracle.oracleSequence()},
              {owner, oracle1.oracleSequence()}}});
        BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");
    }

    void
    testRpc()
    {
        testcase("RPC");
        using namespace jtx;

        {
            Env env(*this);
            std::vector<std::pair<AccountID, std::uint32_t>> oracles;
            oracles.reserve(10);
            for (int i = 0; i < 10; ++i)
            {
                Account const owner{std::to_string(i)};
                env.fund(XRP(1'000), owner);
                Oracle oracle(
                    env,
                    owner,
                    rand(),
                    {{"XRP", "USD", 740 + i, 1}},
                    ter(tesSUCCESS));
                oracles.emplace_back(owner.id(), oracle.oracleSequence());
            }
            // simple average
            auto ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles, 20);
            BEAST_EXPECT(ret[jss::simple_average] == "74.45");
            BEAST_EXPECT(ret[jss::median] == "74.45");
            BEAST_EXPECT(ret[jss::trimmed_mean] == "74.45");
            BEAST_EXPECT(ret[jss::size].asUInt() == 10);
            BEAST_EXPECT(ret[jss::standard_deviation] == "0.9082951062292475");
        }
        BEAST_EXPECT(true);
    }

    void
    run() override
    {
        testErrors();
        testRpc();
    }
};

BEAST_DEFINE_TESTSUITE(GetAggregatePrice, app, ripple);

}  // namespace test
}  // namespace ripple
