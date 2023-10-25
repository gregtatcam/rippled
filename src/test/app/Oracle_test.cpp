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
    void
    testInvalidSet()
    {
        testcase("Invalid Create");

        using namespace jtx;
        Account const owner("owner");

        {
            // Invalid account
            Env env(*this);
            Account const bad("bad");
            env.memoize(bad);
            Oracle oracle(
                env, {.owner = bad, .seq = seq(1), .ter = ter(terNO_ACCOUNT)});
        }

        // Insufficient reserve
        {
            Env env(*this);
            env.fund(env.current()->fees().accountReserve(0), owner);
            Oracle oracle(
                env, {.owner = owner, .ter = ter(tecINSUFFICIENT_RESERVE)});
        }
        // Insufficient reserve if the data series extends to greater than 5
        {
            Env env(*this);
            env.fund(
                env.current()->fees().accountReserve(1) +
                    env.current()->fees().base * 2,
                owner);
            Oracle oracle(env, {.owner = owner});
            BEAST_EXPECT(oracle.exists());
            oracle.set(UpdateArg{
                .series =
                    {
                        {"XRP", "EUR", 740, 1},
                        {"XRP", "GBP", 740, 1},
                        {"XRP", "CNY", 740, 1},
                        {"XRP", "CAD", 740, 1},
                        {"XRP", "AUD", 740, 1},
                    },
                .ter = ter(tecINSUFFICIENT_RESERVE)});
        }

        {
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner}, false);

            // Invalid flag
            oracle.set(
                CreateArg{.flags = tfSellNFToken, .ter = ter(temINVALID_FLAG)});

            // Duplicate token pair
            oracle.set(CreateArg{
                .series = {{"XRP", "USD", 740, 1}, {"XRP", "USD", 750, 1}},
                .ter = ter(tecDUPLICATE)});

            // Price is not included
            oracle.set(CreateArg{
                .series =
                    {{"XRP", "USD", 740, 1}, {"XRP", "USD", std::nullopt, 1}},
                .ter = ter(temMALFORMED)});

            // Array of token pair is 0 or exceeds 10
            oracle.set(CreateArg{
                .series =
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
                .ter = ter(temARRAY_SIZE)});
            oracle.set(CreateArg{.series = {}, .ter = ter(temARRAY_SIZE)});
        }

        // Array of token pair exceeds 10 after update
        {
            Env env{*this};
            env.fund(XRP(1'000), owner);

            Oracle oracle(
                env,
                CreateArg{
                    .owner = owner, .series = {{{"XRP", "USD", 740, 1}}}});
            oracle.set(UpdateArg{
                .series =
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
                .ter = ter(temARRAY_SIZE)});
        }

        {
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner}, false);

            // Symbol class or provider not included on create
            oracle.set(CreateArg{
                .symbolClass = std::nullopt,
                .provider = "provider",
                .ter = ter(temMALFORMED)});
            oracle.set(CreateArg{
                .symbolClass = "currency",
                .provider = std::nullopt,
                .uri = "URI",
                .ter = ter(temMALFORMED)});

            // Symbol class or provider are included on update
            oracle.set(CreateArg{});
            BEAST_EXPECT(oracle.exists());
            oracle.set(UpdateArg{
                .provider = "provider",
                .series = {{"XRP", "USD", 740, 1}},
                .ter = ter(temMALFORMED)});
            oracle.set(UpdateArg{
                .symbolClass = "currency",
                .series = {{"XRP", "USD", 740, 1}},
                .ter = ter(temMALFORMED)});
        }

        {
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner}, false);

            // Fields too long
            // Symbol class
            std::string symbolClass(13, '0');
            oracle.set(CreateArg{
                .symbolClass = symbolClass, .ter = ter(temMALFORMED)});
            // provider
            std::string const large(257, '0');
            oracle.set(CreateArg{.provider = large, .ter = ter(temMALFORMED)});
            // URI
            oracle.set(CreateArg{.uri = large, .ter = ter(temMALFORMED)});
        }

        {
            // Different owner creates a new object and fails because
            // of missing fields currency/provider
            Env env(*this);
            Account const some("some");
            env.fund(XRP(1'000), owner);
            env.fund(XRP(1'000), some);
            Oracle oracle(env, {.owner = owner});
            BEAST_EXPECT(oracle.exists());
            oracle.set(UpdateArg{
                .owner = some,
                .series = {{"XRP", "USD", 740, 1}},
                .ter = ter(temMALFORMED)});
        }

        {
            // Invalid update time
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner});
            BEAST_EXPECT(oracle.exists());
            env.close(std::chrono::seconds(100));
            // Less than the last close time
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .lastUpdateTime = 60,
                .ter = ter(tecINVALID_UPDATE_TIME)});
            // Greater than last close time + 30 sec
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .lastUpdateTime = 500,
                .ter = ter(tecINVALID_UPDATE_TIME)});
            oracle.set(UpdateArg{.series = {{"XRP", "USD", 740, 1}}});
            BEAST_EXPECT(oracle.expectLastUpdateTime(150));
            // Less than the previous lastUpdateTime
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .lastUpdateTime = 149,
                .ter = ter(tecINVALID_UPDATE_TIME)});
        }
    }

    void
    testCreate()
    {
        testcase("Create");
        using namespace jtx;
        Account const owner("owner");

        {
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner});
            BEAST_EXPECT(oracle.exists());
        }

        {
            // Different owner creates a new object
            Env env(*this);
            Account const some("some");
            env.fund(XRP(1'000), owner);
            env.fund(XRP(1'000), some);
            Oracle oracle(env, {.owner = owner});
            BEAST_EXPECT(oracle.exists());
            oracle.set(CreateArg{.owner = some});
            BEAST_EXPECT(Oracle::exists(env, some, oracle.sequence()));
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
        Oracle oracle(env, {.owner = owner});
        BEAST_EXPECT(oracle.exists());

        {
            // Invalid account
            Account const bad("bad");
            env.memoize(bad);
            oracle.remove(
                {.owner = bad, .seq = seq(1), .ter = ter(terNO_ACCOUNT)});
        }

        // Invalid Sequence
        oracle.remove({.sequence = 2, .ter = ter(tecNO_ENTRY)});

        // Invalid owner
        Account const invalid("invalid");
        env.fund(XRP(1'000), invalid);
        oracle.remove({.owner = invalid, .ter = ter(tecNO_ENTRY)});
    }

    void
    testDelete()
    {
        testcase("Delete");
        using namespace jtx;

        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, {.owner = owner});
        BEAST_EXPECT(oracle.exists());
        oracle.remove({});
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
        Oracle oracle(env, {.owner = owner});
        BEAST_EXPECT(oracle.exists());

        // update existing pair
        oracle.set(UpdateArg{.series = {{"XRP", "USD", 740, 2}}});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 740, 2}}));

        // add new pairs, not-included pair is reset
        oracle.set(UpdateArg{.series = {{"XRP", "EUR", 700, 2}}});
        BEAST_EXPECT(
            oracle.expectPrice({{"XRP", "USD", 0, 0}, {"XRP", "EUR", 700, 2}}));

        // update both pairs
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 741, 2}, {"XRP", "EUR", 710, 2}}});
        BEAST_EXPECT(oracle.expectPrice(
            {{"XRP", "USD", 741, 2}, {"XRP", "EUR", 710, 2}}));
    }

    void
    testMultisig(FeatureBitset features)
    {
        testcase("Multisig");
        using namespace jtx;
        Oracle::setFee(100'000);

        Env env(*this, features);
        Account const alice{"alice", KeyType::secp256k1};
        Account const bogie{"bogie", KeyType::secp256k1};
        Account const ed{"ed", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const zelda{"zelda", KeyType::secp256k1};
        Account const bob{"bob", KeyType::secp256k1};
        env.fund(XRP(10'000), alice, becky, zelda, ed, bob);

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
        Oracle oracle(env, CreateArg{.owner = alice}, false);
        oracle.set(CreateArg{.msig = msig(becky), .ter = ter(tefBAD_QUORUM)});
        oracle.set(
            CreateArg{.msig = msig(zelda), .ter = ter(tefBAD_SIGNATURE)});
        oracle.set(CreateArg{.msig = msig(becky, bogie)});
        BEAST_EXPECT(oracle.exists());

        // Update
        oracle.set(UpdateArg{
            .msig = msig(becky),
            .series = {{"XRP", "USD", 740, 1}},
            .ter = ter(tefBAD_QUORUM)});
        oracle.set(UpdateArg{
            .msig = msig(zelda),
            .series = {{"XRP", "USD", 740, 1}},
            .ter = ter(tefBAD_SIGNATURE)});
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 741, 1}}, .msig = msig(becky, bogie)});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 741, 1}}));
        // remove the signer list
        env(signers(alice, jtx::none), sig(alie));
        env.close();
        env.require(owners(alice, 1));
        // create new signer list
        env(signers(alice, 2, {{zelda, 1}, {bob, 1}, {ed, 2}}), sig(alie));
        env.close();
        // old list fails
        oracle.set(UpdateArg{
            .msig = msig(becky, bogie),
            .series = {{"XRP", "USD", 740, 1}},
            .ter = ter(tefBAD_SIGNATURE)});
        // updated list succeeds
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 7412, 2}}, .msig = msig(zelda, bob)});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 7412, 2}}));
        oracle.set(
            UpdateArg{.series = {{"XRP", "USD", 74245, 3}}, .msig = msig(ed)});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 74245, 3}}));

        // Remove
        oracle.remove({.msig = msig(bob), .ter = ter(tefBAD_QUORUM)});
        oracle.remove({.msig = msig(becky), .ter = ter(tefBAD_SIGNATURE)});
        oracle.remove({.msig = msig(ed)});
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
            Oracle oracle(env, {.owner = owner, .ter = ter(temDISABLED)});
        }

        {
            Oracle oracle(env, {.owner = owner}, false);
            oracle.remove({.ter = ter(temDISABLED)});
        }
    }

    void
    testLedgerEntry()
    {
        testcase("Ledger Entry");
        using namespace jtx;

        Env env(*this);
        std::vector<AccountID> accounts;
        std::vector<std::uint32_t> oracles;
        for (int i = 0; i < 10; ++i)
        {
            Account const owner(std::string("owner") + std::to_string(i));
            env.fund(XRP(1'000), owner);
            // different accounts can have the same asset pair
            Oracle oracle(env, {.owner = owner, .sequence = i});
            accounts.push_back(owner.id());
            oracles.push_back(oracle.sequence());
            // same account can have different asset pair
            Oracle oracle1(env, {.owner = owner, .sequence = i + 10});
            accounts.push_back(owner.id());
            oracles.push_back(oracle1.sequence());
        }
        for (int i = 0; i < accounts.size(); ++i)
        {
            auto const jv = Oracle::ledgerEntry(env, accounts[i], oracles[i]);
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
