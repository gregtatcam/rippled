//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#ifndef RIPPLE_TX_AMMOFFERS_H_INCLUDED
#define RIPPLE_TX_AMMOFFERS_H_INCLUDED

#include <ripple/app/misc/AMM.h>
#include <ripple/app/tx/impl/AMMOffer.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Book.h>

#include <vector>

namespace ripple {

class AMMOfferGen;

/** AMMOffers is a container of all AMM offers for the given Book.
 * There might be multiple AMM instances for the same Book with
 * different weights.
 */
template <typename TIn, typename TOut>
class AMMOffers
{
private:
    // All AMM offers for the given book
    std::vector<AMMOffer<TIn, TOut>> ammOffers_;
    // Consumed offers temporarily moved to consumed_
    // in order to avoid the offer being selected again,
    // for instance if the there are two offers with
    // the same quality.
    std::vector<AMMOffer<TIn, TOut>> consumed_;
    beast::Journal const j_;

public:
    AMMOffers(
        ReadView const& view,
        Book const& book,
        AMMOfferGen& ammOfferGen,
        beast::Journal const j);
    ~AMMOffers() = default;

    AMMOffers(AMMOffers const&) = delete;
    AMMOffers&
    operator=(AMMOffers const&) = delete;

    /*---------------------------------------*/
    // These methods call respective methods
    // on all AMMOffer instances
    void
    updateTakerGets(TOut const& remaining);
    void
    updateTakerPays(TIn const& remaining);
    void
    changeQuality(Quality const& quality);
    void
    updateReserves(ReadView const& view);
    void
    consume(TOffer<TIn, TOut> const& offer);
    /*---------------------------------------*/

    /** Move the offers from consumed_ to ammOffers_ */
    void
    reset();

    std::size_t
    size() const
    {
        return ammOffers_.size();
    }

    /** Return best quality AMM offer if available */
    std::optional<std::reference_wrapper<AMMOffer<TIn, TOut> const>>
    tip() const;
    std::optional<std::reference_wrapper<AMMOffer<TIn, TOut>>>
    tip();
};

template <typename TIn, typename TOut>
AMMOffers<TIn, TOut>::AMMOffers(
    ReadView const& view,
    Book const& book,
    AMMOfferGen& ammOfferGen,
    beast::Journal const j)
    : j_(j)
{
    // TODO. Must add an AMM group to contain hashes for different
    // weights
    auto const ammHash = calcAMMHash(50, book.in, book.out);
    if (auto const sle = view.read(keylet::amm(ammHash)); sle)
    {
        auto const ammAccountID = sle->getAccountID(sfAMMAccount);
        auto const [assetIn, assetOut, _] = getAMMBalances(
            view, ammAccountID, std::nullopt, book.in, book.out, j);
        (void)_;
        if (assetIn == beast::zero || assetOut == beast::zero)
            JLOG(j_.fatal()) << "AMMOffers: failed to get AMM " << ammAccountID;
        else
            ammOffers_.emplace_back(std::move(AMMOffer<TIn, TOut>{
                sle, ammAccountID, assetIn, assetOut, ammOfferGen, j}));
    }
}

template <typename TIn, typename TOut>
std::optional<std::reference_wrapper<AMMOffer<TIn, TOut> const>>
AMMOffers<TIn, TOut>::tip() const
{
    std::optional<Quality> bestQuality = std::nullopt;
    std::optional<std::reference_wrapper<AMMOffer<TIn, TOut> const>>
        bestOffer{};
    for (auto& offer : ammOffers_)
    {
        // Should we check for the creation date if the quality is the same?
        if (!bestQuality || offer.quality() > bestQuality)
        {
            bestQuality = offer.quality();
            bestOffer = std::ref(offer);
        }
    }
    return bestOffer;
}

template <typename TIn, typename TOut>
std::optional<std::reference_wrapper<AMMOffer<TIn, TOut>>>
AMMOffers<TIn, TOut>::tip()
{
    if (auto offer = std::as_const(*this).tip())
        return std::ref(const_cast<AMMOffer<TIn, TOut>&>(offer->get()));
    return std::nullopt;
}

template <typename TIn, typename TOut>
void
AMMOffers<TIn, TOut>::updateTakerGets(TOut const& remainingOut)
{
    for (auto& offer : ammOffers_)
        offer.updateTakerGets(remainingOut);
}

template <typename TIn, typename TOut>
void
AMMOffers<TIn, TOut>::updateTakerPays(TIn const& remainingIn)
{
    for (auto& offer : ammOffers_)
        offer.updateTakerPays(remainingIn);
}

template <typename TIn, typename TOut>
void
AMMOffers<TIn, TOut>::changeQuality(Quality const& quality)
{
    for (auto& offer : ammOffers_)
        offer.changeQuality(quality);
}

template <typename TIn, typename TOut>
void
AMMOffers<TIn, TOut>::updateReserves(ReadView const& view)
{
    for (auto& offer : ammOffers_)
        offer.updateReserves(view);
}

template <typename TIn, typename TOut>
void
AMMOffers<TIn, TOut>::consume(TOffer<TIn, TOut> const& offer)
{
    bool consumed = false;
    for (auto it = ammOffers_.begin(); it != ammOffers_.end(); ++it)
    {
        if (&offer == &(*it))
        {
            consumed_.insert(
                consumed_.end(),
                std::make_move_iterator(it),
                std::make_move_iterator(it + 1));
            ammOffers_.erase(it);
            consumed = true;
            break;
        }
    }
    if (!consumed)
    {
        JLOG(j_.error()) << "AMMOffers: failed to consume " << offer.id();
    }
}

template <typename TIn, typename TOut>
void
AMMOffers<TIn, TOut>::reset()
{
    ammOffers_.insert(
        ammOffers_.end(),
        std::make_move_iterator(consumed_.begin()),
        std::make_move_iterator(consumed_.end()));
    consumed_.clear();
}

}  // namespace ripple

#endif  // RIPPLE_TX_AMMOFFERS_H_INCLUDED
