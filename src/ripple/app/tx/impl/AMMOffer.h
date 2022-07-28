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
#ifndef RIPPLE_APP_TX_AMMOFFER_H_INCLUDED
#define RIPPLE_APP_TX_AMMOFFER_H_INCLUDED

#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/app/tx/impl/AMMOfferMaker.h>
#include <ripple/app/tx/impl/Offer.h>
#include <ripple/basics/Log.h>

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
template <typename TIn = STAmount, typename TOut = STAmount>
class AMMOffer : public TOffer<TIn, TOut>
{
    std::function<void()> ammOfferCounter_;
    std::function<void()> setBalancesDirty_;
    beast::Journal j_;

public:
    AMMOffer(
        Amounts const& amounts,
        AccountID const& account,
        std::function<void()> setBalancesDirty,
        std::function<void()> ammOfferCounter,
        beast::Journal const j);

    AMMOffer&
    operator=(AMMOffer const& offer)
    {
        j_ = offer.j_;
        return *this;
    }

    /** Consume the offer. This method validates that the consumed size
     * is the same as the offer size. The offer size can not be changed
     * at this point since it will change the offer quality. See the class
     * comments above.
     */
    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed) override;

    /** AMM offer is always fully consumed
     */
    bool
    fully_consumed() const override
    {
        return true;
    }

    uint256
    key() const override
    {
        return uint256{0};
    }
};

template <typename T>
T
get(Number const& n)
{
    if constexpr (std::is_same_v<T, IOUAmount>)
        return (IOUAmount)n;
    else if constexpr (std::is_same_v<T, XRPAmount>)
        return (XRPAmount)n;
    else
        return STAmount{noIssue(), n.mantissa(), n.exponent()};
}

/*
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
}*/

template <typename TIn, typename TOut>
AMMOffer<TIn, TOut>::AMMOffer(
    Amounts const& amounts,
    AccountID const& account,
    std::function<void()> setBalancesDirty,
    std::function<void()> ammOfferCounter,
    beast::Journal const j)
    : TOffer<TIn, TOut>(Quality{amounts}, account)
    , ammOfferCounter_(ammOfferCounter)
    , setBalancesDirty_(setBalancesDirty)
    , j_(j)
{
    this->m_amounts = {get<TIn>(amounts.in), get<TOut>(amounts.out)};
    this->issIn_ = amounts.in.issue();
    this->issOut_ = amounts.out.issue();
}

template <>
inline AMMOffer<STAmount, STAmount>::AMMOffer(
    Amounts const& amounts,
    AccountID const& account,
    std::function<void()> setBalancesDirty,
    std::function<void()> ammOfferCounter,
    beast::Journal const j)
    : TOffer<>(Quality{amounts}, account)
    , ammOfferCounter_(ammOfferCounter)
    , setBalancesDirty_(setBalancesDirty)
    , j_(j)
{
    this->m_amounts = amounts;
}

template <typename T>
STAmount
toSTAmount(T const& amount, Issue const& issue)
{
    if constexpr (std::is_same_v<T, IOUAmount>)
        return toSTAmount(amount, issue);
    return amount;
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
    else
    {
        std::cout << "AMM offer consumed: in " << toStr(this->m_amounts.in)
                  << " out " << toStr(this->m_amounts.out) << std::endl;
        JLOG(j_.debug()) << "AMMOffer::consume in: "
                         << toStr(this->m_amounts.in)
                         << " out: " << toStr(this->m_amounts.out);
    }

    ammOfferCounter_();
    setBalancesDirty_();
}

}  // namespace ripple

#endif  // RIPPLE_APP_TX_AMMOFFER_H_INCLUDED
