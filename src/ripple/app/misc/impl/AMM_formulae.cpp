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

#include <ripple/app/misc/AMM_formulae.h>

#include <cmath>

namespace ripple {

namespace detail {
#pragma message( \
    "THIS IS TEMPORARY. PORTABLE POW() AND NUMBER CLASS WILL BE IMPLEMENTED TO FACILITATE OPS ON MIXED AMOUNTS AND UINT.")

double
saToDouble(STAmount const& a)
{
    return static_cast<double>(a.mantissa() * std::pow(10, a.exponent()));
}

STAmount
toSTAfromDouble(double v, Issue const& issue)
{
    auto exponent = decimal_places(v);
    std::int64_t mantissa = v * pow(10, exponent);
    exponent = -exponent;
    return STAmount(issue, mantissa, exponent);
}

STAmount
sqrt(STAmount const& a)
{
    return toSTAfromDouble(std::sqrt(saToDouble(a)), a.issue());
}

}  // namespace detail

STAmount
calcAMMLPT(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue,
    std::uint8_t weight1)
{
    assert(weight1 == 50);

    return detail::sqrt(multiply(asset1, asset2, lptIssue));
}

std::optional<STAmount>
calcLPTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lpTokensBalance,
    std::uint16_t weight,
    std::uint16_t tfee)
{
    assert(weight == 50);

    auto const num =
        asset1Balance +
        multiply(
            asset1Deposit, getFeeMult(tfee, weight), asset1Balance.issue());
    auto const fr = detail::sqrt(divide(num, asset1Balance, noIssue())) -
        STAmount{noIssue(), 1};
    if (fr.negative() || fr == beast::zero)
        return std::nullopt;
    return multiply(lpTokensBalance, fr, lpTokensBalance.issue());
}

std::optional<STAmount>
calcAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lpTokensBalance,
    STAmount const& lptAMMBalance,
    std::uint16_t weight1,
    std::uint16_t tfee)
{
    assert(weight1 == 50);

    auto const sq = divide(lpTokensBalance, lptAMMBalance, noIssue()) +
        STAmount{noIssue(), 1};
    auto const num = multiply(sq, sq, noIssue()) - STAmount{noIssue(), 1};
    auto const fr = divide(num, getFeeMult(tfee, weight1), noIssue());
    if (fr == beast::zero || fr.negative())
        return std::nullopt;
    return multiply(asset1Balance, fr, asset1Balance.issue());
}

std::optional<STAmount>
calcLPTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lpTokensBalance,
    std::uint16_t weight,
    std::uint16_t tfee)
{
    assert(weight == 50);

    auto const den = multiply(
        asset1Balance, getFeeMult(tfee, weight), asset1Balance.issue());
    auto const num = den - asset1Withdraw;
    auto const fr =
        STAmount{noIssue(), 1} - detail::sqrt(divide(num, den, noIssue()));
    if (fr.negative() || fr == beast::zero)
        return std::nullopt;
    return multiply(lpTokensBalance, fr, lpTokensBalance.issue());
}

STAmount
calcSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    assert(weight1 == 50);

    auto const num = multiply(
        asset2Balance,
        STAmount{asset2Balance.issue(), weight1},
        asset2Balance.issue());
    auto const den = multiply(
        multiply(
            asset1Balance,
            STAmount(asset1Balance.issue(), 100 - weight1),
            asset1Balance.issue()),
        getFeeMult(tfee),
        asset1Balance.issue());
    return divide(num, den, asset2Balance.issue());
}

STAmount
calcEffectivePrice(STAmount const& asset1Balance, STAmount const& asset2Balance)
{
    return divide(asset1Balance, asset2Balance, asset1Balance.issue());
}

std::optional<STAmount>
changeSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& newSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    assert(weight1 == 50);

    auto const sp = calcSpotPrice(asset1Balance, asset2Balance, weight1, tfee);
    auto const fr = std::pow(
                        detail::saToDouble(divide(newSP, sp, newSP.issue())),
                        static_cast<double>(weight1) / 100.) -
        1.;
    if (fr <= 0)
        return std::nullopt;
    return multiply(
        asset1Balance, detail::toSTAfromDouble(fr), asset1Balance.issue());
}

}  // namespace ripple
