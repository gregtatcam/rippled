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

#include <ripple/protocol/jss.h>
#include <test/jtx/Oracle.h>

namespace ripple {
namespace test {

struct Oracle_test : public beast::unit_test::suite
{
private:
    static auto
    ledgerEntryOracle(jtx::Env& env, uint256 const& id)
    {
        Json::Value jvParams;
        jvParams[jss::oracle][jss::OracleID] = to_string(id);
        return env.rpc(
            "json", "ledger_entry", to_string(jvParams))[jss::result];
    }

    void
    testInvalidSet()
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
                {{"XRP", "USD", 740, 1}},
                ter(tecINSUFFICIENT_RESERVE));
        }

        {
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(env);

            // Invalid flag
            oracle.create(
                owner,
                {{"XRP", "USD", 740, 1}},
                "currency",
                "provider",
                "URI",
                // last update time
                std::nullopt,
                tfSellNFToken,
                // msig
                std::nullopt,
                0,
                ter(temINVALID_FLAG));

            // Duplicate token pair
            oracle.create(
                owner,
                {{"XRP", "USD", 740, 1}, {"XRP", "USD", 750, 1}},
                "currency",
                "provider",
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                0,
                ter(tecDUPLICATE));

            // Array of token pair is 0 or exceeds 10
            oracle.create(
                owner,
                {{"XRP", "US1", 740, 1},
                 {"XRP", "US2", 750, 1},
                 {"XRP", "US3", 740, 1},
                 {"XRP", "US4", 750, 1},
                 {"XRP", "US5", 740, 1},
                 {"XRP", "US6", 750, 1},
                 {"XRP", "US7", 740, 1},
                 {"XRP", "US8", 750, 1},
                 {"XRP", "US9", 740, 1},
                 {"XRP", "U10", 750, 1},
                 {"XRP", "U11", 740, 1}},
                "currency",
                "provider",
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                0,
                ter(temARRAY_SIZE));
            oracle.create(
                owner,
                {},
                "currency",
                "provider",
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                0,
                ter(temARRAY_SIZE));
        }

        // Array of token pair exceeds 10 after update
        {
            Env env{*this};
            env.fund(XRP(1'000), owner);

            Oracle oracle(
                env, owner, {{"XRP", "USD", 740, 1}}, ter(tesSUCCESS));
            oracle.update(
                owner,
                {
                    {"XRP", "US1", 740, 1},
                    {"XRP", "US2", 750, 1},
                    {"XRP", "US3", 740, 1},
                    {"XRP", "US4", 750, 1},
                    {"XRP", "US5", 740, 1},
                    {"XRP", "US6", 750, 1},
                    {"XRP", "US7", 740, 1},
                    {"XRP", "US8", 750, 1},
                    {"XRP", "US9", 740, 1},
                    {"XRP", "U10", 750, 1},
                },
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(temARRAY_SIZE));
        }

        {
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(env);

            // Symbol class or provider not included on create
            oracle.create(
                owner,
                {{"XRP", "USD", 740, 1}},
                std::nullopt,
                "provider",
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                0,
                ter(temMALFORMED));
            oracle.create(
                owner,
                {{"XRP", "USD", 740, 1}},
                "currency",
                std::nullopt,
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                0,
                ter(temMALFORMED));

            // Symbol class or provider are included on update
            oracle.set(
                owner,
                {{"XRP", "USD", 740, 1}},
                std::nullopt,
                "provider",
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(temMALFORMED));
            oracle.set(
                owner,
                {{"XRP", "USD", 740, 1}},
                "currency",
                std::nullopt,
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(temMALFORMED));

            // Fields too long
            // Symbol class
            std::string symbolClass(13, '0');
            oracle.set(
                owner,
                {{"XRP", "USD", 740, 1}},
                symbolClass,
                "provider",
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(temMALFORMED));
            // provider
            std::string const large(257, '0');
            oracle.set(
                owner,
                {{"XRP", "USD", 740, 1}},
                "currency",
                large,
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(temMALFORMED));
            // URI
            oracle.set(
                owner,
                {{"XRP", "USD", 740, 1}},
                "currency",
                "provider",
                large,
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(temMALFORMED));
        }

        {
            // invalid owner
            Env env(*this);
            Account const some("some");
            env.fund(XRP(1'000), owner);
            env.fund(XRP(1'000), some);
            Oracle oracle(
                env, owner, {{"XRP", "USD", 740, 1}}, ter(tesSUCCESS));
            oracle.update(
                some,
                {{"XRP", "USD", 740, 1}},
                "URI",
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(tecNO_PERMISSION));
        }
    }

    void
    testCreate()
    {
        testcase("Create");
        using namespace jtx;

        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, owner, {{"XRP", "USD", 740, 1}}, ter(tesSUCCESS));
        BEAST_EXPECT(oracle.exists());
    }

    void
    testInvalidDelete()
    {
        testcase("Invalid Delete");

        using namespace jtx;
        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, owner, {{"USD", "XRP", 740, 1}}, ter(tesSUCCESS));
        BEAST_EXPECT(oracle.exists());

        // Invalid OracleID
        oracle.remove(
            owner, std::nullopt, oracle.randOracleID(), 0, ter(tecNO_ENTRY));

        // Invalid owner
        Account const invalid("invalid");
        env.fund(XRP(1'000), invalid);
        oracle.remove(
            invalid, std::nullopt, std::nullopt, 0, ter(tecNO_PERMISSION));
    }

    void
    testDelete()
    {
        testcase("Delete");
        using namespace jtx;

        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, owner, {{"XRP", "USD", 740, 1}}, ter(tesSUCCESS));
        BEAST_EXPECT(oracle.exists());
        oracle.remove(owner);
        BEAST_EXPECT(!oracle.exists());
    }

    void
    testUpdate()
    {
        testcase("Update");
        using namespace jtx;
        Account const owner("owner");

        Env env(*this);
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, owner, {{"XRP", "USD", 740, 1}}, ter(tesSUCCESS));
        BEAST_EXPECT(oracle.exists());

        // update existing pair
        oracle.update(owner, {{"XRP", "USD", 740, 2}});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 740, 2}}));

        // add new pairs, the not-included pair is reset
        oracle.update(owner, {{"XRP", "EUR", 700, 2}});
        BEAST_EXPECT(
            oracle.expectPrice({{"XRP", "USD", 0, 0}, {"XRP", "EUR", 700, 2}}));
    }

    void
    testMultisig(FeatureBitset features)
    {
        testcase("Multisig");
        using namespace jtx;

        Env env(*this, features);
        Account const alice{"alice", KeyType::secp256k1};
        Account const bogie{"bogie", KeyType::secp256k1};
        Account const ed{"ed", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const zelda{"zelda", KeyType::secp256k1};
        Account const bob{"bob", KeyType::secp256k1};
        env.fund(XRP(1'000), alice, becky, zelda, ed, bob);

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}, {ed, 2}}), sig(alie));
        env.close();
        // if multiSignReserve disabled then its 2 + 1 per signer
        int const signerListOwners{features[featureMultiSignReserve] ? 1 : 5};
        env.require(owners(alice, signerListOwners));

        // Create
        Oracle oracle(env);
        oracle.create(
            alice,
            {{"XRP", "USD", 740, 1}},
            "currency",
            "provider",
            "URI",
            std::nullopt,
            0,
            msig(becky),
            0,
            ter(tefBAD_QUORUM));
        oracle.create(
            alice,
            {{"XRP", "USD", 740, 1}},
            "currency",
            "provider",
            "URI",
            std::nullopt,
            0,
            msig(zelda),
            0,
            ter(tefBAD_SIGNATURE));
        oracle.create(
            alice,
            {{"XRP", "USD", 740, 1}},
            "currency",
            "provider",
            "URI",
            std::nullopt,
            0,
            msig(becky, bogie));
        BEAST_EXPECT(oracle.exists());

        // Update
        oracle.update(
            alice,
            {{"XRP", "USD", 740, 1}},
            "URI",
            std::nullopt,
            0,
            msig(becky),
            std::nullopt,
            0,
            ter(tefBAD_QUORUM));
        oracle.update(
            alice,
            {{"XRP", "USD", 740, 1}},
            "URI",
            std::nullopt,
            0,
            msig(zelda),
            std::nullopt,
            0,
            ter(tefBAD_SIGNATURE));
        oracle.update(
            alice,
            {{"XRP", "USD", 740, 1}},
            "URI",
            std::nullopt,
            0,
            msig(becky, bogie));
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 740, 1}}));
        // remove the signer list
        env(signers(alice, jtx::none), sig(alie));
        env.close();
        env.require(owners(alice, 1));
        // create new signer list
        env(signers(alice, 2, {{zelda, 1}, {bob, 1}, {ed, 2}}), sig(alie));
        env.close();
        // old list fails
        oracle.update(
            alice,
            {{"XRP", "USD", 740, 1}},
            "URI",
            std::nullopt,
            0,
            msig(becky, bogie),
            std::nullopt,
            0,
            ter(tefBAD_SIGNATURE));
        // updated list succeeds
        oracle.update(
            alice,
            {{"XRP", "USD", 7412, 2}},
            "URI",
            std::nullopt,
            0,
            msig(zelda, bob));
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 7412, 2}}));
        oracle.update(
            alice,
            {{"XRP", "USD", 74245, 3}},
            "URI",
            std::nullopt,
            0,
            msig(ed));
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 74245, 3}}));

        // Remove
        oracle.remove(
            alice, msig(bob), std::nullopt, 100'000, ter(tefBAD_QUORUM));
        oracle.remove(
            alice, msig(becky), std::nullopt, 100'000, ter(tefBAD_SIGNATURE));
        oracle.remove(alice, msig(ed), std::nullopt, 100'000);
        BEAST_EXPECT(!oracle.exists());
    }

    void
    testAmendment()
    {
        testcase("Amendment");
        using namespace jtx;

        auto const features = supported_amendments() - featurePriceOracle;
        Account const owner("owner");
        Env env(*this, features);

        env.fund(XRP(1'000), owner);
        {
            Oracle oracle(
                env, owner, {{"XRP", "USD", 740, 1}}, ter(temDISABLED));
        }
        {
            Oracle oracle(env);
            oracle.update(
                owner,
                {{"XRP", "USD", 740, 1}},
                std::nullopt,
                std::nullopt,
                0,
                std::nullopt,
                std::nullopt,
                0,
                ter(temDISABLED));
        }
        {
            Oracle oracle(env);
            oracle.remove(
                owner, std::nullopt, std::nullopt, 0, ter(temDISABLED));
        }
    }

    void
    testLedgerEntry()
    {
        testcase("Ledger Entry");
        using namespace jtx;

        Env env(*this);
        std::vector<AccountID> accounts;
        std::vector<uint256> oracles;
        for (int i = 0; i < 10; ++i)
        {
            Account const owner(std::string("owner") + std::to_string(i));
            env.fund(XRP(1'000), owner);
            // different accounts can have the same asset pair
            Oracle oracle(
                env, owner, {{"XRP", "USD", 740, 1}}, ter(tesSUCCESS));
            accounts.push_back(owner.id());
            oracles.push_back(oracle.oracleID());
            // same account can have different asset pair
            Oracle oracle1(
                env, owner, {{"XRP", "EUR", 740, 1}}, ter(tesSUCCESS));
            accounts.push_back(owner.id());
            oracles.push_back(oracle1.oracleID());
        }
        for (int i = 0; i < accounts.size(); ++i)
        {
            auto const jv = ledgerEntryOracle(env, oracles[i]);
            try
            {
                BEAST_EXPECT(
                    jv[jss::node][jss::Owner] == to_string(accounts[i]));
            }
            catch (...)
            {
                fail();
            }
        }
    }

public:
    void
    run() override
    {
        using namespace jtx;
        auto const all = supported_amendments();
        testInvalidSet();
        testInvalidDelete();
        testCreate();
        testDelete();
        testUpdate();
        testAmendment();
        for (auto const& features :
             {all,
              all - featureMultiSignReserve - featureExpandedSignerList,
              all - featureExpandedSignerList})
            testMultisig(features);
        testLedgerEntry();
    }
};

BEAST_DEFINE_TESTSUITE(Oracle, app, ripple);

}  // namespace test

}  // namespace ripple
