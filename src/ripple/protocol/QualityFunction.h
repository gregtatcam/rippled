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

#ifndef RIPPLE_PROTOCOL_QUALITYFUNCTION_H_INCLUDED
#define RIPPLE_PROTOCOL_QUALITYFUNCTION_H_INCLUDED

#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/basics/Number.h>
#include <ripple/protocol/Quality.h>

namespace ripple {

/** Average Quality as a function of out: q(out) = m * out + b,
 * where m = -f / poolGets, b = f*poolPays / poolGets, f = 1 - trading_fee
 * for AMM offers and m = 0, b = Quality for CLOB offers. The function
 * is derived from the swap-out formula:
 * in = poolGets * (poolPays/(poolPays-out) - 1)/f.
 * AvgQFunction is used to derive InstQFunction and to find the required
 * output amount when the quality limit is included in the payment transaction.
 */
class AvgQFunction
{
    friend class InstQFunction;

private:
    // Slope
    Number m_;
    // Intercept
    Number b_;
    // Seated if QF is for CLOB offer. Note that m_ is 0 in this case.
    std::optional<Quality> quality_;
    // Quality limit defines the range where the function is valid. This
    // is the case when there is AMM offer and CLOB offer and AMM offer has
    // a better spot price quality. CLOB offer quality is the limit
    // in this case.
    std::optional<Quality> qLimit_;

public:
    struct AMMTag
    {
    };
    struct CLOBTag
    {
    };
    /** Constructor for CLOB offer
     * @param quality CLOB offer quality
     */
    AvgQFunction(Quality const& quality, CLOBTag);
    /** Constructor for AMM offer
     * @param amounts AMM offer amounts
     * @param qLimit CLOB offer quality if seated
     * @param tfee trading fee
     */
    template <typename TIn, typename TOut>
    AvgQFunction(
        TAmounts<TIn, TOut> const& amounts,
        std::optional<Quality> const& qLimit,
        std::uint32_t tfee,
        AMMTag);
    AvgQFunction();
    /** Combine qf with the next step qf.
     */
    void
    combineWithNext(AvgQFunction const& qf);

    /** Find output to generate the requested
     * average quality.
     * @param quality requested average quality (quality limit)
     */
    Number
    outFromQ(Quality const& quality) const;

    static Number
    outFromQ(
        Number const& m,
        Number const& b,
        Quality const& quality,
        std::optional<Quality> const& qLimit);

    /** Find output given input.
     */
    Number
    outFromIn(Number const& input) const;

    /** Find quality given output
     */
    Quality
    qFromOut(Number const& output) const;

    /** Return true if the quality function is constant
     */
    bool
    isConstQ() const
    {
        return quality_.has_value();
    }

    std::optional<Quality>
    quality() const
    {
        return quality_;
    }
};

/** Instantiate the quality from out/in ratio. This is similar to how
 * the quality is instantiated from amounts with getRate() (see Quality(Amounts)
 * constructor).
 */
static inline Quality
toQuality(Number const& q)
{
    try
    {
        // q is out/in but getRate(out, in) transforms in/out
        // so take the inverse of q.
        auto const r = toSTAmount(noIssue(), 1 / q);
        if (r == beast::zero)  // offer is too good
            return Quality{0};
        assert((r.exponent() >= -100) && (r.exponent() <= 155));
        std::uint64_t ret = r.exponent() + 100;
        return Quality{(ret << (64 - 8)) | r.mantissa()};
    }
    catch (std::exception const&)
    {
        return Quality{0};
    }
}

template <typename TIn, typename TOut>
AvgQFunction::AvgQFunction(
    TAmounts<TIn, TOut> const& amounts,
    std::optional<Quality> const& qLimit,
    std::uint32_t tfee,
    AvgQFunction::AMMTag)
{
    if (amounts.in <= beast::zero || amounts.out <= beast::zero)
        Throw<std::runtime_error>("AvgQFunction amounts are 0.");
    auto const cfee = feeMult(tfee);
    m_ = -cfee / amounts.in;
    b_ = amounts.out * cfee / amounts.in;
    qLimit_ = std::nullopt;
    if (qLimit)
    {
        // qLimit is for inst quality. The limit for this function
        // should be for avg quality. this is solution for inst q.
        auto const out = -(b_ - root2(b_ / qLimit->rate()) / m_);
        qLimit_ = qFromOut(out);
    }
}

/** Instant quality (also spot price quality) function. Defines two functions
 * q(out) and q(in). Both functions are derived from the avg quality function
 * q(out) = m*out + b by taking the derivative with respect to out and in
 * respectively. q(out) = (m*out + b)^2)/b, q(in) = b/(1 - m*in)^2.
 * The functions are used to find the strands required to output
 * the requested amount while optimizing on the overall quality and
 * meeting the limitations like SendMax, etc.
 */
class InstQFunction
{
private:
    // Average quality slope
    Number m_;
    // Average quality intercept
    Number b_;
    // Average quality limit
    std::optional<Quality> avgQLimit_;
    // Instant quality limit
    std::optional<Quality> qLimit_;

public:
    InstQFunction() = default;
    InstQFunction(AvgQFunction const& qf);

    /** Return the quality given the output
     */
    Quality
    qFromOut(Number const& output) const;

    /** Return the output given the quality
     */
    Number
    outFromQ(Quality const& q) const;

    /** Return the output given the average quality
     */
    Number
    outFromAvgQ(Quality const& q) const;

    /** Return the input given the quality
     */
    Number
    inFromQ(Quality const& q) const;

    /** Return the spot quality
     */
    Quality
    spotQuality() const;

    /** Return the slope
     */
    Number
    slope() const;

    /** Return qLimit
     */
    std::optional<Quality> const&
    qLimit() const;

    bool
    isConstQ() const
    {
        return m_ == 0;
    }

    /** Find the quality so the sum of the output from all
     * quality functions equals to required output.
     */
    static Quality
    splitOutReqBetweenStrands(
        auto const& cbeginIt,
        auto const& cendIt,
        Number const& req,
        auto const& qGetter = [](auto const& q) { return q; });

    /** Find the quality so the sum of the input from all
     * quality functions equals to required input.
     */
    static Quality
    splitInReqBetweenStrands(
        auto const& cbeginIt,
        auto const& cendIt,
        Number const& req,
        auto const& qGetter = [](auto const& q) { return q; });
};

Quality
InstQFunction::splitOutReqBetweenStrands(
    auto const& cbeginIt,
    auto const& cendIt,
    const Number& req,
    auto const& qGetter)
{
    Number a = 0;
    Number b = 0;

    for (auto it = cbeginIt; it != cendIt; ++it)
    {
        auto const& q = qGetter(*it);
        if (q.m_ == 0)
            return q.spotQuality();

        a += -q.b_ / q.m_;
        b += root2(q.b_) / q.m_;
    }

    a -= req;

    if (b == 0)
        Throw<std::runtime_error>("splitOutReqBetweenStrands error");

    auto const r = -a / b;

    return toQuality(r * r);
}

Quality
InstQFunction::splitInReqBetweenStrands(
    auto const& cbeginIt,
    auto const& cendIt,
    const Number& req,
    auto const& qGetter)
{
    Number a = 0;
    Number b = 0;

    for (auto it = cbeginIt; it != cendIt; ++it)
    {
        auto const& q = qGetter(*it);
        if (q.m_ == 0)
            return q.spotQuality();

        a += root2(q.b_) / q.m_;
        b += 1 / q.m_;
    }

    b -= req;

    if (b == 0)
        Throw<std::runtime_error>("splitInReqBetweenStrands error");

    auto const r = a / b;

    return toQuality(r * r);
}

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_QUALITYFUNCTION_H_INCLUDED
