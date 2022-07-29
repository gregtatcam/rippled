//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED
#define RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED

#include <ripple/app/misc/AMM.h>
#include <ripple/app/tx/impl/AMMOffer.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STLedgerEntry.h>
#include "ripple/app/paths/AMMOfferCounter.h"

namespace ripple {

namespace {

class FibSeqHelper
{
private:
    mutable Amounts curSeq_{};
    mutable Number x_{0};
    mutable Number y_{0};

public:
    FibSeqHelper() = default;
    ~FibSeqHelper() = default;
    Amounts const&
    firstSeq(Amounts const& balances, std::uint16_t tfee) const
    {
        auto const SP = balances.out / (balances.in * feeMult(tfee));
        curSeq_.in = toSTAmount(
            balances.in.issue(), (Number(5) / 10000) * balances.in / 2);
        curSeq_.out = toSTAmount(balances.out.issue(), SP * curSeq_.in);
        y_ = curSeq_.out;
        return curSeq_;
    }
    Amounts const&
    curSeq() const
    {
        return curSeq_;
    }
    Amounts const&
    nextSeq(Amounts const& balances, std::uint16_t tfee) const
    {
        auto const total = x_ + y_;
        curSeq_.out = toSTAmount(balances.out.issue(), total);
        curSeq_.in = toSTAmount(
            balances.in.issue(),
            (balances.in * balances.out / (balances.out - curSeq_.out) -
             balances.in) /
                feeMult(tfee));
        x_ = y_;
        y_ = total;
        return curSeq_;
    }
};

}  // namespace

template <typename TIn, typename TOut>
class AMMOfferMaker
{
private:
    AMMOfferCounter const& offerCounter_;
    AccountID ammAccountID_;
    std::uint32_t tradingFee_;
    mutable Amounts balances_;
    mutable std::optional<FibSeqHelper> fibSeqHelper_;
    mutable bool dirty_;
    beast::Journal const j_;

public:
    AMMOfferMaker(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Issue const& in,
        Issue const& out,
        AMMOfferCounter const& offerCounter,
        beast::Journal j);
    ~AMMOfferMaker() = default;
    AMMOfferMaker(AMMOfferMaker const&) = delete;

    std::optional<AMMOffer<TIn, TOut>>
    makeOffer(
        ReadView const& view,
        std::optional<Quality> const& clobQuality = std::nullopt,
        TIn const* remIn = nullptr,
        TOut const* remOut = nullptr,
        std::int32_t trIn = 0) const;

    Amounts
    balances() const
    {
        return balances_;
    }

private:
    Amounts
    fetchBalances(ReadView const& view) const;

    STAmount
    ammAccountHolds(
        ReadView const& view,
        AccountID const& ammAccountID,
        Issue const& issue) const;
};

template <typename TIn, typename TOut>
AMMOfferMaker<TIn, TOut>::AMMOfferMaker(
    ReadView const& view,
    AccountID const& ammAccountID,
    std::uint32_t tradingFee,
    Issue const& in,
    Issue const& out,
    AMMOfferCounter const& offerCounter,
    beast::Journal j)
    : offerCounter_(offerCounter)
    , ammAccountID_(ammAccountID)
    , tradingFee_(tradingFee)
    , balances_{STAmount{in}, STAmount{out}}
    , fibSeqHelper_{std::nullopt}
    , dirty_(true)
    , j_(j)
{
    balances_ = fetchBalances(view);
}

template <typename TIn, typename TOut>
STAmount
AMMOfferMaker<TIn, TOut>::ammAccountHolds(
    const ReadView& view,
    AccountID const& ammAccountID,
    const Issue& issue) const
{
    if (isXRP(issue))
    {
        if (auto const sle = view.read(keylet::account(ammAccountID)))
            return sle->getFieldAmount(sfBalance);
    }
    else if (auto const sle = view.read(
                 keylet::line(ammAccountID, issue.account, issue.currency));
             !isFrozen(view, ammAccountID, issue.currency, issue.account))
    {
        auto amount = sle->getFieldAmount(sfBalance);
        if (amount.negative())
            amount.negate();
        amount.setIssuer(issue.account);
        return amount;
    }

    return STAmount{issue};
}

template <typename TIn, typename TOut>
Amounts
AMMOfferMaker<TIn, TOut>::fetchBalances(const ReadView& view) const
{
    if (dirty_)
    {
        auto const assetIn =
            ammAccountHolds(view, ammAccountID_, balances_.in.issue());
        auto const assetOut =
            ammAccountHolds(view, ammAccountID_, balances_.out.issue());
        // This should not happen since AMMOfferMaker is created only
        // if AMM exists for the given token pair.
        if (assetIn <= beast::zero || assetOut <= beast::zero)
            Throw<std::runtime_error>("AMMOfferMaker: unexpected 0 balances");

        return Amounts(assetIn, assetOut);
    }

    dirty_ = false;

    return balances_;
}

template <typename TIn, typename TOut>
std::optional<AMMOffer<TIn, TOut>>
AMMOfferMaker<TIn, TOut>::makeOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality,
    TIn const* remIn,
    TOut const* remOut,
    std::int32_t trIn) const
{
    if (offerCounter_.maxItersReached())
        return std::nullopt;

    auto const balances = fetchBalances(view);

    std::cout << "makeOffer: balances " << balances_.in << " " << balances_.out
              << " new balances " << balances.in << " " << balances.out
              << std::endl;

    if (clobQuality.has_value())
        std::cout << "makeOffer: rate " << clobQuality->rate() << std::endl;

    auto const offerAmounts = [&]() -> std::optional<Amounts> {
        if (offerCounter_.multiPath())
        {
            auto const offerAmounts = [&]() {
                // first sequence
                if (!fibSeqHelper_.has_value())
                {
                    fibSeqHelper_.emplace();
                    return fibSeqHelper_->firstSeq(balances, tradingFee_);
                }
                // if balances have not changed (one side is enough to check),
                // then the offer size stays the same
                else if (balances.out == balances_.out)
                {
                    return fibSeqHelper_->curSeq();
                }
                // advance forward to next sequence
                else
                {
                    return fibSeqHelper_->nextSeq(balances, tradingFee_);
                }
            }();
            auto const quality = Quality{offerAmounts};
            if (clobQuality && quality < clobQuality.value())
                return std::nullopt;
            // Change offer size proportionally to the quality
            if (remOut && Number(offerAmounts.out) > *remOut)
                return quality.ceil_out(
                    offerAmounts,
                    toSTAmount(*remOut, offerAmounts.out.issue()));
            if (remIn && Number(offerAmounts.in) > *remIn)
                return quality.ceil_in(
                    offerAmounts, toSTAmount(*remIn, offerAmounts.in.issue()));
            return offerAmounts;
        }
        else
        {
            auto const quality = Quality{balances};
            auto const offerAmounts = [&]() -> std::optional<Amounts> {
                if (clobQuality)
                {
                    if (quality < clobQuality)
                        return std::nullopt;
                    return changeSpotPriceQuality(
                        balances, *clobQuality, tradingFee_);
                }
                return balances;
            }();
            if (!offerAmounts.has_value())
                return std::nullopt;
            // Change offer size based on swap in/out formulas
            if (remOut && !remIn && Number(offerAmounts->out) > *remOut)
                return Amounts{
                    swapAssetOut(*offerAmounts, *remOut, tradingFee_),
                    toSTAmount(*remOut, offerAmounts->out.issue())};
            // remIn can also have remOut, where remOut is step's cache_.out
            // we want to make sure we don't produce more out in fwd
            if (remIn && Number(offerAmounts->in) > *remIn)
            {
                auto in = toSTAmount(*remIn, offerAmounts->in.issue());
                auto out = swapAssetIn(*offerAmounts, *remIn, tradingFee_);
                auto const saRemOut =
                    toSTAmount(*remOut, offerAmounts->out.issue());
                if (out > saRemOut)
                {
                    out = saRemOut;
                    in = swapAssetOut(*offerAmounts, out, tradingFee_);
                }
                return Amounts{in, out};
            }
            return offerAmounts;
        }
    }();

    balances_ = balances;

    if (offerAmounts.has_value() && offerAmounts->in > beast::zero &&
        offerAmounts->out > beast::zero)
    {
        std::cout << "makeOffer: creating " << offerAmounts->in << " "
                  << offerAmounts->out << ", rate "
                  << Quality(*offerAmounts).rate() << std::endl;
        return AMMOffer<TIn, TOut>(
            *offerAmounts,
            ammAccountID_,
            [&]() { dirty_ = true; },
            [&]() { offerCounter_.incrementCounter(); },
            j_);
    }
    std::cout << "makeOffer: not selected\n";
    return std::nullopt;
}

}  // namespace ripple

#endif  // RIPPLE_APP_TX_AMMOFFERMAKER_H_INCLUDED
