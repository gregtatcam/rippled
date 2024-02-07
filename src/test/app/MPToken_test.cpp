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
#include <test/jtx/AMMTest.h>

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
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = 1,
                 .metadata = "test",
                 .err = temMALFORMED});

            // tries to set a txfee while not enabling transfer
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = maxTransferFee + 1,
                 .metadata = "test",
                 .flags = tfMPTCanTransfer,
                 .err = temBAD_MPTOKEN_TRANSFER_FEE});

            // empty metadata returns error
            mptAlice.create(
                {.maxAmt = 100,
                 .assetScale = 0,
                 .transferFee = 0,
                 .metadata = "",
                 .err = temMALFORMED});

            // MaximumAmout of 0 returns error
            mptAlice.create(
                {.maxAmt = 0,
                 .assetScale = 1,
                 .transferFee = 1,
                 .metadata = "test",
                 .err = temMALFORMED});

            // MaximumAmount larger than 63 bit returns error
            mptAlice.create(
                {.maxAmt = 0xFFFFFFFFFFFFFFF0ull,
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
                {.maxAmt = 0x7FFFFFFFFFFFFFFF,
                 .assetScale = 1,
                 .transferFee = 10,
                 .metadata = "123",
                 .ownerCount = 1,
                 .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow |
                     tfMPTCanTrade | tfMPTCanTransfer | tfMPTCanClawback});
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
                {.account = &bob, .holder = &alice, .err = temMALFORMED});

            mptAlice.authorize(
                {.account = &bob, .holder = &bob, .err = temMALFORMED});

            // alice tries to hold onto her own token
            mptAlice.authorize({.account = &alice, .err = temMALFORMED});

            // alice tries to authorize herself
            mptAlice.authorize({.holder = &alice, .err = temMALFORMED});

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
            mptAlice.authorize({.err = temMALFORMED});

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

            mptAlice.create({.ownerCount = 1, .holderCount = 0});

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

        // If allowlisting is enabled, Payment fails if the receiver is not
        // authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTRequireAuth});

            mptAlice.authorize({.account = &bob});

            mptAlice.pay(alice, bob, 100, tecNO_AUTH);
        }

        // If allowlisting is enabled, Payment fails if the sender is not
        // authorized
        {
            Env env{*this, features};

            MPTTester mptAlice(env, alice, {.holders = {&bob}});

            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTRequireAuth});

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

            mptAlice.create({.ownerCount = 1});

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

            mptAlice.create({.ownerCount = 1, .flags = tfMPTCanLock});

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

            mptAlice.create({.maxAmt = 100, .ownerCount = 1, .holderCount = 0});

            mptAlice.authorize({.account = &bob});

            // issuer sends holder the max amount allowed
            mptAlice.pay(alice, bob, 100);

            // issuer tries to exceed max amount
            // TODO MPT: should it be tecMPT_MAX_AMOUNT_EXCEEDED?
            mptAlice.pay(alice, bob, 1, tecPATH_PARTIAL);
        }

        // TODO: This test is currently failing! Modify the STAmount to change
        // the range
        // Issuer fails trying to send more than the default maximum
        // amount allowed
        // {
        //     Env env{*this, features};

        //     MPTTester mptAlice(env, alice, {.holders = {&bob}});

        //     mptAlice.create({.ownerCount = 1, .holderCount = 0});

        //     mptAlice.authorize({.account = &bob});

        //     // issuer sends holder the default max amount allowed
        //     mptAlice.pay(alice, bob, maxMPTokenAmount);

        //     // issuer tries to exceed max amount
        //     mptAlice.pay(alice, bob, 1, tecMPT_MAX_AMOUNT_EXCEEDED);
        // }

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
            mptAlice.pay(bob, alice, 1'000);

            // Payment between the holders. The sender doesn't have
            // enough funds to cover the transfer fee.
            mptAlice.pay(bob, carol, 1'000, tecPATH_PARTIAL);

            // Payment between the holders. The sender pays 10% transfer fee.
            env(pay(bob, carol, mptAlice.mpt(100)), sendmax(mptAlice.mpt(125)));
        }
    }

    void
    testMPTInvalidInTx(FeatureBitset features)
    {
        testcase("MPT Amount Invalid in Transaction");
        using namespace test::jtx;
        Env env{*this, features};
        Account const alice("alice");  // issuer

        MPTTester mptAlice(env, alice);

        mptAlice.create();

        env(check::create(env.master, alice, mptAlice.mpt(100)),
            ter(temINVALID));
        env.close();

        BEAST_EXPECT(expectOffers(env, alice, 0));
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
    testOfferCrossing(FeatureBitset features)
    {
        testcase("Offer Crossing");
        using namespace test::jtx;
        Account const gw = Account("gw");
        Account const alice = Account("alice");
        Account const carol = Account("carol");
        auto const USD = gw["USD"];

        // XRP/MPT
        {
            Env env{*this, features};

            MPTTester mpt(env, gw, {.holders = {&alice, &carol}});

            mpt.create({.ownerCount = 1, .holderCount = 0});

            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 200);

            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            env(offer(alice, XRP(100), mpt.mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{XRP(100), mpt.mpt(101)}}}));

            env(offer(carol, mpt.mpt(101), XRP(100)));
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

            mpt.create({.ownerCount = 1, .holderCount = 0});

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

            env(offer(alice, USD(100), mpt.mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{USD(100), mpt.mpt(101)}}}));

            env(offer(carol, mpt.mpt(101), USD(100)));
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
            mpt1.create({.ownerCount = 1, .holderCount = 0});

            MPTTester mpt2(
                env, gw, {.holders = {&alice, &carol}, .fund = false});
            mpt2.create({.ownerCount = 2, .holderCount = 0});

            mpt1.authorize({.account = &alice});
            mpt1.authorize({.account = &carol});
            mpt1.pay(gw, alice, 200);
            mpt1.pay(gw, carol, 200);

            mpt2.authorize({.account = &alice});
            mpt2.authorize({.account = &carol});
            mpt2.pay(gw, alice, 200);
            mpt2.pay(gw, carol, 200);

            env(offer(alice, mpt2.mpt(100), mpt1.mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{mpt2.mpt(100), mpt1.mpt(101)}}}));

            env(offer(carol, mpt1.mpt(101), mpt2.mpt(100)));
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

            mpt.create({.ownerCount = 1, .holderCount = 0});

            mpt.authorize({.account = &alice});
            mpt.pay(gw, alice, 200);

            mpt.authorize({.account = &carol});
            mpt.pay(gw, carol, 200);

            mpt.authorize({.account = &bob});

            env(offer(alice, XRP(100), mpt.mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{XRP(100), mpt.mpt(101)}}}));

            env(pay(carol, bob, mpt.mpt(101)),
                test::jtx::path(~mpt.MPT()),
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

            mpt.create({.ownerCount = 1, .holderCount = 0});

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

            env(offer(alice, USD(100), mpt.mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{USD(100), mpt.mpt(101)}}}));

            env(pay(carol, bob, mpt.mpt(101)),
                test::jtx::path(~mpt.MPT()),
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

            mpt.create({.ownerCount = 1, .holderCount = 0});

            env(trust(alice, USD(2'000)), txflags(tfClearNoRipple));
            env(pay(gw, alice, USD(1'000)));
            env(trust(bob, USD(2'000)), txflags(tfClearNoRipple));
            env.close();

            mpt.authorize({.account = &alice});
            env(pay(gw, alice, mpt.mpt(200)));

            mpt.authorize({.account = &carol});
            env(pay(gw, carol, mpt.mpt(200)));

            env(offer(alice, mpt.mpt(101), USD(100)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{mpt.mpt(101), USD(100)}}}));

            env(pay(carol, bob, USD(100)),
                test::jtx::path(~USD),
                sendmax(mpt.mpt(101)),
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
            mpt1.create({.ownerCount = 1, .holderCount = 0});

            MPTTester mpt2(
                env, gw, {.holders = {&alice, &carol, &bob}, .fund = false});
            mpt2.create({.ownerCount = 2, .holderCount = 0});

            mpt1.authorize({.account = &alice});
            mpt1.pay(gw, alice, 200);
            mpt2.authorize({.account = &alice});

            mpt2.authorize({.account = &carol});
            mpt2.pay(gw, carol, 200);

            mpt1.authorize({.account = &bob});

            env(offer(alice, mpt2.mpt(100), mpt1.mpt(101)));
            env.close();
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{Amounts{mpt2.mpt(100), mpt1.mpt(101)}}}));

            env(pay(carol, bob, mpt1.mpt(101)),
                test::jtx::path(~mpt1.MPT()),
                sendmax(mpt2.mpt(100)),
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

            mpt.create({.ownerCount = 1, .holderCount = 0});

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &bob});
            mpt.pay(gw, alice, 10'100);

            AMM amm(env, alice, XRP(10'000), mpt.mpt(10'100));

            env(pay(carol, bob, mpt.mpt(100)),
                test::jtx::path(~mpt.MPT()),
                sendmax(XRP(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(
                amm.expectBalances(XRP(10'100), mpt.mpt(10'000), amm.tokens()));
            BEAST_EXPECT(mpt.checkMPTokenAmount(bob, 100));
        }

        // IOU/MPT AMM
        {
            Env env{*this, features};

            fund(env, gw, {alice, carol, bob}, XRP(11'000), {USD(20'000)});

            MPTTester mpt(env, gw, {.fund = false});

            mpt.create({.ownerCount = 1, .holderCount = 0});

            mpt.authorize({.account = &alice});
            mpt.authorize({.account = &bob});
            mpt.pay(gw, alice, 10'100);

            AMM amm(env, alice, USD(10'000), mpt.mpt(10'100));

            env(pay(carol, bob, mpt.mpt(100)),
                test::jtx::path(~mpt.MPT()),
                sendmax(USD(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(
                amm.expectBalances(USD(10'100), mpt.mpt(10'000), amm.tokens()));
            BEAST_EXPECT(mpt.checkMPTokenAmount(bob, 100));
        }

        // MPT/MPT AMM cross-asset payment
        {
            Env env{*this, features};
            env.fund(XRP(20'000), gw, alice, carol, bob);
            env.close();

            MPTTester mpt1(env, gw, {.fund = false});
            mpt1.create();
            mpt1.authorize({.account = &alice});
            mpt1.authorize({.account = &bob});
            mpt1.pay(gw, alice, 10'100);

            MPTTester mpt2(env, gw, {.fund = false});
            mpt2.create();
            mpt2.authorize({.account = &alice});
            mpt2.authorize({.account = &bob});
            mpt2.authorize({.account = &carol});
            mpt2.pay(gw, alice, 10'100);
            mpt2.pay(gw, carol, 100);

            AMM amm(env, alice, mpt2.mpt(10'000), mpt1.mpt(10'100));

            env(pay(carol, bob, mpt1.mpt(100)),
                test::jtx::path(~mpt1.MPT()),
                sendmax(mpt2.mpt(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                mpt2.mpt(10'100), mpt1.mpt(10'000), amm.tokens()));
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

            auto createMPT = [&]() -> MPTTester {
                MPTTester mpt(env, gw, {.fund = false});
                mpt.create();
                mpt.authorize({.account = &alice});
                mpt.pay(gw, alice, 2'000);
                return mpt;
            };

            auto mpt1 = createMPT();
            auto mpt2 = createMPT();
            auto mpt3 = createMPT();

            env(offer(alice, EUR(100), mpt1.mpt(101)));
            env(offer(alice, mpt1.mpt(101), mpt2.mpt(102)));
            env(offer(alice, mpt2.mpt(102), USD(103)));
            env(offer(alice, USD(103), CRN(104)));
            env.close();
            AMM amm(env, alice, CRN(1'000), mpt3.mpt(1'104));
            env(offer(alice, mpt3.mpt(104), YAN(100)));

            env(pay(carol, bob, YAN(100)),
                test::jtx::path(
                    ~mpt1.MPT(), ~mpt2.MPT(), ~USD, ~CRN, ~mpt3.MPT(), ~YAN),
                sendmax(EUR(100)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            env.close();

            BEAST_EXPECT(env.balance(bob, YAN) == YAN(1'100));
            BEAST_EXPECT(
                amm.expectBalances(CRN(1'104), mpt3.mpt(1'000), amm.tokens()));
            BEAST_EXPECT(expectOffers(env, alice, 0));
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

        // Test Direct Payment
        testPayment(all);
        // Integration with DEX
        testOfferCrossing(all);
        testCrossAssetPayment(all);

        // Test MPT Amount is invalid in non-Payment Tx
        testMPTInvalidInTx(all);

        // Test parsed MPTokenIssuanceID in API response metadata
        // TODO: This test exercises the parsing logic of mptID in `tx`,
        // but,
        //       mptID is also parsed in different places like `account_tx`,
        //       `subscribe`, `ledger`. We should create test for these
        //       occurances (lower prioirity).
        testTxJsonMetaFields(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(MPToken, tx, ripple, 2);

}  // namespace ripple
