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
    static auto
    ledgerEntryOracle(jtx::Env& env, uint256 const& id)
    {
        Json::Value jvParams;
        jvParams[jss::oracle][jss::OracleID] = to_string(id);
        return env.rpc(
            "json", "ledger_entry", to_string(jvParams))[jss::result];
    }

public:
    void
    testErrors()
    {
        testcase("Errors");
        using namespace jtx;
        Env env(*this);

        // missing symbol
        auto ret = Oracle::aggregatePrice(
            env, std::nullopt, "USD", {{uint256{1}}}, std::nullopt, 0x01);
        BEAST_EXPECT(
            ret[jss::error_message].asString() == "Missing field 'symbol'.");

        // missing price_unit
        ret = Oracle::aggregatePrice(
            env, "XRP", std::nullopt, {{uint256{1}}}, std::nullopt, 0x01);
        BEAST_EXPECT(
            ret[jss::error_message].asString() ==
            "Missing field 'price_unit'.");

        // missing oracles array
        ret = Oracle::aggregatePrice(
            env, "XRP", "USD", std::nullopt, std::nullopt, 0x01);
        BEAST_EXPECT(
            ret[jss::error_message].asString() == "Missing field 'oracles'.");

        // empty oracles array
        ret =
            Oracle::aggregatePrice(env, "XRP", "USD", {{}}, std::nullopt, 0x01);
        BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

        // invalid flags
        ret = Oracle::aggregatePrice(
            env, "XRP", "USD", {{uint256{1}}}, std::nullopt, 0);
        BEAST_EXPECT(
            ret[jss::error_message].asString() == "Missing field 'flags'.");

        // trim set but not the flag
        ret =
            Oracle::aggregatePrice(env, "XRP", "USD", {{uint256{1}}}, 5, 0x01);
        BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");

        // flag set but not the trim
        ret = Oracle::aggregatePrice(
            env, "XRP", "USD", {{uint256{1}}}, std::nullopt, 0x04);
        BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");

        // invalid oracle id
        ret = Oracle::aggregatePrice(
            env, "XRP", "USD", {{uint256{1}}}, std::nullopt, 0x01);
        BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");

        // oracles have wrong asset pair
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, owner, "XRP", "EUR", "currency", "chainlink");
        Oracle oracle1(env, owner, "XRP", "USD", "currency", "chainlink");
        ret = Oracle::aggregatePrice(
            env,
            "XRP",
            "USD",
            {{oracle.oracleID(), oracle1.oracleID()}},
            std::nullopt,
            0x01);
        BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");
    }

    void
    testRpc()
    {
        testcase("RPC");
        using namespace jtx;

        {
            Env env(*this);
            std::vector<uint256> oracles;
            for (int i = 0; i < 10; ++i)
            {
                Account const owner{std::to_string(i)};
                env.fund(XRP(1'000), owner);
                Oracle oracle(
                    env, owner, "XRP", "USD", "currency", "chainlink");
                oracles.push_back(oracle.oracleID());
                oracle.update(owner, 740 + i, 1);
            }
            // simple average
            auto ret = Oracle::aggregatePrice(
                env, "XRP", "USD", oracles, std::nullopt, 0x01);
            BEAST_EXPECT(ret[jss::simple_average] == "74.45");
            // median
            ret = Oracle::aggregatePrice(
                env, "XRP", "USD", oracles, std::nullopt, 0x02);
            BEAST_EXPECT(ret[jss::median] == "74.45");
            // trimmed mean
            ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles, 20, 0x04);
            BEAST_EXPECT(ret[jss::trimmed_mean] == "74.45");
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
