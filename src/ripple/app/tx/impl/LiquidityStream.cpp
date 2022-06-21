//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2022 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/LiquidityStream.h>

namespace ripple {

// should make this common for OfferStream and FlowLiquidityStream
static STAmount
accountFundsHelper(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    Issue const&,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    return accountFunds(view, id, saDefault, freezeHandling, j);
}

static IOUAmount
accountFundsHelper(
    ReadView const& view,
    AccountID const& id,
    IOUAmount const& amtDefault,
    Issue const& issue,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    if (issue.account == id)
        // self funded
        return amtDefault;

    return toAmount<IOUAmount>(accountHolds(
        view, id, issue.currency, issue.account, freezeHandling, j));
}

static XRPAmount
accountFundsHelper(
    ReadView const& view,
    AccountID const& id,
    XRPAmount const& amtDefault,
    Issue const& issue,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    return toAmount<XRPAmount>(accountHolds(
        view, id, issue.currency, issue.account, freezeHandling, j));
}

template <typename TIn, typename TOut>
FlowLiquidityStream<TIn, TOut>::FlowLiquidityStream(
    ApplyView& view,
    ApplyView& cancelView,
    Book const& book,
    NetClock::time_point when,
    StepCounter& counter,
    bool useAMMLiquidity,
    TIn const* remainingIn,
    TOut const* remainingOut,
    beast::Journal journal)
    : offerStream_(view, cancelView, book, when, counter, journal)
    , ammOffer_(
          useAMMLiquidity
              ? AMMOffer<TIn, TOut>::makeOffer(view, book.in, book.out, journal)
              : std::nullopt)
    , view_(view)
    , j_(journal)
    , cachedOBOffer_(false)
    , useAMMOffer_(false)
    , remainingIn_(remainingIn)
    , remainingOut_(remainingOut)
{
}

template <typename TIn, typename TOut>
bool
FlowLiquidityStream<TIn, TOut>::step()
{
    if (!cachedOBOffer_)
        cachedOBOffer_ = offerStream_.step();

    // AMM offer can only be used once at
    // any step iteration.
    if (useAMMOffer_)
        ammOffer_.reset();

    useAMMOffer_ = false;
    if (ammOffer_.has_value())
    {
        if (cachedOBOffer_)
        {
            if (ammOffer_->changeQuality(offerStream_.tip().quality()))
            {
                if (remainingOut_ && ammOffer_->amount().out > *remainingOut_)
                    ammOffer_->updateTakerGets(*remainingOut_);
                else if (remainingIn_ && ammOffer_->amount().in > *remainingIn_)
                    ammOffer_->updateTakerPays(*remainingIn_);
                useAMMOffer_ = true;
            }
        }
        else
        {
            if (remainingOut_)
                ammOffer_->updateTakerGets(*remainingOut_);
            else if (remainingIn_)
                ammOffer_->updateTakerPays(*remainingIn_);
            useAMMOffer_ = true;
        }
    }

    if (useAMMOffer_)
    {
        auto const amount(ammOffer_->amount());
        ownerFunds_ = accountFundsHelper(
            view_,
            ammOffer_->owner(),
            amount.out,
            ammOffer_->issueOut(),
            fhZERO_IF_FROZEN,
            j_);
    }

    auto const res = useAMMOffer_ || cachedOBOffer_;
    if (!useAMMOffer_ && cachedOBOffer_)
        cachedOBOffer_ = false;

    return res;
}

template <typename TIn, typename TOut>
TOffer<TIn, TOut>&
FlowLiquidityStream<TIn, TOut>::tip() const
{
    if (useAMMOffer_)
    {
        if (!ammOffer_.has_value())
            Throw<std::logic_error>("Offer is not available.");
        return const_cast<FlowLiquidityStream*>(this)->ammOffer_.value();
    }
    return offerStream_.tip();
}

template class FlowLiquidityStream<STAmount, STAmount>;
template class FlowLiquidityStream<IOUAmount, IOUAmount>;
template class FlowLiquidityStream<XRPAmount, IOUAmount>;
template class FlowLiquidityStream<IOUAmount, XRPAmount>;

}  // namespace ripple
