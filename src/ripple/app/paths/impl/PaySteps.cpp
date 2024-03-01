//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/basics/contract.h>
#include <ripple/json/json_writer.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/Feature.h>

#include <algorithm>
#include <numeric>
#include <sstream>

namespace ripple {

// Check equal with tolerance
bool
checkNear(IOUAmount const& expected, IOUAmount const& actual)
{
    double const ratTol = 0.001;
    if (abs(expected.exponent() - actual.exponent()) > 1)
        return false;

    if (actual.exponent() < -20)
        return true;

    auto const a = (expected.exponent() < actual.exponent())
        ? expected.mantissa() / 10
        : expected.mantissa();
    auto const b = (actual.exponent() < expected.exponent())
        ? actual.mantissa() / 10
        : actual.mantissa();
    if (a == b)
        return true;

    double const diff = std::abs(a - b);
    auto const r = diff / std::max(std::abs(a), std::abs(b));
    return r <= ratTol;
};

bool
checkNear(XRPAmount const& expected, XRPAmount const& actual)
{
    return expected == actual;
};

bool
checkNear(MPTAmount const& expected, MPTAmount const& actual)
{
    return expected == actual;
};

static bool
isXRPAccount(STPathElement const& pe)
{
    if (pe.getNodeType() != STPathElement::typeAccount)
        return false;
    return isXRP(pe.getAccountID());
};

static std::pair<TER, std::unique_ptr<Step>>
toStep(
    StrandContext const& ctx,
    STPathElement const* e1,
    STPathElement const* e2,
    Asset const& curAsset)
{
    auto& j = ctx.j;

    if (ctx.isFirst && e1->isAccount() &&
        (e1->getNodeType() & STPathElement::typeCurrency) &&
        e1->getPathAsset().isXRP())
    {
        return make_XRPEndpointStep(ctx, e1->getAccountID());
    }

    if (ctx.isLast && isXRPAccount(*e1) && e2->isAccount())
        return make_XRPEndpointStep(ctx, e2->getAccountID());

    if (e1->isAccount() && e2->isAccount())
    {
        if (curAsset.isMPT())
            return make_MPTEndpointStep(
                ctx,
                e1->getAccountID(),
                e2->getAccountID(),
                curAsset.mptIssue().mpt());
        return make_DirectStepI(
            ctx,
            e1->getAccountID(),
            e2->getAccountID(),
            curAsset.issue().currency);
    }

    if (e1->isOffer() && e2->isAccount())
    {
        // should already be taken care of
        JLOG(j.error())
            << "Found offer/account payment step. Aborting payment strand.";
        assert(0);
        return {temBAD_PATH, std::unique_ptr<Step>{}};
    }

    assert(
        (e2->getNodeType() & STPathElement::typeAsset) ||
        (e2->getNodeType() & STPathElement::typeIssuer));
    auto const outAsset = e2->getNodeType() & STPathElement::typeAsset
        ? e2->getPathAsset()
        : curAsset;
    auto const outIssuer = e2->getNodeType() & STPathElement::typeIssuer
        ? e2->getIssuerID()
        : curAsset.account();

    if (isXRP(curAsset) && outAsset.isXRP())
    {
        JLOG(j.info()) << "Found xrp/xrp offer payment step";
        return {temBAD_PATH, std::unique_ptr<Step>{}};
    }

    assert(e2->isOffer());

    if (outAsset.isXRP())
    {
        if (curAsset.isMPT())
            return make_BookStepMX(ctx, curAsset);
        return make_BookStepIX(ctx, curAsset);
    }

    if (isXRP(curAsset))
    {
        if (outAsset.isMPT())
            return make_BookStepXM(ctx, outAsset.mpt());
        return make_BookStepXI(ctx, {outAsset.currency(), outIssuer});
    }

    if (curAsset.isMPT() && outAsset.isCurrency())
        return make_BookStepMI(
            ctx, curAsset.mptIssue(), {outAsset.currency(), outIssuer});
    if (curAsset.isIssue() && outAsset.isMPT())
        return make_BookStepIM(ctx, curAsset.issue(), outAsset.mpt());

    if (curAsset.isMPT())
        return make_BookStepMM(ctx, curAsset.mptIssue(), outAsset.mpt());
    return make_BookStepII(
        ctx, curAsset.issue(), {outAsset.currency(), outIssuer});
}

std::pair<TER, Strand>
toStrand(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Asset const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Asset> const& sendMaxAsset,
    STPath const& path,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    beast::Journal j)
{
    if (isXRP(src) || isXRP(dst) || !isConsistent(deliver) ||
        (sendMaxAsset && !isConsistent(*sendMaxAsset)))
        return {temBAD_PATH, Strand{}};

    if ((sendMaxAsset && sendMaxAsset->account() == noAccount()) ||
        (src == noAccount()) || (dst == noAccount()) ||
        (deliver.account() == noAccount()))
        return {temBAD_PATH, Strand{}};

    if ((deliver.isMPT() && deliver.account() == beast::zero) ||
        (sendMaxAsset && sendMaxAsset->isMPT() &&
         sendMaxAsset->account() == beast::zero))
        return {temBAD_PATH, Strand{}};

    for (auto const& pe : path)
    {
        auto const t = pe.getNodeType();

        if ((t & ~STPathElement::typeAll) || !t)
            return {temBAD_PATH, Strand{}};

        bool const hasAccount = t & STPathElement::typeAccount;
        bool const hasIssuer = t & STPathElement::typeIssuer;
        bool const hasCurrency = t & STPathElement::typeCurrency;
        bool const hasMPT = t & STPathElement::typeMPT;

        if (hasAccount && (hasIssuer || hasCurrency))
            return {temBAD_PATH, Strand{}};

        if (hasIssuer && isXRP(pe.getIssuerID()))
            return {temBAD_PATH, Strand{}};

        if (hasAccount && isXRP(pe.getAccountID()))
            return {temBAD_PATH, Strand{}};

        if (hasCurrency && hasIssuer &&
            isXRP(pe.getCurrency()) != isXRP(pe.getIssuerID()))
            return {temBAD_PATH, Strand{}};

        if (hasIssuer && (pe.getIssuerID() == noAccount()))
            return {temBAD_PATH, Strand{}};

        if (hasAccount && (pe.getAccountID() == noAccount()))
            return {temBAD_PATH, Strand{}};

        if (hasMPT && (hasCurrency || hasAccount))
            return {temBAD_PATH, Strand{}};

        if (hasMPT && hasIssuer && (pe.getIssuerID() != pe.getMPT().second))
            return {temBAD_PATH, Strand{}};
    }

    Asset curAsset = [&]() -> Asset {
        auto const& asset = sendMaxAsset ? *sendMaxAsset : deliver;
        if (isXRP(asset))
            return xrpIssue();
        if (asset.isMPT())
            return asset;
        return Issue{asset.issue().currency, src};
    }();

    // Currency or MPT
    auto hasAsset = [](STPathElement const pe) {
        return pe.getNodeType() & STPathElement::typeAsset;
    };

    std::vector<STPathElement> normPath;
    // reserve enough for the path, the implied source, destination,
    // sendmax and deliver.
    normPath.reserve(4 + path.size());
    {
        // Implied step: sender of the transaction and either sendmax or deliver
        // asset
        auto const t = [&]() {
            auto const t =
                STPathElement::typeAccount | STPathElement::typeIssuer;
            if (curAsset.isMPT())
                return t | STPathElement::typeMPT;
            return t | STPathElement::typeCurrency;
        }();
        normPath.emplace_back(t, src, curAsset, curAsset.account());

        // If transaction includes sendmax with the issuer, which is not
        // the sender then the issuer is the second implied step, unless
        // the path starts at address, which is the issuer of sendmax
        if (sendMaxAsset && sendMaxAsset->account() != src &&
            (path.empty() || !path[0].isAccount() ||
             path[0].getAccountID() != sendMaxAsset->account()))
        {
            normPath.emplace_back(
                sendMaxAsset->account(), std::nullopt, std::nullopt);
        }

        for (auto const& i : path)
            normPath.push_back(i);

        {
            // Note that for offer crossing (only) we do use an offer book
            // even if all that is changing is the Issue/MPTIssue.account.
            STPathElement const& lastAsset =
                *std::find_if(normPath.rbegin(), normPath.rend(), hasAsset);
            if (lastAsset.getPathAsset() != deliver ||
                (offerCrossing && lastAsset.getIssuerID() != deliver.account()))
            {
                normPath.emplace_back(std::nullopt, deliver, deliver.account());
            }
        }

        if (!((normPath.back().isAccount() &&
               normPath.back().getAccountID() == deliver.account()) ||
              (dst == deliver.account())))
        {
            normPath.emplace_back(
                deliver.account(),
                std::nullopt,
                std::nullopt,
                STPathElement::PathAssetTag{});
        }

        if (!normPath.back().isAccount() ||
            normPath.back().getAccountID() != dst)
        {
            normPath.emplace_back(
                dst, std::nullopt, std::nullopt, STPathElement::PathAssetTag{});
        }
    }

    if (normPath.size() < 2)
        return {temBAD_PATH, Strand{}};

    auto const strandSrc = normPath.front().getAccountID();
    auto const strandDst = normPath.back().getAccountID();
    bool const isDefaultPath = path.empty();

    Strand result;
    result.reserve(2 * normPath.size());

    /* A strand may not include the same account node more than once
       in the same currency. In a direct step, an account will show up
       at most twice: once as a src and once as a dst (hence the two element
       array). The strandSrc and strandDst will only show up once each.
    */
    std::array<boost::container::flat_set<Asset>, 2> seenDirectAssets;
    // A strand may not include the same offer book more than once
    boost::container::flat_set<Asset> seenBookOuts;
    seenDirectAssets[0].reserve(normPath.size());
    seenDirectAssets[1].reserve(normPath.size());
    seenBookOuts.reserve(normPath.size());
    auto ctx = [&](bool isLast = false) {
        return StrandContext{
            view,
            result,
            strandSrc,
            strandDst,
            deliver,
            limitQuality,
            isLast,
            ownerPaysTransferFee,
            offerCrossing,
            isDefaultPath,
            seenDirectAssets,
            seenBookOuts,
            ammContext,
            j};
    };

    for (std::size_t i = 0; i < normPath.size() - 1; ++i)
    {
        /* Iterate through the path elements considering them in pairs.
           The first element of the pair is `cur` and the second element is
           `next`. When an offer is one of the pairs, the step created will be
           for `next`. This means when `cur` is an offer and `next` is an
           account then no step is created, as a step has already been created
           for that offer.
        */
        std::optional<STPathElement> impliedPE;
        auto cur = &normPath[i];
        auto const next = &normPath[i + 1];

        // Switch over from MPT to Currency.
        if (curAsset.isMPT() && cur->hasCurrency())
            curAsset = Issue{};

        // Can only update the account for Issue since MPTIssue's account
        // is immutable as it is part of MPTID
        if (curAsset.isIssue())
        {
            if (cur->isAccount())
                curAsset.issue().account = cur->getAccountID();
            else if (cur->hasIssuer())
                curAsset.issue().account = cur->getIssuerID();
        }

        if (cur->hasCurrency())
        {
            curAsset = Issue{cur->getCurrency(), curAsset.account()};
            if (isXRP(curAsset.issue().currency))
                curAsset.issue().account = xrpAccount();
        }
        else if (cur->hasMPT())
            curAsset = cur->getPathAsset().mpt();

        auto getImpliedStep = [&](AccountID const& src_,
                                  AccountID const& dst_,
                                  Asset const& asset_) {
            if (asset_.isMPT())
                return make_MPTEndpointStep(
                    ctx(), src_, dst_, asset_.mptIssue().mpt());
            return make_DirectStepI(ctx(), src_, dst_, asset_.issue().currency);
        };

        if (cur->isAccount() && next->isAccount())
        {
            // TODO MPT This code never executes if curAsset is Currency
            // since curAsset's account is set to cur's account above.
            // It should not execute for MPT either because MPT rippling
            // is invalid. Should this block be removed?
            if (!isXRP(curAsset) && curAsset.account() != cur->getAccountID() &&
                curAsset.account() != next->getAccountID())
            {
                if (curAsset.isMPT())
                    Throw<FlowException>(
                        tefEXCEPTION, "MPT is invalid with rippling");
                JLOG(j.trace()) << "Inserting implied account";
                auto msr = getImpliedStep(
                    cur->getAccountID(), curAsset.account(), curAsset);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back(std::move(msr.second));
                impliedPE.emplace(
                    STPathElement::typeAccount,
                    curAsset.account(),
                    xrpCurrency(),
                    xrpAccount());
                cur = &*impliedPE;
            }
        }
        else if (cur->isAccount() && next->isOffer())
        {
            // TODO MPT Same as above.
            if (curAsset.account() != cur->getAccountID())
            {
                if (curAsset.isMPT())
                    Throw<FlowException>(
                        tefEXCEPTION, "MPT is invalid with rippling");
                JLOG(j.trace()) << "Inserting implied account before offer";
                auto msr = getImpliedStep(
                    cur->getAccountID(), curAsset.account(), curAsset);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back(std::move(msr.second));
                impliedPE.emplace(
                    STPathElement::typeAccount,
                    curAsset.account(),
                    xrpCurrency(),
                    xrpAccount());
                cur = &*impliedPE;
            }
        }
        else if (cur->isOffer() && next->isAccount())
        {
            if (curAsset.account() != next->getAccountID() &&
                !isXRP(next->getAccountID()))
            {
                if (isXRP(curAsset))
                {
                    if (i != normPath.size() - 2)
                        return {temBAD_PATH, Strand{}};
                    else
                    {
                        // Last step. insert xrp endpoint step
                        auto msr =
                            make_XRPEndpointStep(ctx(), next->getAccountID());
                        if (msr.first != tesSUCCESS)
                            return {msr.first, Strand{}};
                        result.push_back(std::move(msr.second));
                    }
                }
                else
                {
                    JLOG(j.trace()) << "Inserting implied account after offer";
                    auto msr = getImpliedStep(
                        curAsset.account(), next->getAccountID(), curAsset);
                    if (msr.first != tesSUCCESS)
                        return {msr.first, Strand{}};
                    result.push_back(std::move(msr.second));
                }
            }
            continue;
        }

        if (!next->isOffer() && next->hasAsset() &&
            next->getPathAsset() != curAsset)
        {
            // Should never happen
            assert(0);
            return {temBAD_PATH, Strand{}};
        }

        auto s = toStep(
            ctx(/*isLast*/ i == normPath.size() - 2), cur, next, curAsset);
        if (s.first == tesSUCCESS)
            result.emplace_back(std::move(s.second));
        else
        {
            JLOG(j.debug()) << "toStep failed: " << s.first;
            return {s.first, Strand{}};
        }
    }

    auto checkStrand = [&]() -> bool {
        auto stepAccts = [](Step const& s) -> std::pair<AccountID, AccountID> {
            if (auto r = s.directStepAccts())
                return *r;
            if (auto const r = s.bookStepBook())
                return std::make_pair(r->in.account(), r->out.account());
            Throw<FlowException>(
                tefEXCEPTION, "Step should be either a direct or book step");
            return std::make_pair(xrpAccount(), xrpAccount());
        };

        auto curAcc = src;
        auto curAsset = [&]() -> Asset {
            auto const& asset = sendMaxAsset ? *sendMaxAsset : deliver;
            if (isXRP(asset))
                return xrpIssue();
            if (asset.isMPT())
                return asset;
            return Issue{asset.issue().currency, src};
        }();

        for (auto const& s : result)
        {
            auto const accts = stepAccts(*s);
            if (accts.first != curAcc)
                return false;

            if (auto const b = s->bookStepBook())
            {
                if (curAsset != b->in)
                    return false;
                curAsset = b->out;
            }
            else if (curAsset.isIssue())
            {
                curAsset.issue().account = accts.second;
            }

            curAcc = accts.second;
        }
        if (curAcc != dst)
            return false;
        if (curAsset.isIssue() != deliver.isIssue() ||
            (curAsset.isIssue() &&
             curAsset.issue().currency != deliver.issue().currency) ||
            (curAsset.isMPT() && curAsset.mptIssue() != deliver.mptIssue()))
        {
            std::cout << to_string(curAsset) << std::endl;
            std::cout << to_string(deliver) << std::endl;
            return false;
        }
        if (curAsset.account() != deliver.account() &&
            curAsset.account() != dst)
            return false;
        return true;
    };

    if (!checkStrand())
    {
        JLOG(j.warn()) << "Flow check strand failed";
        assert(0);
        return {temBAD_PATH, Strand{}};
    }

    return {tesSUCCESS, std::move(result)};
}

std::pair<TER, std::vector<Strand>>
toStrands(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Asset const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Asset> const& sendMax,
    STPathSet const& paths,
    bool addDefaultPath,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    beast::Journal j)
{
    std::vector<Strand> result;
    result.reserve(1 + paths.size());
    // Insert the strand into result if it is not already part of the vector
    auto insert = [&](Strand s) {
        bool const hasStrand =
            std::find(result.begin(), result.end(), s) != result.end();

        if (!hasStrand)
            result.emplace_back(std::move(s));
    };

    if (addDefaultPath)
    {
        auto sp = toStrand(
            view,
            src,
            dst,
            deliver,
            limitQuality,
            sendMax,
            STPath(),
            ownerPaysTransferFee,
            offerCrossing,
            ammContext,
            j);
        auto const ter = sp.first;
        auto& strand = sp.second;

        if (ter != tesSUCCESS)
        {
            JLOG(j.trace()) << "failed to add default path";
            if (isTemMalformed(ter) || paths.empty())
            {
                return {ter, std::vector<Strand>{}};
            }
        }
        else if (strand.empty())
        {
            JLOG(j.trace()) << "toStrand failed";
            Throw<FlowException>(
                tefEXCEPTION, "toStrand returned tes & empty strand");
        }
        else
        {
            insert(std::move(strand));
        }
    }
    else if (paths.empty())
    {
        JLOG(j.debug()) << "Flow: Invalid transaction: No paths and direct "
                           "ripple not allowed.";
        return {temRIPPLE_EMPTY, std::vector<Strand>{}};
    }

    TER lastFailTer = tesSUCCESS;
    for (auto const& p : paths)
    {
        auto sp = toStrand(
            view,
            src,
            dst,
            deliver,
            limitQuality,
            sendMax,
            p,
            ownerPaysTransferFee,
            offerCrossing,
            ammContext,
            j);
        auto ter = sp.first;
        auto& strand = sp.second;

        if (ter != tesSUCCESS)
        {
            lastFailTer = ter;
            JLOG(j.trace()) << "failed to add path: ter: " << ter
                            << "path: " << p.getJson(JsonOptions::none);
            if (isTemMalformed(ter))
                return {ter, std::vector<Strand>{}};
        }
        else if (strand.empty())
        {
            JLOG(j.trace()) << "toStrand failed";
            Throw<FlowException>(
                tefEXCEPTION, "toStrand returned tes & empty strand");
        }
        else
        {
            insert(std::move(strand));
        }
    }

    if (result.empty())
        return {lastFailTer, std::move(result)};

    return {tesSUCCESS, std::move(result)};
}

StrandContext::StrandContext(
    ReadView const& view_,
    std::vector<std::unique_ptr<Step>> const& strand_,
    // A strand may not include an inner node that
    // replicates the source or destination.
    AccountID const& strandSrc_,
    AccountID const& strandDst_,
    Asset const& strandDeliver_,
    std::optional<Quality> const& limitQuality_,
    bool isLast_,
    bool ownerPaysTransferFee_,
    OfferCrossing offerCrossing_,
    bool isDefaultPath_,
    std::array<boost::container::flat_set<Asset>, 2>& seenDirectAssets_,
    boost::container::flat_set<Asset>& seenBookOuts_,
    AMMContext& ammContext_,
    beast::Journal j_)
    : view(view_)
    , strandSrc(strandSrc_)
    , strandDst(strandDst_)
    , strandDeliver(strandDeliver_)
    , limitQuality(limitQuality_)
    , isFirst(strand_.empty())
    , isLast(isLast_)
    , ownerPaysTransferFee(ownerPaysTransferFee_)
    , offerCrossing(offerCrossing_)
    , isDefaultPath(isDefaultPath_)
    , strandSize(strand_.size())
    , prevStep(!strand_.empty() ? strand_.back().get() : nullptr)
    , seenDirectAssets(seenDirectAssets_)
    , seenBookOuts(seenBookOuts_)
    , ammContext(ammContext_)
    , j(j_)
{
}

template <class InAmt, class OutAmt>
bool
isDirectXrpToXrp(Strand const& strand)
{
    return false;
}

template <>
bool
isDirectXrpToXrp<XRPAmount, XRPAmount>(Strand const& strand)
{
    return (strand.size() == 2);
}

template bool
isDirectXrpToXrp<XRPAmount, IOUAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<IOUAmount, XRPAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<IOUAmount, IOUAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<MPTAmount, MPTAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<IOUAmount, MPTAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<MPTAmount, IOUAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<XRPAmount, MPTAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<MPTAmount, XRPAmount>(Strand const& strand);

}  // namespace ripple
