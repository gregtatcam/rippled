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

#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/check.h>
#include <test/jtx/trust.h>
#include <test/jtx/xchain_bridge.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class MPToken_test : public beast::unit_test::suite
{
    void
    testCreateValidation(FeatureBitset features)
    {
        testcase("Create Validate");
        using namespace test::jtx;
        Account const alice("alice");

        // test preflight of MPTokenIssuanceCreate
        {
            // If the MPT amendment is not enabled, you should not be able to
            // create MPTokenIssuances
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice);

            mptAlice.create({.ownerCount = 0, .err = temDISABLED});
        }

        // test preflight of MPTokenIssuanceCreate
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice);

            mptAlice.create({.flags = 0x00000001, .err = temINVALID_FLAG});

            // tries to set a txfee while not enabling in the flag
            mptAlice.create(
                {.maxAmt = "100",
                 .assetScale = 0,
                 .transferFee = 1,
                 .metadata = "test",
                 .err = temMALFORMED});

            // tries to set a txfee greater than max
            mptAlice.create(
                {.maxAmt = "100",
                 .assetScale = 0,
                 .transferFee = maxTransferFee + 1,
                 .metadata = "test",
                 .flags = tfMPTCanTransfer,
                 .err = temBAD_MPTOKEN_TRANSFER_FEE});

            // tries to set a txfee while not enabling transfer
            mptAlice.create(
                {.maxAmt = "100",
                 .assetScale = 0,
                 .transferFee = maxTransferFee,
                 .metadata = "test",
                 .err = temMALFORMED});

            // empty metadata returns error
            mptAlice.create(
                {.maxAmt = "100",
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "",
                 .err = temMALFORMED});

            // MaximumAmout of 0 returns error
            mptAlice.create(
                {.maxAmt = "0",
                 .assetScale = 1,
                 .transferFee = 1,
                 .metadata = "test",
                 .err = temMALFORMED});

            // MaximumAmount larger than 63 bit returns error
            mptAlice.create(
                {.maxAmt = "18446744073709551600",  // FFFFFFFFFFFFFFF0
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "test",
                 .err = temMALFORMED});
            mptAlice.create(
                {.maxAmt = "9223372036854775808",  // 8000000000000000
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "test",
                 .err = temMALFORMED});
        }
    }

    void
    testCreateEnabled(FeatureBitset features)
    {
        testcase("Create Enabled");

        using namespace test::jtx;
        Account const alice("alice");

        {
            // If the MPT amendment IS enabled, you should be able to create
            // MPTokenIssuances
            Env env{*this, features};
            MPTTester mptAlice(env, alice);
            mptAlice.create(
                {.maxAmt = "9223372036854775807",  // 7FFFFFFFFFFFFFFF
                 .assetScale = 1,
                 .transferFee = 10,
                 .metadata = "123",
                 .ownerCount = 1,
                 .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow |
                     tfMPTCanTrade | tfMPTCanTransfer | tfMPTCanClawback});

            // Get the hash for the most recent transaction.
            std::string const txHash{
                env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

            Json::Value const result = env.rpc("tx", txHash)[jss::result];
            BEAST_EXPECT(
                result[sfMaximumAmount.getJsonName()] == "9223372036854775807");
        }
    }

    void
    testDestroyValidation(FeatureBitset features)
    {
        testcase("Destroy Validate");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        // MPTokenIssuanceDestroy (preflight)
        {
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice);
            auto const id = getMptID(alice, env.seq(alice));
            mptAlice.destroy({.id = id, .ownerCount = 0, .err = temDISABLED});

            env.enableFeature(featureMPTokensV1);

            mptAlice.destroy(
                {.id = id, .flags = 0x00000001, .err = temINVALID_FLAG});
        }

        // MPTokenIssuanceDestroy (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.destroy(
                {.id = getMptID(alice.id(), env.seq(alice)),
                 .ownerCount = 0,
                 .err = tecOBJECT_NOT_FOUND});

            mptAlice.create({.ownerCount = 1});

            // a non-issuer tries to destroy a mptissuance they didn't issue
            mptAlice.destroy({.issuer = &bob, .err = tecNO_PERMISSION});

            // Make sure that issuer can't delete issuance when it still has
            // outstanding balance
            {
                // bob now holds a mptoken object
                mptAlice.authorize({.account = &bob, .holderCount = 1});

                // alice pays bob 100 tokens
                mptAlice.pay(alice, bob, 100);

                mptAlice.destroy({.err = tecHAS_OBLIGATIONS});
            }
        }
    }

    void
    testDestroyEnabled(FeatureBitset features)
    {
        testcase("Destroy Enabled");

        using namespace test::jtx;
        Account const alice("alice");

        // If the MPT amendment IS enabled, you should be able to destroy
        // MPTokenIssuances
        Env env{*this, features};
        MPTTester mptAlice(env, alice);

        mptAlice.create({.ownerCount = 1});

        mptAlice.destroy({.ownerCount = 0});
    }

    void
    testAuthorizeValidation(FeatureBitset features)
    {
        testcase("Validate authorize transaction");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const cindy("cindy");
        // Validate amendment enable in MPTokenAuthorize (preflight)
        {
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.authorize(
                {.account = &bob,
                 .id = getMptID(alice, env.seq(alice)),
                 .err = temDISABLED});
        }

        // Validate fields in MPTokenAuthorize (preflight)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1});

            mptAlice.authorize(
                {.account = &bob, .flags = 0x00000002, .err = temINVALID_FLAG});

            mptAlice.authorize(
                {.account = &bob, .holder = &bob, .err = temMALFORMED});

            mptAlice.authorize({.holder = &alice, .err = temMALFORMED});
        }

        // Try authorizing when MPTokenIssuance doesn't exist in
        // MPTokenAuthorize (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});
            auto const id = getMptID(alice, env.seq(alice));

            mptAlice.authorize(
                {.holder = &bob, .id = id, .err = tecOBJECT_NOT_FOUND});

            mptAlice.authorize(
                {.account = &bob, .id = id, .err = tecOBJECT_NOT_FOUND});
        }

        // Test bad scenarios without allowlisting in MPTokenAuthorize
        // (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1});

            // bob submits a tx with a holder field
            mptAlice.authorize(
                {.account = &bob, .holder = &alice, .err = tecNO_PERMISSION});

            // alice tries to hold onto her own token
            mptAlice.authorize({.account = &alice, .err = tecNO_PERMISSION});

            // the mpt does not enable allowlisting
            mptAlice.authorize({.holder = &bob, .err = tecNO_AUTH});

            // bob now holds a mptoken object
            mptAlice.authorize({.account = &bob, .holderCount = 1});

            // bob cannot create the mptoken the second time
            mptAlice.authorize({.account = &bob, .err = tecMPTOKEN_EXISTS});

            // Check that bob cannot delete MPToken when his balance is
            // non-zero
            {
                // alice pays bob 100 tokens
                mptAlice.pay(alice, bob, 100);

                // bob tries to delete his MPToken, but fails since he still
                // holds tokens
                mptAlice.authorize(
                    {.account = &bob,
                     .flags = tfMPTUnauthorize,
                     .err = tecHAS_OBLIGATIONS});

                // bob pays back alice 100 tokens
                mptAlice.pay(bob, alice, 100);
            }

            // bob deletes/unauthorizes his MPToken
            mptAlice.authorize({.account = &bob, .flags = tfMPTUnauthorize});

            // bob receives error when he tries to delete his MPToken that has
            // already been deleted
            mptAlice.authorize(
                {.account = &bob,
                 .holderCount = 0,
                 .flags = tfMPTUnauthorize,
                 .err = tecOBJECT_NOT_FOUND});
        }

        // Test bad scenarios with allow-listing in MPTokenAuthorize (preclaim)
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTRequireAuth});

            // alice submits a tx without specifying a holder's account
            mptAlice.authorize({.err = tecNO_PERMISSION});

            // alice submits a tx to authorize a holder that hasn't created
            // a mptoken yet
            mptAlice.authorize({.holder = &bob, .err = tecOBJECT_NOT_FOUND});

            // alice specifys a holder acct that doesn't exist
            mptAlice.authorize({.holder = &cindy, .err = tecNO_DST});

            // bob now holds a mptoken object
            mptAlice.authorize({.account = &bob, .holderCount = 1});

            // alice tries to unauthorize bob.
            // although tx is successful,
            // but nothing happens because bob hasn't been authorized yet
            mptAlice.authorize({.holder = &bob, .flags = tfMPTUnauthorize});

            // alice authorizes bob
            // make sure bob's mptoken has set lsfMPTAuthorized
            mptAlice.authorize({.holder = &bob});

            // alice tries authorizes bob again.
            // tx is successful, but bob is already authorized,
            // so no changes
            mptAlice.authorize({.holder = &bob});

            // bob deletes his mptoken
            mptAlice.authorize(
                {.account = &bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }

        // Test mptoken reserve requirement - first two mpts free (doApply)
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            MPTTester mptAlice1(
                env,
                alice,
                {.holders = {&bob},
                 .xrpHolders = acctReserve + XRP(1).value().xrp()});
            mptAlice1.create();

            MPTTester mptAlice2(env, alice, {.fund = false});
            mptAlice2.create();

            MPTTester mptAlice3(env, alice, {.fund = false});
            mptAlice3.create({.ownerCount = 3});

            // first mpt for free
            mptAlice1.authorize({.account = &bob, .holderCount = 1});

            // second mpt free
            mptAlice2.authorize({.account = &bob, .holderCount = 2});

            mptAlice3.authorize(
                {.account = &bob, .err = tecINSUFFICIENT_RESERVE});

            env(pay(
                env.master, bob, drops(incReserve + incReserve + incReserve)));
            env.close();

            mptAlice3.authorize({.account = &bob, .holderCount = 3});
        }
    }

    void
    testAuthorizeEnabled(FeatureBitset features)
    {
        testcase("Authorize Enabled");

        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        // Basic authorization without allowlisting
        {
            Env env{*this, features};

            // alice create mptissuance without allowisting
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1});

            // bob creates a mptoken
            mptAlice.authorize({.account = &bob, .holderCount = 1});

            // bob deletes his mptoken
            mptAlice.authorize(
                {.account = &bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }

        // With allowlisting
        {
            Env env{*this, features};

            // alice creates a mptokenissuance that requires authorization
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTRequireAuth});

            // bob creates a mptoken
            mptAlice.authorize({.account = &bob, .holderCount = 1});

            // alice authorizes bob
            mptAlice.authorize({.account = &alice, .holder = &bob});

            // Unauthorize bob's mptoken
            mptAlice.authorize(
                {.account = &alice,
                 .holder = &bob,
                 .holderCount = 1,
                 .flags = tfMPTUnauthorize});

            mptAlice.authorize(
                {.account = &bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }

        // Holder can have dangling MPToken even if issuance has been destroyed.
        // Make sure they can still delete/unauthorize the MPToken
        {
            Env env{*this, features};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1});

            // bob creates a mptoken
            mptAlice.authorize({.account = &bob, .holderCount = 1});

            // alice deletes her issuance
            mptAlice.destroy({.ownerCount = 0});

            // bob can delete his mptoken even though issuance is no longer
            // existent
            mptAlice.authorize(
                {.account = &bob, .holderCount = 0, .flags = tfMPTUnauthorize});
        }
    }

    void
    testSetValidation(FeatureBitset features)
    {
        testcase("Validate set transaction");

        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder
        Account const cindy("cindy");
        // Validate fields in MPTokenIssuanceSet (preflight)
        {
            Env env{*this, features - featureMPTokensV1};
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.set(
                {.account = &bob,
                 .id = getMptID(alice, env.seq(alice)),
                 .err = temDISABLED});

            env.enableFeature(featureMPTokensV1);

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &bob, .holderCount = 1});

            // test invalid flag
            mptAlice.set(
                {.account = &alice,
                 .flags = 0x00000008,
                 .err = temINVALID_FLAG});

            // set both lock and unlock flags at the same time will fail
            mptAlice.set(
                {.account = &alice,
                 .flags = tfMPTLock | tfMPTUnlock,
                 .err = temINVALID_FLAG});

            // if the holder is the same as the acct that submitted the tx,
            // tx fails
            mptAlice.set(
                {.account = &alice,
                 .holder = &alice,
                 .flags = tfMPTLock,
                 .err = temMALFORMED});
        }

        // Validate fields in MPTokenIssuanceSet (preclaim)
        // test when a mptokenissuance has disabled locking
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1});

            // alice tries to lock a mptissuance that has disabled locking
            mptAlice.set(
                {.account = &alice,
                 .flags = tfMPTLock,
                 .err = tecNO_PERMISSION});

            // alice tries to unlock mptissuance that has disabled locking
            mptAlice.set(
                {.account = &alice,
                 .flags = tfMPTUnlock,
                 .err = tecNO_PERMISSION});

            // issuer tries to lock a bob's mptoken that has disabled
            // locking
            mptAlice.set(
                {.account = &alice,
                 .holder = &bob,
                 .flags = tfMPTLock,
                 .err = tecNO_PERMISSION});

            // issuer tries to unlock a bob's mptoken that has disabled
            // locking
            mptAlice.set(
                {.account = &alice,
                 .holder = &bob,
                 .flags = tfMPTUnlock,
                 .err = tecNO_PERMISSION});
        }

        // Validate fields in MPTokenIssuanceSet (preclaim)
        // test when mptokenissuance has enabled locking
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            // alice trying to set when the mptissuance doesn't exist yet
            mptAlice.set(
                {.id = getMptID(alice.id(), env.seq(alice)),
                 .flags = tfMPTLock,
                 .err = tecOBJECT_NOT_FOUND});

            // create a mptokenissuance with locking
            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanLock});

            // a non-issuer acct tries to set the mptissuance
            mptAlice.set(
                {.account = &bob, .flags = tfMPTLock, .err = tecNO_PERMISSION});

            // trying to set a holder who doesn't have a mptoken
            mptAlice.set(
                {.holder = &bob,
                 .flags = tfMPTLock,
                 .err = tecOBJECT_NOT_FOUND});

            // trying to set a holder who doesn't exist
            mptAlice.set(
                {.holder = &cindy, .flags = tfMPTLock, .err = tecNO_DST});
        }
    }

    void
    testSetEnabled(FeatureBitset features)
    {
        testcase("Enabled set transaction");

        using namespace test::jtx;

        // Test locking and unlocking
        Env env{*this, features};
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder

        MPTTester mptAlice(env, alice, {.holders = {&bob}});

        // create a mptokenissuance with locking
        mptAlice.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanLock});

        mptAlice.authorize({.account = &bob, .holderCount = 1});

        // locks bob's mptoken
        mptAlice.set({.account = &alice, .holder = &bob, .flags = tfMPTLock});

        // trying to lock bob's mptoken again will still succeed
        // but no changes to the objects
        mptAlice.set({.account = &alice, .holder = &bob, .flags = tfMPTLock});

        // alice locks the mptissuance
        mptAlice.set({.account = &alice, .flags = tfMPTLock});

        // alice tries to lock up both mptissuance and mptoken again
        // it will not change the flags and both will remain locked.
        mptAlice.set({.account = &alice, .flags = tfMPTLock});
        mptAlice.set({.account = &alice, .holder = &bob, .flags = tfMPTLock});

        // alice unlocks bob's mptoken
        mptAlice.set({.account = &alice, .holder = &bob, .flags = tfMPTUnlock});

        // locks up bob's mptoken again
        mptAlice.set({.account = &alice, .holder = &bob, .flags = tfMPTLock});

        // alice unlocks mptissuance
        mptAlice.set({.account = &alice, .flags = tfMPTUnlock});

        // alice unlocks bob's mptoken
        mptAlice.set({.account = &alice, .holder = &bob, .flags = tfMPTUnlock});

        // alice unlocks mptissuance and bob's mptoken again despite that
        // they are already unlocked. Make sure this will not change the
        // flags
        mptAlice.set({.account = &alice, .holder = &bob, .flags = tfMPTUnlock});
        mptAlice.set({.account = &alice, .flags = tfMPTUnlock});
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace test::jtx;
        Account const alice("alice");  // issuer
        Account const bob("bob");      // holder
        Account const carol("carol");  // holder
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            // env(mpt::authorize(alice, id.key, std::nullopt));
            // env.close();

            mptAlice.authorize({.account = &bob});
            mptAlice.authorize({.account = &carol});

            // issuer to holder
            mptAlice.pay(alice, bob, 100);

            // holder to issuer
            mptAlice.pay(bob, alice, 100);

            // holder to holder
            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(bob, carol, 50);
        }

        // Holder is not authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            // issuer to holder
            mptAlice.pay(alice, bob, 100, tecNO_AUTH);

            // holder to issuer
            mptAlice.pay(bob, alice, 100, tecNO_AUTH);

            // holder to holder
            mptAlice.pay(bob, carol, 50, tecNO_AUTH);
        }

        // If allowlisting is enabled, Payment fails if the receiver is not
        // authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            mptAlice.authorize({.account = &bob});

            mptAlice.pay(alice, bob, 100, tecNO_AUTH);
        }

        // If allowlisting is enabled, Payment fails if the sender is not
        // authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTRequireAuth | tfMPTCanTransfer});

            // bob creates an empty MPToken
            mptAlice.authorize({.account = &bob});

            // alice authorizes bob to hold funds
            mptAlice.authorize({.account = &alice, .holder = &bob});

            // alice sends 100 MPT to bob
            mptAlice.pay(alice, bob, 100);

            // alice UNAUTHORIZES bob
            mptAlice.authorize(
                {.account = &alice, .holder = &bob, .flags = tfMPTUnauthorize});

            // bob fails to send back to alice because he is no longer
            // authorize to move his funds!
            mptAlice.pay(bob, alice, 100, tecNO_AUTH);
        }

        // Payer doesn't have enough funds
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = &bob});
            mptAlice.authorize({.account = &carol});

            mptAlice.pay(alice, bob, 100);

            // Pay to another holder
            mptAlice.pay(bob, carol, 101, tecPATH_PARTIAL);

            // Pay to the issuer
            mptAlice.pay(bob, alice, 101, tecPATH_PARTIAL);
        }

        // MPT is locked
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            mptAlice.create(
                {.ownerCount = 1, .flags = tfMPTCanLock | tfMPTCanTransfer});

            mptAlice.authorize({.account = &bob});
            mptAlice.authorize({.account = &carol});

            mptAlice.pay(alice, bob, 100);
            mptAlice.pay(alice, carol, 100);

            // Global lock
            mptAlice.set({.account = &alice, .flags = tfMPTLock});
            // Can't send between holders
            mptAlice.pay(bob, carol, 1, tecMPT_LOCKED);
            mptAlice.pay(carol, bob, 2, tecMPT_LOCKED);
            // Issuer can send
            mptAlice.pay(alice, bob, 3);
            // Holder can send back to issuer
            mptAlice.pay(bob, alice, 4);

            // Global unlock
            mptAlice.set({.account = &alice, .flags = tfMPTUnlock});
            // Individual lock
            mptAlice.set(
                {.account = &alice, .holder = &bob, .flags = tfMPTLock});
            // Can't send between holders
            mptAlice.pay(bob, carol, 5, tecMPT_LOCKED);
            mptAlice.pay(carol, bob, 6, tecMPT_LOCKED);
            // Issuer can send
            mptAlice.pay(alice, bob, 7);
            // Holder can send back to issuer
            mptAlice.pay(bob, alice, 8);
        }

        // Issuer fails trying to send more than the maximum amount allowed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create(
                {.maxAmt = "100",
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = &bob});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, 100);

            // issuer tries to exceed max amount
            auto const MPT = mptAlice["MPT"];
            mptAlice.pay(alice, bob, 1, tecPATH_PARTIAL);
            env(pay(alice, bob, MPT(1)),
                txflags(tfPartialPayment),
                ter(tecPATH_DRY));
        }

        // Issuer fails trying to send more than the default maximum
        // amount allowed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &bob});

            // issuer sends holder the default max amount allowed
            mptAlice.pay(alice, bob, maxMPTokenAmount);

            // issuer tries to exceed max amount
            mptAlice.pay(alice, bob, 1, tecPATH_PARTIAL);
        }

        // Can't pay negative amount
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &bob});

            mptAlice.pay(alice, bob, -1, temBAD_AMOUNT);
        }

        // pay more than max amount
        // fails in the json parser before
        // transactor is called
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice, bob);
            STAmount mpt{MPTIssue{getMptID(alice.id(), 1)}, UINT64_C(100)};
            Json::Value jv;
            jv[jss::secret] = alice.name();
            jv[jss::tx_json] = pay(alice, bob, mpt);
            jv[jss::tx_json][jss::Amount][jss::value] =
                to_string(maxMPTokenAmount + 1);
            auto const jrr = env.rpc("json", "submit", to_string(jv));
            BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
        }

        // Transfer fee
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            // Transfer fee is 10%
            mptAlice.create(
                {.transferFee = 10'000,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});

            // Holders create MPToken
            mptAlice.authorize({.account = &bob});
            mptAlice.authorize({.account = &carol});

            // Payment between the issuer and the holder, no transfer fee.
            mptAlice.pay(alice, bob, 2'000);

            // Payment between the holder and the issuer, no transfer fee.
            mptAlice.pay(alice, bob, 1'000);

            // Payment between the holders. The sender has to include sendmax
            // to cover the transfer fee.
            auto const MPT = mptAlice["MPT"];
            env(pay(bob, carol, MPT(1'000)), ter(tecPATH_PARTIAL));
            env(pay(bob, carol, MPT(1'000)), sendmax(MPT(1'100)));

            // Payment between the holders. The sender has to include sendmax
            // to cover the transfer fee.
            env(pay(bob, carol, MPT(100)), ter(tecPATH_PARTIAL));
            env(pay(bob, carol, MPT(100)), sendmax(MPT(110)));
        }

        // Test that non-issuer cannot send to each other if MPTCanTransfer
        // isn't set
        {
            Env env(*this, features);

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            // alice creates issuance without MPTCanTransfer
            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            // bob creates a MPToken
            mptAlice.authorize({.account = &bob});

            // cindy creates a MPToken
            mptAlice.authorize({.account = &carol});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            // bob tries to send cindy 10 tokens, but fails because canTransfer
            // is off
            mptAlice.pay(bob, carol, 10, tecNO_AUTH);

            // bob can send back to alice(issuer) just fine
            mptAlice.pay(bob, alice, 10);
        }

        // MPT is disabled
        {
            Env env{*this, features - featureMPTokensV1};

            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), bob);
            STAmount mpt{MPTIssue{getMptID(alice.id(), 1)}, UINT64_C(100)};

            env(pay(alice, bob, mpt), ter(temDISABLED));
        }

        // MPT is disabled, unsigned request
        {
            Env env{*this, features - featureMPTokensV1};
            auto const USD = alice["USD"];

            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), carol);
            STAmount mpt{MPTIssue{getMptID(alice.id(), 1)}, UINT64_C(100)};

            Json::Value jv;
            jv[jss::secret] = alice.name();
            jv[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
            jv[jss::tx_json] = pay(alice, carol, mpt);
            auto const jrr = env.rpc("json", "submit", to_string(jv));
            BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "temDISABLED");
        }

        // Invalid combination of send, sendMax, deliverMin
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &carol});

            env.disableFeature(featureMPTokensV2);

            // sendMax and DeliverMin are valid XRP amount,
            // but is invalid combination with MPT amount
            auto const MPT = mptAlice["MPT"];
            env(pay(alice, carol, MPT(100)),
                sendmax(XRP(100)),
                ter(temMALFORMED));
            env(pay(alice, carol, MPT(100)),
                delivermin(XRP(100)),
                ter(temMALFORMED));
        }

        // build_path is invalid if MPT
        {
            Env env{*this, features - featureMPTokensV2};

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &carol});

            Json::Value payment;
            payment[jss::secret] = alice.name();
            payment[jss::tx_json] = pay(alice, carol, mptAlice.mpt(100));

            payment[jss::build_path] = true;
            auto jrr = env.rpc("json", "submit", to_string(payment));
            BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::result][jss::error_message] ==
                "Field 'build_path' not allowed in this context.");
        }

        // Issuer fails trying to send fund after issuance was destroyed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &bob});

            // alice destroys issuance
            mptAlice.destroy({.ownerCount = 0});

            // alice tries to send bob fund after issuance is destroyed, should
            // fail.
            mptAlice.pay(alice, bob, 100, tecMPT_ISSUANCE_NOT_FOUND);
        }

        // Issuer fails trying to send to some who doesn't own MPT for a
        // issuance that was destroyed
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            // alice destroys issuance
            mptAlice.destroy({.ownerCount = 0});

            // alice tries to send bob who doesn't own the MPT after issuance is
            // destroyed, it should fail
            mptAlice.pay(alice, bob, 100, tecMPT_ISSUANCE_NOT_FOUND);
        }

        // Issuers issues maximum amount of MPT to a holder, the holder should
        // be able to transfer the max amount to someone else
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob, &carol}});

            mptAlice.create(
                {.maxAmt = "100", .ownerCount = 1, .flags = tfMPTCanTransfer});

            mptAlice.authorize({.account = &bob});
            mptAlice.authorize({.account = &carol});

            mptAlice.pay(alice, bob, 100);

            // transfer max amount to another holder
            mptAlice.pay(bob, carol, 100);
        }
    }

    void
    testMPTInvalidInTx(FeatureBitset features)
    {
        testcase("MPT Amount Invalid in Transaction");
        using namespace test::jtx;

        std::set<std::string> txWithAmounts;
        std::set<std::string> supportedTx = {
            jss::Clawback.c_str(),
            jss::SetFee.c_str(),
            jss::Payment.c_str(),
            jss::OfferCreate.c_str(),
            jss::Payment.c_str(),
            jss::AMMCreate.c_str(),
            jss::CheckCreate.c_str(),
            jss::CheckCash.c_str()};
        for (auto const& format : TxFormats::getInstance())
        {
            for (auto const& e : format.getSOTemplate())
            {
                // Transaction has amount fields.
                // Exclude transactions supporting MPT.
                // Exclude pseudo-transaction SetFee.
                // Don't consider the Fee field since
                // it's included in every transaction.
                if (e.supportMPT() != soeMPTNone &&
                    e.sField().getName() != jss::Fee &&
                    !supportedTx.contains(format.getName()) &&
                    format.getName() != jss::SetFee)
                {
                    txWithAmounts.insert(format.getName());
                    break;
                }
            }
        }

        Account const alice("alice");
        auto const USD = alice["USD"];
        Account const carol("carol");
        MPTIssue issue(getMptID(alice.id(), 1));
        STAmount mpt{issue, UINT64_C(100)};
        auto const jvb = bridge(alice, USD, alice, USD);
        for (auto const& feature : {features, features - featureMPTokensV1})
        {
            Env env{*this, feature};
            env.fund(XRP(1'000), alice);
            env.fund(XRP(1'000), carol);
            auto test = [&](Json::Value const& jv) {
                txWithAmounts.erase(jv[jss::TransactionType].asString());

                // tx is signed
                auto jtx = env.jt(jv);
                Serializer s;
                jtx.stx->add(s);
                auto jrr = env.rpc("submit", strHex(s.slice()));
                BEAST_EXPECT(
                    jrr[jss::result][jss::error] == "invalidTransaction");

                // tx is unsigned
                Json::Value jv1;
                jv1[jss::secret] = alice.name();
                jv1[jss::tx_json] = jv;
                jrr = env.rpc("json", "submit", to_string(jv1));
                BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
            };
            // All transactions with sfAmount, which don't support MPT
            // and transactions with amount fields, which can't be MPT

            // AMMDeposit
            auto ammDeposit = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMDeposit;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = to_json(xrpIssue());
                jv[jss::Asset2] = to_json(USD.issue());
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                jv[jss::Flags] = tfSingleAsset;
                test(jv);
            };
            for (SField const& field :
                 {std::ref(sfEPrice), std::ref(sfLPTokenOut)})
                ammDeposit(field);
            // AMMWithdraw
            auto ammWithdraw = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = to_json(xrpIssue());
                jv[jss::Asset2] = to_json(USD.issue());
                jv[jss::Flags] = tfSingleAsset;
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                test(jv);
            };
            for (SField const& field :
                 {std::ref(sfEPrice), std::ref(sfLPTokenIn)})
                ammWithdraw(field);
            // AMMBid
            auto ammBid = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMBid;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = to_json(xrpIssue());
                jv[jss::Asset2] = to_json(USD.issue());
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                test(jv);
            };
            ammBid(sfBidMin);
            ammBid(sfBidMax);
            // EscrowCreate
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::EscrowCreate;
                jv[jss::Account] = alice.human();
                jv[jss::Destination] = carol.human();
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv);
            }
            // PaymentChannelCreate
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelCreate;
                jv[jss::Account] = alice.human();
                jv[jss::Destination] = carol.human();
                jv[jss::SettleDelay] = 1;
                jv[sfPublicKey.fieldName] = strHex(alice.pk().slice());
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv);
            }
            // PaymentChannelFund
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelFund;
                jv[jss::Account] = alice.human();
                jv[sfChannel.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv);
            }
            // PaymentChannelClaim
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::PaymentChannelClaim;
                jv[jss::Account] = alice.human();
                jv[sfChannel.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv);
            }
            // NFTokenCreateOffer
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenCreateOffer;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenID.fieldName] = to_string(uint256{1});
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv);
            }
            // NFTokenAcceptOffer
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenBrokerFee.fieldName] =
                    mpt.getJson(JsonOptions::none);
                test(jv);
            }
            // NFTokenMint
            {
                Json::Value jv;
                jv[jss::TransactionType] = jss::NFTokenMint;
                jv[jss::Account] = alice.human();
                jv[sfNFTokenTaxon.fieldName] = 1;
                jv[jss::Amount] = mpt.getJson(JsonOptions::none);
                test(jv);
            }
            // TrustSet
            auto trustSet = [&](SField const& field) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::TrustSet;
                jv[jss::Account] = alice.human();
                jv[jss::Flags] = 0;
                jv[field.fieldName] = mpt.getJson(JsonOptions::none);
                test(jv);
            };
            trustSet(sfLimitAmount);
            trustSet(sfFee);
            // XChainCommit
            {
                Json::Value const jv = xchain_commit(alice, jvb, 1, mpt);
                test(jv);
            }
            // XChainClaim
            {
                Json::Value const jv = xchain_claim(alice, jvb, 1, mpt, alice);
                test(jv);
            }
            // XChainCreateClaimID
            {
                Json::Value const jv =
                    xchain_create_claim_id(alice, jvb, mpt, alice);
                test(jv);
            }
            // XChainAddClaimAttestation
            {
                Json::Value const jv = claim_attestation(
                    alice,
                    jvb,
                    alice,
                    mpt,
                    alice,
                    true,
                    1,
                    alice,
                    signer(alice));
                test(jv);
            }
            // XChainAddAccountCreateAttestation
            {
                Json::Value const jv = create_account_attestation(
                    alice,
                    jvb,
                    alice,
                    mpt,
                    XRP(10),
                    alice,
                    false,
                    1,
                    alice,
                    signer(alice));
                test(jv);
            }
            // XChainAccountCreateCommit
            {
                Json::Value const jv = sidechain_xchain_account_create(
                    alice, jvb, alice, mpt, XRP(10));
                test(jv);
            }
            // XChain[Create|Modify]Bridge
            auto bridgeTx = [&](Json::StaticString const& tt,
                                bool minAmount = false) {
                Json::Value jv;
                jv[jss::TransactionType] = tt;
                jv[jss::Account] = alice.human();
                jv[sfXChainBridge.fieldName] = jvb;
                jv[sfSignatureReward.fieldName] =
                    mpt.getJson(JsonOptions::none);
                if (minAmount)
                    jv[sfMinAccountCreateAmount.fieldName] =
                        mpt.getJson(JsonOptions::none);
                test(jv);
            };
            bridgeTx(jss::XChainCreateBridge);
            bridgeTx(jss::XChainCreateBridge, true);
            bridgeTx(jss::XChainModifyBridge);
            bridgeTx(jss::XChainModifyBridge, true);
        }
        BEAST_EXPECT(txWithAmounts.empty());
    }

    void
    testTxJsonMetaFields(FeatureBitset features)
    {
        // checks synthetically parsed mptissuanceid from  `tx` response
        // it checks the parsing logic
        testcase("Test synthetic fields from tx response");

        using namespace test::jtx;

        Account const alice{"alice"};

        Env env{*this, features};
        MPTTester mptAlice(env, alice);

        mptAlice.create();

        std::string const txHash{
            env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

        Json::Value const meta = env.rpc("tx", txHash)[jss::result][jss::meta];

        // Expect mpt_issuance_id field
        BEAST_EXPECT(meta.isMember(jss::mpt_issuance_id));
        BEAST_EXPECT(
            meta[jss::mpt_issuance_id] == to_string(mptAlice.issuanceID()));
    }

    void
    testClawbackValidation(FeatureBitset features)
    {
        testcase("MPT clawback validations");
        using namespace test::jtx;

        // Make sure clawback cannot work when featureMPTokensV1 is disabled
        {
            Env env(*this, features - featureMPTokensV1);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const USD = alice["USD"];
            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), getMptID(alice.id(), env.seq(alice)));

            env(claw(alice, bob["USD"](5), bob), ter(temMALFORMED));
            env.close();

            env(claw(alice, mpt(5)), ter(temDISABLED));
            env.close();

            env(claw(alice, mpt(5), bob), ter(temDISABLED));
            env.close();
        }

        // Test preflight
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const USD = alice["USD"];
            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), getMptID(alice.id(), env.seq(alice)));

            // clawing back IOU from a MPT holder fails
            env(claw(alice, bob["USD"](5), bob), ter(temMALFORMED));
            env.close();

            // clawing back MPT without specifying a holder fails
            env(claw(alice, mpt(5)), ter(temMALFORMED));
            env.close();

            // clawing back zero amount fails
            env(claw(alice, mpt(0), bob), ter(temBAD_AMOUNT));
            env.close();

            // alice can't claw back from herself
            env(claw(alice, mpt(5), alice), ter(temMALFORMED));
            env.close();

            // can't clawback negative amount
            env(claw(alice, mpt(-1), bob), ter(temBAD_AMOUNT));
            env.close();
        }

        // Preclaim - clawback fails when MPTCanClawback is disabled on issuance
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            // enable asfAllowTrustLineClawback for alice
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(alice, asfAllowTrustLineClawback));

            // Create issuance without enabling clawback
            mptAlice.create({.ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &bob});

            mptAlice.pay(alice, bob, 100);

            // alice cannot clawback before she didn't enable MPTCanClawback
            // asfAllowTrustLineClawback has no effect
            mptAlice.claw(alice, bob, 1, tecNO_PERMISSION);
        }

        // Preclaim - test various scenarios
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(1000), carol);
            env.close();
            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            auto const fakeMpt = ripple::test::jtx::MPT(
                alice.name(), getMptID(alice.id(), env.seq(alice)));

            // issuer tries to clawback MPT where issuance doesn't exist
            env(claw(alice, fakeMpt(5), bob), ter(tecOBJECT_NOT_FOUND));
            env.close();

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

            // alice tries to clawback from someone who doesn't have MPToken
            mptAlice.claw(alice, bob, 1, tecOBJECT_NOT_FOUND);

            // bob creates a MPToken
            mptAlice.authorize({.account = &bob});

            // clawback fails because bob currently has a balance of zero
            mptAlice.claw(alice, bob, 1, tecINSUFFICIENT_FUNDS);

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            // carol fails tries to clawback from bob because he is not the
            // issuer
            mptAlice.claw(carol, bob, 1, tecNO_PERMISSION);
        }

        // clawback more than max amount
        // fails in the json parser before
        // transactor is called
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), getMptID(alice.id(), env.seq(alice)));

            Json::Value jv = claw(alice, mpt(1), bob);
            jv[jss::Amount][jss::value] = to_string(maxMPTokenAmount + 1);
            Json::Value jv1;
            jv1[jss::secret] = alice.name();
            jv1[jss::tx_json] = jv;
            auto const jrr = env.rpc("json", "submit", to_string(jv1));
            BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidParams");
        }
    }

    void
    testClawback(FeatureBitset features)
    {
        testcase("MPT Clawback");
        using namespace test::jtx;

        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

            // bob creates a MPToken
            mptAlice.authorize({.account = &bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            mptAlice.claw(alice, bob, 1);

            mptAlice.claw(alice, bob, 1000);

            // clawback fails because bob currently has a balance of zero
            mptAlice.claw(alice, bob, 1, tecINSUFFICIENT_FUNDS);
        }

        // Test that globally locked funds can be clawed
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanLock | tfMPTCanClawback});

            // bob creates a MPToken
            mptAlice.authorize({.account = &bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            mptAlice.set({.account = &alice, .flags = tfMPTLock});

            mptAlice.claw(alice, bob, 100);
        }

        // Test that individually locked funds can be clawed
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanLock | tfMPTCanClawback});

            // bob creates a MPToken
            mptAlice.authorize({.account = &bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            mptAlice.set(
                {.account = &alice, .holder = &bob, .flags = tfMPTLock});

            mptAlice.claw(alice, bob, 100);
        }

        // Test that unauthorized funds can be clawed back
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            // alice creates issuance
            mptAlice.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanClawback | tfMPTRequireAuth});

            // bob creates a MPToken
            mptAlice.authorize({.account = &bob});

            // alice authorizes bob
            mptAlice.authorize({.account = &alice, .holder = &bob});

            // alice pays bob 100 tokens
            mptAlice.pay(alice, bob, 100);

            // alice unauthorizes bob
            mptAlice.authorize(
                {.account = &alice, .holder = &bob, .flags = tfMPTUnauthorize});

            mptAlice.claw(alice, bob, 100);
        }
    }

    void
    testOfferCrossing(FeatureBitset features)
    {
        testcase("Offer Crossing");
        using namespace test::jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        auto const USD = gw["USD"];

        // MPTokenV2 is disabled
        {
            Env env{*this, features - featureMPTokensV2};

            MPTTester mpt(env, gw, {.holders = {&alice, &carol}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 200);

            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            env(offer(alice, XRP(100), mpt.mpt(101)), ter(temDISABLED));
            env.close();
        }

        // XRP/MPT
        {
            Env env{*this, features};

            MPTTester mpt(env, gw, {.holders = {&alice, &carol}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 200);

            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            env(offer(alice, XRP(100), MPT(101)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{XRP(100), MPT(101)}}}));

            env(offer(carol, MPT(101), XRP(100)));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mpt.checkMPTokenAmount(carol, 301));
        }

        // IOU/MPT
        {
            Env env{*this, features};

            MPTTester mpt(env, gw, {.holders = {&alice, &carol}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            env(trust(alice, USD(2'000)));
            env(pay(gw, alice, USD(1'000)));
            env.close();

            env(trust(carol, USD(2'000)));
            env(pay(gw, carol, USD(1'000)));
            env.close();

            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 200);

            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            env(offer(alice, USD(100), MPT(101)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{USD(100), MPT(101)}}}));

            env(offer(carol, MPT(101), USD(100)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == USD(1'100));
            BEAST_EXPECT(env.balance(carol, USD) == USD(900));
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mpt.checkMPTokenAmount(carol, 301));
        }

        // MPT/MPT
        {
            Env env{*this, features};

            MPTTester mpt1(env, gw, {.holders = {&alice, &carol}});
            mpt1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT1 = mpt1["MPT1"];

            MPTTester mpt2(
                env, gw, {.holders = {&alice, &carol}, .fund = false});
            mpt2.create(
                {.ownerCount = 2, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT2 = mpt2["MPT2"];

            mpt1.authorize({.account = &alice});
            mpt1.authorize({.account = &carol});
            mpt1.pay(gw, alice, 200);
            mpt1.pay(gw, carol, 200);

            mpt2.authorize({.account = &alice});
            mpt2.authorize({.account = &carol});
            mpt2.pay(gw, alice, 200);
            mpt2.pay(gw, carol, 200);

            env(offer(alice, MPT2(100), MPT1(101)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{MPT2(100), MPT1(101)}}}));

            env(offer(carol, MPT1(101), MPT2(100)));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(mpt1.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mpt1.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mpt1.checkMPTokenAmount(carol, 301));
            BEAST_EXPECT(mpt2.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mpt2.checkMPTokenAmount(alice, 300));
            BEAST_EXPECT(mpt2.checkMPTokenAmount(carol, 100));
        }
    }

    void
    testCrossAssetPayment(FeatureBitset features)
    {
        testcase("Cross Asset Payment");
        using namespace test::jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        Account const bob = Account("bob");
        auto const USD = gw["USD"];

        // MPT/XRP
        {
            Env env{*this, features};
            MPTTester mpt(env, gw, {.holders = {&alice, &carol, &bob}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 200);

            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            mpt.authorize({.account = &bob});

            env(offer(alice, XRP(100), MPT(101)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{XRP(100), MPT(101)}}}));

            env(pay(carol, bob, MPT(101)),
                test::jtx::path(~MPT),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mpt.checkMPTokenAmount(bob, 101));
        }

        // MPT/IOU
        {
            Env env{*this, features};

            MPTTester mpt(env, gw, {.holders = {&alice, &carol, &bob}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            env(trust(alice, USD(2'000)));
            env(pay(gw, alice, USD(1'000)));
            env(trust(bob, USD(2'000)));
            env(pay(gw, bob, USD(1'000)));
            env(trust(carol, USD(2'000)));
            env(pay(gw, carol, USD(1'000)));
            env.close();

            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 200);

            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            mpt.authorize({.account = &bob});

            env(offer(alice, USD(100), MPT(101)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{USD(100), MPT(101)}}}));

            env(pay(carol, bob, MPT(101)),
                test::jtx::path(~MPT),
                sendmax(USD(100)),
                txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(env.balance(carol, USD) == USD(900));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mpt.checkMPTokenAmount(bob, 101));
        }

        // IOU/MPT
        {
            Env env{*this, features};

            MPTTester mpt(env, gw, {.holders = {&alice, &carol, &bob}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            env(trust(alice, USD(2'000)), txflags(tfClearNoRipple));
            env(pay(gw, alice, USD(1'000)));
            env(trust(bob, USD(2'000)), txflags(tfClearNoRipple));
            env.close();

            mpt.authorize({.account = &alice});
            env(pay(gw, alice, MPT(200)));

            mpt.authorize({.account = &carol});
            env(pay(gw, carol, MPT(200)));

            env(offer(alice, MPT(101), USD(100)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{MPT(101), USD(100)}}}));

            env(pay(carol, bob, USD(100)),
                test::jtx::path(~USD),
                sendmax(MPT(101)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(env.balance(alice, USD) == USD(900));
            BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 301));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(400));
            BEAST_EXPECT(mpt.checkMPTokenAmount(carol, 99));
            BEAST_EXPECT(env.balance(bob, USD) == USD(100));
        }

        // MPT/MPT
        {
            Env env{*this, features};

            MPTTester mpt1(env, gw, {.holders = {&alice, &carol, &bob}});
            mpt1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT1 = mpt1["MPT1"];

            MPTTester mpt2(
                env, gw, {.holders = {&alice, &carol, &bob}, .fund = false});
            mpt2.create(
                {.ownerCount = 2, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT2 = mpt2["MPT2"];

            mpt1.authorize({.account = &alice});
            mpt1.pay(gw, alice, 200);
            mpt2.authorize({.account = &alice});

            mpt2.authorize({.account = &carol});
            mpt2.pay(gw, carol, 200);

            mpt1.authorize({.account = &bob});

            env(offer(alice, MPT2(100), MPT1(101)));
            env.close();
            BEAST_EXPECT(
                expectOffers(env, alice, 1, {{Amounts{MPT2(100), MPT1(101)}}}));

            env(pay(carol, bob, MPT1(101)),
                test::jtx::path(~MPT1),
                sendmax(MPT2(100)),
                txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(mpt1.checkMPTokenOutstandingAmount(200));
            BEAST_EXPECT(mpt2.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mpt1.checkMPTokenAmount(alice, 99));
            BEAST_EXPECT(mpt1.checkMPTokenAmount(bob, 101));
        }

        // XRP/MPT AMM
        {
            Env env{*this, features};

            fund(env, gw, {alice, carol, bob}, XRP(11'000), {USD(20'000)});

            MPTTester mpt(env, gw, {.fund = false});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &bob});
            mpt.pay(gw, alice, 10'100);

            AMM amm(env, alice, XRP(10'000), MPT(10'100));

            env(pay(carol, bob, MPT(100)),
                test::jtx::path(~MPT),
                sendmax(XRP(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(
                amm.expectBalances(XRP(10'100), MPT(10'000), amm.tokens()));
            BEAST_EXPECT(mpt.checkMPTokenAmount(bob, 100));
        }

        // IOU/MPT AMM
        {
            Env env{*this, features};

            fund(env, gw, {alice, carol, bob}, XRP(11'000), {USD(20'000)});

            MPTTester mpt(env, gw, {.fund = false});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &bob});
            mpt.pay(gw, alice, 10'100);

            AMM amm(env, alice, USD(10'000), MPT(10'100));

            env(pay(carol, bob, MPT(100)),
                test::jtx::path(~MPT),
                sendmax(USD(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(
                amm.expectBalances(USD(10'100), MPT(10'000), amm.tokens()));
            BEAST_EXPECT(mpt.checkMPTokenAmount(bob, 100));
        }

        // MPT/MPT AMM cross-asset payment
        {
            Env env{*this, features};
            env.fund(XRP(20'000), gw, alice, carol, bob);
            env.close();

            MPTTester mpt1(env, gw, {.fund = false});
            mpt1.create({.flags = tfMPTCanTransfer});
            auto const MPT1 = mpt1["MPT1"];
            mpt1.authorize({.account = &alice});
            mpt1.authorize({.account = &bob});
            mpt1.pay(gw, alice, 10'100);

            MPTTester mpt2(env, gw, {.fund = false});
            mpt2.create({.flags = tfMPTCanTransfer});
            auto const MPT2 = mpt2["MPT1"];
            mpt2.authorize({.account = &alice});
            mpt2.authorize({.account = &bob});
            mpt2.authorize({.account = &carol});
            mpt2.pay(gw, alice, 10'100);
            mpt2.pay(gw, carol, 100);

            AMM amm(env, alice, MPT2(10'000), MPT1(10'100));

            env(pay(carol, bob, MPT1(100)),
                test::jtx::path(~MPT1),
                sendmax(MPT2(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(
                amm.expectBalances(MPT2(10'100), MPT1(10'000), amm.tokens()));
            BEAST_EXPECT(mpt1.checkMPTokenAmount(bob, 100));
        }

        // Multi-steps with AMM
        // IOU/MPT1 MPT1/MPT2 MPT2/IOU IOU/IOU AMM:IOU/MPT MPT/IOU
        {
            Env env{*this, features};
            auto const USD = gw["USD"];
            auto const EUR = gw["EUR"];
            auto const CRN = gw["CRN"];
            auto const YAN = gw["YAN"];

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(1'000),
                {USD(1'000), EUR(1'000), CRN(2'000), YAN(1'000)});

            auto createMPT = [&]() -> std::pair<MPTTester, MPT> {
                MPTTester mpt(env, gw, {.fund = false});
                mpt.create({.flags = tfMPTCanTransfer});
                mpt.authorize({.account = &alice});
                mpt.pay(gw, alice, 2'000);
                return {mpt, mpt["MPT"]};
            };

            auto const [mpt1, MPT1] = createMPT();
            auto const [mpt2, MPT2] = createMPT();
            auto const [mpt3, MPT3] = createMPT();

            env(offer(alice, EUR(100), MPT1(101)));
            env(offer(alice, MPT1(101), MPT2(102)));
            env(offer(alice, MPT2(102), USD(103)));
            env(offer(alice, USD(103), CRN(104)));
            env.close();
            AMM amm(env, alice, CRN(1'000), MPT3(1'104));
            env(offer(alice, MPT3(104), YAN(100)));

            env(pay(carol, bob, YAN(100)),
                test::jtx::path(~MPT1, ~MPT2, ~USD, ~CRN, ~MPT3, ~YAN),
                sendmax(EUR(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(env.balance(bob, YAN) == YAN(1'100));
            BEAST_EXPECT(
                amm.expectBalances(CRN(1'104), MPT3(1'000), amm.tokens()));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
    }

    void
    testPath(FeatureBitset features)
    {
        testcase("Path");
        using namespace test::jtx;
        Account const gw{"gw"};
        Account const gw1{"gw1"};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const bob{"bob"};
        Account const dan{"dan"};
        auto const USD = gw["USD"];
        auto const EUR = gw1["EUR"];

        // MPT can be a mpt end point step or a book-step

        // Direct MPT payment
        {
            Env env = pathTestEnv(*this);

            MPTTester mpt(env, gw, {.holders = {&dan, &carol}});
            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];
            mpt.authorize({.account = &dan});
            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            auto const [pathSet, srcAmt, dstAmt] =
                find_paths(env, carol, dan, MPT(-1));
            BEAST_EXPECT(srcAmt == MPT(200));
            BEAST_EXPECT(dstAmt == MPT(200));
            // Direct payment, no path
            BEAST_EXPECT(pathSet.empty());
        }

        // Cross-asset payment via XRP/MPT offer (one step)
        {
            Env env = pathTestEnv(*this);

            env.fund(XRP(1'000), carol);

            MPTTester mpt(env, gw, {.holders = {&alice, &dan}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &dan});
            mpt.pay(gw, alice, 200);

            env(offer(alice, XRP(100), MPT(100)));
            env.close();

            auto const [pathSet, srcAmt, dstAmt] =
                find_paths(env, carol, dan, MPT(-1));
            BEAST_EXPECT(srcAmt == XRP(100));
            BEAST_EXPECT(dstAmt == MPT(100));
            // This path is consistent with XRP/IOU.
            BEAST_EXPECT(same(pathSet, stpath(IPE(mpt.issuanceID()))));
        }

        // Cross-asset payment via IOU/MPT offer (one step)
        {
            Env env = pathTestEnv(*this);

            env.fund(XRP(1'000), carol);
            env.fund(XRP(1'000), gw);

            MPTTester mpt(env, gw1, {.holders = {&alice, &dan}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &dan});
            mpt.pay(gw1, alice, 200);

            env(trust(alice, USD(400)));
            env(trust(carol, USD(400)));
            env(pay(gw, carol, USD(200)));

            env(offer(alice, USD(100), MPT(100)));
            env.close();

            auto const [pathSet, srcAmt, dstAmt] =
                find_paths(env, carol, dan, MPT(-1));
            BEAST_EXPECT(srcAmt == USD(100));
            BEAST_EXPECT(dstAmt == MPT(100));
            // This path is consistent with IOU1/gw1 / IOU/gw
            BEAST_EXPECT(same(pathSet, stpath(gw, IPE(mpt.issuanceID()))));
        }

        // Cross-asset payment via MPT1/MPT offer (one step)
        {
            Env env = pathTestEnv(*this);

            MPTTester mpt(env, gw, {.holders = {&alice, &dan}});
            MPTTester mpt1(env, gw1, {.holders = {&carol}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];
            mpt1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT1 = mpt1["MPT1"];

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &dan});
            mpt.pay(gw, alice, 200);

            mpt1.authorize({.account = &carol});
            mpt1.authorize({.account = &alice});
            mpt1.pay(gw1, carol, 200);

            env(offer(alice, MPT1(100), MPT(100)));
            env.close();

            auto const [pathSet, srcAmt, dstAmt] =
                find_paths(env, carol, dan, MPT(-1));
            BEAST_EXPECT(srcAmt == MPT1(100));
            BEAST_EXPECT(dstAmt == MPT(100));
            // This path is consistent with IOU1/gw / IOU/gw path -
            // [gw1, IOU/gw], except for gw1. This is due to no MPT rippling
            BEAST_EXPECT(same(pathSet, stpath(IPE(mpt.issuanceID()))));
        }

        // Cross-asset payment via offers (two steps)
        {
            Env env = pathTestEnv(*this);

            env.fund(XRP(1'000), carol);
            env.fund(XRP(1'000), dan);

            MPTTester mpt(env, gw, {.holders = {&alice, &bob}});

            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &bob});
            mpt.pay(gw, alice, 200);
            mpt.pay(gw, bob, 200);

            env(trust(bob, USD(200)));
            env(pay(gw, bob, USD(100)));
            env(trust(dan, USD(200)));
            env(trust(alice, USD(200)));

            env(offer(alice, XRP(100), MPT(100)));
            env(offer(bob, MPT(100), USD(100)));
            env.close();

            auto const [pathSet, srcAmt, dstAmt] =
                find_paths(env, carol, dan, USD(-1));
            BEAST_EXPECT(srcAmt == XRP(100));
            BEAST_EXPECT(dstAmt == USD(100));
            // This path is consistent with XRP/ IOU1/gw - IOU1/gw1 / IOU/gw
            BEAST_EXPECT(
                same(pathSet, stpath(IPE(mpt.issuanceID()), IPE(USD))));
        }

        // Cross-asset payment via offers (two steps)
        // Start/End with mpt/mp1 and book steps in the middle
        {
            Env env = pathTestEnv(*this);
            Account const gw2{"gw2"};
            env.fund(XRP(1'000), gw2);
            auto const USD2 = gw2["USD"];

            MPTTester mpt(env, gw, {.holders = {&alice, &carol}});
            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];
            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            MPTTester mpt1(env, gw1, {.holders = {&bob, &dan}});
            mpt1.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT1 = mpt1["MPT1"];
            mpt1.authorize({.account = &bob});
            mpt1.pay(gw1, bob, 200);
            mpt1.authorize({.account = &dan});

            env(trust(alice, USD2(400)));
            env(pay(gw2, alice, USD2(200)));
            env(trust(bob, USD2(400)));

            env(offer(alice, MPT(100), USD2(100)));
            env(offer(bob, USD2(100), MPT1(100)));
            env.close();

            auto const [pathSet, srcAmt, dstAmt] =
                find_paths(env, carol, dan, MPT1(-1));
            BEAST_EXPECT(srcAmt == MPT(100));
            BEAST_EXPECT(dstAmt == MPT1(100));
            // This path is consistent with IOU/gw / IOU/gw2 -
            // IOU/gw2 / IOU1/gw1 path -
            // [gw, IOU2/gw2, IOU1/gw1], except for gw.
            // This is due to no MPT rippling
            BEAST_EXPECT(
                same(pathSet, stpath(IPE(USD2), IPE(mpt1.issuanceID()))));
        }
    }

    void
    testCheck(FeatureBitset features)
    {
        testcase("Check Create/Cash");

        using namespace test::jtx;
        Account const gw{"gw"};
        Account const alice{"alice"};

        // MPTokensV2 is disabled
        {
            Env env{*this, features - featureMPTokensV2};

            MPTTester mpt(env, gw, {.holders = {&alice}});
            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];
            mpt.authorize({.account = &alice});

            uint256 const checkId{keylet::check(gw, env.seq(gw)).key};

            env(check::create(gw, alice, MPT(100)), ter(temDISABLED));
            env.close();

            env(check::cash(alice, checkId, MPT(100)), ter(temDISABLED));
            env.close();
        }

        // Insufficient funds
        {
            Env env{*this, features};
            Account const carol{"carol"};

            MPTTester mpt(env, gw, {.holders = {&alice, &carol}});
            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];
            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 50);

            uint256 const checkId{keylet::check(alice, env.seq(alice)).key};

            // can create
            env(check::create(alice, carol, MPT(100)));
            env.close();

            // can't cash since alice only has 50 of MPT
            env(check::cash(carol, checkId, MPT(100)), ter(tecPATH_PARTIAL));
            env.close();

            // can cash if DeliverMin is set
            // carol is not authorized, MPToken is authorized by CheckCash
            env(check::cash(carol, checkId, check::DeliverMin(MPT(50))));
            env.close();
            BEAST_EXPECT(mpt.checkMPTokenAmount(carol, 50));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(50));
        }

        // Exceed max amount
        {
            Env env{*this, features};

            MPTTester mpt(env, gw, {.holders = {&alice}});
            mpt.create(
                {.maxAmt = "100",
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];

            uint256 const checkId{keylet::check(gw, env.seq(gw)).key};

            // can create
            env(check::create(gw, alice, MPT(200)));
            env.close();

            // can't cash since the outstanding amount exceeds max amount
            env(check::cash(alice, checkId, MPT(200)), ter(tecPATH_PARTIAL));
            env.close();

            // can cash if DeliverMin is set
            env(check::cash(alice, checkId, check::DeliverMin(MPT(100))));
            env.close();
            BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(100));
        }

        // Normal create/cash
        {
            Env env{*this, features};

            MPTTester mpt(env, gw, {.holders = {&alice}});
            mpt.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            auto const MPT = mpt["MPT"];
            mpt.authorize({.account = &alice});

            uint256 const checkId{keylet::check(gw, env.seq(gw)).key};

            env(check::create(gw, alice, MPT(100)));
            env.close();

            env(check::cash(alice, checkId, MPT(100)));
            env.close();

            BEAST_EXPECT(mpt.checkMPTokenAmount(alice, 100));
            BEAST_EXPECT(mpt.checkMPTokenOutstandingAmount(100));
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};

        // MPTokenIssuanceCreate
        testCreateValidation(all);
        testCreateEnabled(all);

        // MPTokenIssuanceDestroy
        testDestroyValidation(all);
        testDestroyEnabled(all);

        // MPTokenAuthorize
        testAuthorizeValidation(all);
        testAuthorizeEnabled(all);

        // MPTokenIssuanceSet
        testSetValidation(all);
        testSetEnabled(all);

        // MPT clawback
        testClawbackValidation(all);
        testClawback(all);

        // Test Direct Payment
        testPayment(all);

        // Test MPT Amount is invalid in Tx, which don't support MPT
        testMPTInvalidInTx(all);

        // Test parsed MPTokenIssuanceID in API response metadata
        testTxJsonMetaFields(all);

        // Test offer crossing
        testOfferCrossing(all);

        // Test cross asset payment
        testCrossAssetPayment(all);

        // Test path finding
        testPath(all);

        // Test checks
        testCheck(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(MPToken, tx, ripple, 2);

}  // namespace ripple
