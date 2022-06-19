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

#ifndef RIPPLE_APP_LIQUIDITYSTREAM_H_INCLUDED
#define RIPPLE_APP_LIQUIDITYSTREAM_H_INCLUDED

#include <ripple/app/tx/impl/AMMOffer.h>
#include <ripple/app/tx/impl/OfferStream.h>

namespace ripple {

/** Provide liquidity stream. Combines order book offers and AMM
 * into one stream.
 */
template <typename TIn, typename TOut>
class FlowLiquidityStream
{
private:
    FlowOfferStream<TIn, TOut> offerStream_;
    using AMMOffer_t =
        std::optional<std::reference_wrapper<AMMOffer<TIn, TOut>>>;
    using StepCounter = typename TOfferStreamBase<TIn, TOut>::StepCounter;
    AMMOffer_t ammOffer_;
    ApplyView& view_;
    beast::Journal const j_;
    bool cachedOBOffer_;
    bool useAMMOffer_;
    TIn const* remainingIn_;
    TOut const* remainingOut_;
    std::optional<TOut> ownerFunds_;  // do we need this for AMM?

public:
    FlowLiquidityStream(
        ApplyView& view,
        ApplyView& cancelView,
        Book const& book,
        NetClock::time_point when,
        StepCounter& counter,
        AMMOffer_t const& ammOffer,
        TIn const* remainingIn,
        TOut const* remainingOut,
        beast::Journal journal);

    ~FlowLiquidityStream()
    {
    }

    /** Advance to the next valid order book or AMM offer.
        This automatically removes:
          - Offers with missing ledger entries
          - Offers found unfunded
          - expired offers
       @return `true` if there is a valid offer.
     */
    bool
    step();

    /** Returns the offer at the tip of the order book.
      Offers are always presented in decreasing quality.
      Only valid if step() returned `true`.
  */
    TOffer<TIn, TOut>&
    tip() const;

    FlowOfferStream<TIn, TOut>&
    offerStream()
    {
        return offerStream_;
    }

    TOut
    ownerFunds() const
    {
        if (useAMMOffer_)
            return *ownerFunds_;
        else
            return offerStream_.ownerFunds();
    }
};

}  // namespace ripple

#endif  // RIPPLE_APP_LIQUIDITYSTREAM_H_INCLUDED
