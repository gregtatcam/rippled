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

template <typename TIn, typename TOut>
FlowLiquidityStream<TIn, TOut>::FlowLiquidityStream(
    ApplyView& view,
    ApplyView& cancelView,
    Book const& book,
    NetClock::time_point when,
    StepCounter& counter,
    std::optional<AMMOfferMaker<TIn, TOut>> const& ammMaker,
    TIn const* remainingIn,
    TOut const* remainingOut,
    beast::Journal journal)
    : offerStream_(view, cancelView, book, when, counter, journal)
    , view_(view)
    , j_(journal)
    , cachedOBOffer_(offerStream_.step())
    , useAMMOffer_(false)
{
    std::cout << "FlowLiquidityStream: " << cachedOBOffer_ << std::endl;
    // AMMOffer can be used only once at any payment engine iteration,
    // and it can only be used in front of a CLOB offer.
    if (ammMaker)
    {
        if (cachedOBOffer_)
        {
            auto const offer = offerStream_.tip();
            std::cout << "FlowLiquidityStream: " << toStr(offer.amount().in)
                      << " " << toStr(offer.amount().out) << " "
                      << offer.quality().rate() << std::endl;
        }
        ammOffer_ = ammMaker->makeOffer(
            view,
            cachedOBOffer_
                ? std::optional<Quality>(offerStream_.tip().quality())
                : std::nullopt,
            remainingIn,
            remainingOut);
        useAMMOffer_ = ammOffer_.has_value();
        ammOwnerFunds_ = get<TOut>(ammMaker->balances().out);
    }
}

template <typename TIn, typename TOut>
bool
FlowLiquidityStream<TIn, TOut>::step()
{
    if (useAMMOffer_)
    {
        useAMMOffer_ = false;
        return true;
    }
    else if (cachedOBOffer_)
    {
        cachedOBOffer_ = false;
        return true;
    }

    ammOffer_.reset();
    ammOwnerFunds_.reset();

    return offerStream_.step();
}

template <typename TIn, typename TOut>
TOffer<TIn, TOut>&
FlowLiquidityStream<TIn, TOut>::tip() const
{
    if (ammOffer_.has_value())
        return const_cast<FlowLiquidityStream*>(this)->ammOffer_.value();
    return offerStream_.tip();
}

template class FlowLiquidityStream<STAmount, STAmount>;
template class FlowLiquidityStream<IOUAmount, IOUAmount>;
template class FlowLiquidityStream<XRPAmount, IOUAmount>;
template class FlowLiquidityStream<IOUAmount, XRPAmount>;

}  // namespace ripple
