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
#ifndef RIPPLE_TX_AMMOFFER_H_INCLUDED
#define RIPPLE_TX_AMMOFFER_H_INCLUDED

#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/app/tx/impl/Offer.h>

namespace ripple {

/** AMMOffer represents AMM offer created on the fly.
 * It is instantiated by AMMOffers in BookStep if there is an AMM pool
 * available for the Book's issue.
 * The offer's size is initially set to the AMM's pool size, which
 * provides the best hypothetical offer quality given current AMM pool
 * reserves. The size is updated in the offer stream (FlowLiquidityStream).
 * The stream evaluates whether the order book or AMM offer should be
 * included in the stream. This is done based on the best offer quality.
 * The AMM offer size is adjusted to match, if possible, the order book
 * quality. If there is no order book offer available then the AMM offer
 * size is adjusted based on the remainingOut and remainingIn values.
 * Ultimately the offer stream includes the best quality offer.
 * The offer size can only be adjusted by the stream since the AMM
 * offer's quality changes with the offer size and the offers must be
 * ordered by quality and consumed in this order by the BookStep.
 */
template <typename TIn, typename TOut>
class AMMOffer : public TOffer<TIn, TOut>
{
    TAmounts<TIn, TOut> reserves_;  // AMM current pool reserves
    AccountID ammAccountID_;        // AMM root account id
    std::uint8_t weightIn_;         // Asset in weight
    std::uint32_t tfee_;            // AMM trading fee
    beast::Journal j_;

public:
    AMMOffer(
        std::shared_ptr<SLE const> const& amm,
        AccountID const& ammAccountID,
        STAmount const& assetIn,
        STAmount const& assetOut,
        beast::Journal const j);

    /** Updates offer size given takerGets
     */
    void
    updateTakerGets(TOut const& out);

    /** Updates offer size given takerPays
     */
    void
    updateTakerPays(TIn const& in);

    /** Changes offer size given target quality
     * @return true if the size can be changed
     */
    bool
    changeQuality(Quality const& quality);

    /** Update pool reserves and set the offer size
     * to the reserves. This changes the offer quality
     * to the best theoretical quality. This method
     * must be only called when the best quality
     * Strand is applied.
     */
    void
    updateReserves(ReadView const& view);

    AccountID
    account() const
    {
        return ammAccountID_;
    }

    bool
    isAMM() const override
    {
        return true;
    }

    SLE::pointer
    getEntry() const
    {
        return this->m_entry;
    }

    /** Consume the offer. This method validates that the consumed size
     * is the same as the offer size. The offer size can not be changed
     * at this point since it will change the offer quality. See the class
     * comments above.
     */
    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed) override;

private:
    /** Instantiates SLE required for TOffer.
     */
    static SLE::pointer
    makeTOfferSLE(
        AccountID const& ammAccountID,
        STAmount const& assetIn,
        STAmount const& assetOut);

    /** Updates TOffer taketGets/takerPays
     */
    void
    updateOfferSize(TIn const& in, TOut const& out);
};

template <typename TIn, typename TOut>
SLE::pointer
AMMOffer<TIn, TOut>::makeTOfferSLE(
    AccountID const& ammAccountID,
    STAmount const& assetIn,
    STAmount const& assetOut)
{
    std::uint32_t const seq = 1;
    auto const offer_index = keylet::offer(ammAccountID, seq);
    auto offerSLE = std::make_shared<SLE>(offer_index);
    offerSLE->setAccountID(sfAccount, ammAccountID);
    offerSLE->setFieldU32(sfSequence, seq);
    offerSLE->setFieldAmount(sfTakerPays, assetIn);
    offerSLE->setFieldAmount(sfTakerGets, assetOut);
    return offerSLE;
}

template <typename TIn, typename TOut>
AMMOffer<TIn, TOut>::AMMOffer(
    std::shared_ptr<SLE const> const& amm,
    AccountID const& ammAccountID,
    STAmount const& assetIn,
    STAmount const& assetOut,
    beast::Journal const j)
    : TOffer<TIn, TOut>(
          AMMOffer::makeTOfferSLE(ammAccountID, assetIn, assetOut),
          Quality{assetOut, assetIn})
    , reserves_({toAmount<TIn>(assetIn), toAmount<TOut>(assetOut)})
    , ammAccountID_(ammAccountID)
    , weightIn_{orderWeight(
          amm->getFieldU8(sfAssetWeight),
          this->issueIn(),
          this->issueOut())}
    , tfee_{amm->getFieldU32(sfTradingFee)}
    , j_(j)
{
}

template <typename TIn, typename TOut>
void
AMMOffer<TIn, TOut>::updateTakerGets(TOut const& out)
{
    auto const in = swapAssetOut(
        toSTAmount(reserves_.out),
        toSTAmount(reserves_.in),
        toSTAmount(out),
        weightIn_,
        tfee_);
    updateOfferSize(get<TIn>(in), out);
}

template <typename TIn, typename TOut>
void
AMMOffer<TIn, TOut>::updateTakerPays(TIn const& in)
{
    auto const out = swapAssetIn(
        toSTAmount(reserves_.in),
        toSTAmount(reserves_.out),
        toSTAmount(in),
        weightIn_,
        tfee_);
    updateOfferSize(in, get<TOut>(out));
}

template <typename TIn, typename TOut>
bool
AMMOffer<TIn, TOut>::changeQuality(Quality const& quality)
{
    if (auto const res = changeSpotPriceQuality(
            toSTAmount(reserves_.in),
            toSTAmount(reserves_.out),
            quality,
            weightIn_,
            tfee_))
    {
        updateOfferSize(get<TIn>(res->first), get<TOut>(res->second));
        return true;
    }
    return false;
}

template <typename TIn, typename TOut>
void
AMMOffer<TIn, TOut>::updateReserves(ReadView const& view)
{
    auto const amm = view.read(keylet::account(ammAccountID_));
    assert(amm);
    auto const [assetIn, assetOut, _] = getAMMBalances(
        view,
        ammAccountID_,
        std::nullopt,
        this->issueIn(),
        this->issueOut(),
        j_);
    (void)_;
    reserves_ = {get<TIn>(assetIn), get<TOut>(assetOut)};
    // Reset the size to reflect the best theoretical quality
    updateOfferSize(reserves_.in, reserves_.out);
}

template <typename TIn, typename TOut>
void
AMMOffer<TIn, TOut>::updateOfferSize(TIn const& in, TOut const& out)
{
    this->m_amounts.in = in;
    this->m_amounts.out = out;
    this->m_quality = Quality(in, out);
    this->setFieldAmounts();
}

template <typename T>
std::string
toStr(T const& a)
{
    std::stringstream str;
    if constexpr (std::is_same_v<T, IOUAmount> || std::is_same_v<T, IOUAmount>)
        str << to_string(a);
    else
        str << a;
    return str.str();
}

template <typename TIn, typename TOut>
void
AMMOffer<TIn, TOut>::consume(
    ApplyView& view,
    TAmounts<TIn, TOut> const& consumed)
{
    if (consumed != this->m_amounts)
    {
        std::stringstream str;
        str << "AMMOffer invalid consumed size: offer size in/out "
            << toStr(this->m_amounts.in) << " " << toStr(this->m_amounts.out)
            << ", consumed in/out " << toStr(consumed.in) << " "
            << toStr(consumed.out);
        Throw<std::logic_error>(str.str());
    }
}

}  // namespace ripple

#endif  // RIPPLE_TX_AMMOFFER_H_INCLUDED
