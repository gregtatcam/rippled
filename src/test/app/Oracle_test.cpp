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

#include <test/jtx/Oracle.h>

namespace ripple {
namespace test {

struct Oracle_test : public beast::unit_test::suite
{
private:
    void
    testInvalidCreate()
    {
        testcase("Invalid Create");

        using namespace jtx;
        Account const owner("owner");

        // Insufficient reserve
        {
            Env env(*this);
            env.fund(env.current()->fees().accountReserve(0), owner);
            Oracle oracle(
                env,
                owner,
                "XRP",
                "USD",
                "currency",
                10,
                std::nullopt,
                0,
                ter(tecINSUFFICIENT_RESERVE));
        }

        Env env(*this);
        env.fund(XRP(1'000), owner);
        Oracle oracle(env);

        // Invalid flags
        oracle.create(
            tfSellNFToken,
            owner,
            "XRP",
            "USD",
            "currency",
            std::nullopt,
            std::nullopt,
            10,
            std::nullopt,
            std::nullopt,
            0,
            ter(temINVALID_FLAG));

        // Invalid options
        // flags, symbol, priceUnit, symbolClass, name, tomlDomain
        std::vector<std::tuple<
            std::uint32_t,
            std::optional<std::string>,
            std::optional<std::string>,
            std::optional<std::string>,
            std::optional<std::string>,
            std::optional<std::string>>>
            options = {
                {tfPriceOracle,
                 "XRP",
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt},
                {tfPriceOracle,
                 "XRP",
                 "USD",
                 std::nullopt,
                 std::nullopt,
                 std::nullopt},
                {tfPriceOracle, "XRP", "USD", "currency", "name", std::nullopt},
                {tfPriceOracle,
                 "XRP",
                 "USD",
                 "currency",
                 std::nullopt,
                 "domain"},
                {tfAnyOracle,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 "name",
                 std::nullopt},
                {tfAnyOracle,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 std::nullopt,
                 "domain"},
                {tfAnyOracle,
                 "XRP",
                 std::nullopt,
                 std::nullopt,
                 "name",
                 "domain"},
                {tfAnyOracle,
                 std::nullopt,
                 "USD",
                 std::nullopt,
                 "name",
                 "domain"},
                {tfAnyOracle,
                 std::nullopt,
                 std::nullopt,
                 "currency",
                 "name",
                 "domain"},
                {tfPriceOracle | tfAnyOracle,
                 std::nullopt,
                 std::nullopt,
                 "currency",
                 "name",
                 "domain"}};
        for (auto const& opt : options)
            oracle.create(
                std::get<0>(opt),
                owner,
                std::get<1>(opt),
                std::get<2>(opt),
                std::get<3>(opt),
                std::get<4>(opt),
                std::get<5>(opt),
                10,
                std::nullopt,
                std::nullopt,
                0,
                ter(temMALFORMED));

        // Invalid number historical
        for (auto i : {0, 11})
            oracle.create(
                tfPriceOracle,
                owner,
                "XRP",
                "USD",
                "currency",
                std::nullopt,
                std::nullopt,
                i,
                std::nullopt,
                std::nullopt,
                0,
                ter(temBAD_HISTORICAL));

        // Oracle already exists
        oracle.create(tfPriceOracle, owner, "XRP", "USD", "currency");
        BEAST_EXPECT(oracle.exists());
        oracle.create(
            tfPriceOracle,
            owner,
            "XRP",
            "USD",
            "currency",
            std::nullopt,
            std::nullopt,
            10,
            oracle.oracleID(),
            std::nullopt,
            0,
            (ter(tecDUPLICATE)));
    }

    void
    testCreate()
    {
        testcase("Create");

        using namespace jtx;

        // Pricing
        {
            Env env(*this);
            Account const owner("owner");
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, owner, "XRP", "USD", "currency", 10);
            BEAST_EXPECT(oracle.exists());
        }

        // Any
        {
            Env env(*this);
            Account const owner("owner");
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, owner, "name", "tomldomain", 10);
            BEAST_EXPECT(oracle.exists());
        }
    }

    void
    testInvalidDelete()
    {
        testcase("Invalid Delete");

        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, owner, "name", "tomldomain", 10);
        BEAST_EXPECT(oracle.exists());

        // Invalid OracleID
        oracle.remove(
            owner, std::nullopt, oracle.randOracleID(), 0, ter(tecNO_ENTRY));

        // Invalid owner
        Account const invalid("invalid");
        env.fund(XRP(1'000), invalid);
        oracle.remove(
            invalid, std::nullopt, std::nullopt, 0, ter(tecNO_PERMISSION));

        // Invalid multisig
    }

    void
    testDelete()
    {
        testcase("Delete");

        using namespace jtx;

        // Pricing Oracle
        {
            Env env(*this);
            Account owner("owner");
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, owner, "XPR", "USD", "currency", 10);
            BEAST_EXPECT(oracle.exists());
            oracle.remove(owner);
            BEAST_EXPECT(!oracle.exists());
        }

        // Any Oracle
        {
            Env env(*this);
            Account owner("owner");
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, owner, "name", "tomldomain", 10);
            BEAST_EXPECT(oracle.exists());
            oracle.remove(owner);
            BEAST_EXPECT(!oracle.exists());
        }
    }

    void
    testInvalidUpdate()
    {
        testcase("Invalid Update");

        using namespace jtx;

        BEAST_EXPECT(true);
    }

    void
    testUpdate()
    {
        testcase("Update");

        using namespace jtx;

        BEAST_EXPECT(true);
    }

    void
    testMultisig(FeatureBitset features)
    {
        testcase("Multisig");

        using namespace jtx;

        // Create
        {
            Env env(*this, features);
            Account const alice{"alice", KeyType::secp256k1};
            Account const bogie{"bogie", KeyType::secp256k1};
            Account const ed{"ed", KeyType::secp256k1};
            Account const becky{"becky", KeyType::ed25519};
            Account const zelda{"zelda", KeyType::secp256k1};
            env.fund(XRP(1'000), alice, becky, zelda, ed);

            // alice uses a regular key with the master disabled.
            Account const alie{"alie", KeyType::secp256k1};
            env(regkey(alice, alie));
            env(fset(alice, asfDisableMaster), sig(alice));

            // Attach signers to alice.
            env(signers(alice, 2, {{becky, 1}, {bogie, 1}, {ed, 2}}),
                sig(alie));
            env.close();
            // if multiSignReserve disabled then its 2 + 1 per signer
            int const signerListOwners{
                features[featureMultiSignReserve] ? 1 : 5};
            env.require(owners(alice, signerListOwners));

            Oracle oracle(env);
            oracle.create(
                tfPriceOracle,
                alice,
                "XRP",
                "USD",
                "currency",
                std::nullopt,
                std::nullopt,
                10,
                std::nullopt,
                msig(becky),
                0,
                ter(tefBAD_QUORUM));
            oracle.create(
                tfPriceOracle,
                alice,
                "XRP",
                "USD",
                "currency",
                std::nullopt,
                std::nullopt,
                10,
                std::nullopt,
                msig(zelda),
                0,
                ter(tefBAD_SIGNATURE));
            oracle.create(
                tfPriceOracle,
                alice,
                "XRP",
                "USD",
                "currency",
                std::nullopt,
                std::nullopt,
                10,
                std::nullopt,
                msig(becky, bogie));
            BEAST_EXPECT(oracle.exists());

            // Update
            // One of the test is to change the signer list

            // Remove
            oracle.remove(
                alice, msig(becky), std::nullopt, 100'000, ter(tefBAD_QUORUM));
            oracle.remove(
                alice,
                msig(zelda),
                std::nullopt,
                100'000,
                ter(tefBAD_SIGNATURE));
            oracle.remove(alice, msig(ed), std::nullopt, 100'000);
            BEAST_EXPECT(!oracle.exists());
        }
    }

public:
    void
    run() override
    {
        using namespace jtx;
        auto const all = supported_amendments();
        testInvalidCreate();
        testInvalidDelete();
        testInvalidUpdate();
        testCreate();
        testDelete();
        testUpdate();
        for (auto const& features :
             {all,
              all - featureMultiSignReserve - featureExpandedSignerList,
              all - featureExpandedSignerList})
            testMultisig(features);
    }
};

BEAST_DEFINE_TESTSUITE(Oracle, app, ripple);

}  // namespace test

}  // namespace ripple
