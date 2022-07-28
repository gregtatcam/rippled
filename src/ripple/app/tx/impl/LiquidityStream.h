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

#ifndef RIPPLE_APP_TX_LIQUIDITYSTREAM_H_INCLUDED
#define RIPPLE_APP_TX_LIQUIDITYSTREAM_H_INCLUDED

#include <ripple/app/tx/impl/AMMOfferMaker.h>
#include <ripple/app/tx/impl/OfferStream.h>

namespace ripple {

/** Provide liquidity stream. Combines order book and AMM
 * offers into one stream. Mirrors and is used instead of
 * FlowOfferStream class in BookStep. At each step()
 * the offer is selected and provided to BookStep based
 * on the best quality offer. When AMM offer has a better
 * quality, useAMMOffer_ is set, order book offer is
 * cached in offerStream_, and the cached state is set in
 * cachedOBOffer_. Cached order book offer is used
 * at the next step() iteration. Only one AMM offer can
 * be used at each step() iteration. The offer is reset
 * once it is used.
 */
template <typename TIn, typename TOut>
class FlowLiquidityStream
{
private:
    FlowOfferStream<TIn, TOut> offerStream_;
    using StepCounter = typename TOfferStreamBase<TIn, TOut>::StepCounter;
    std::optional<AMMOffer<TIn, TOut>> ammOffer_;
    std::optional<TOut> ammOwnerFunds_;
    ApplyView& view_;
    beast::Journal const j_;
    bool cachedOBOffer_;
    bool useAMMOffer_;

public:
    FlowLiquidityStream(
        ApplyView& view,
        ApplyView& cancelView,
        Book const& book,
        NetClock::time_point when,
        StepCounter& counter,
        std::optional<AMMOfferMaker<TIn, TOut>> const& ammMaker,
        TIn const* remainingIn,
        TOut const* remainingOut,
        beast::Journal journal);

    ~FlowLiquidityStream() = default;

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

    TOut
    ownerFunds() const
    {
        if (ammOwnerFunds_.has_value())
            return *ammOwnerFunds_;
        return offerStream_.ownerFunds();
    }

    void
    permRmOffer(uint256 key)
    {
        // TODO, should handle auth case for AMM
        if (key != uint256{0})
            offerStream_.permRmOffer(key);
    }

    boost::container::flat_set<uint256> const&
    permToRemove() const
    {
        return offerStream_.permToRemove();
    }
};

}  // namespace ripple

#endif  // RIPPLE_APP_TX_LIQUIDITYSTREAM_H_INCLUDED
