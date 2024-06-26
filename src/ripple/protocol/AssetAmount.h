//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_TOKENAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_TOKENAMOUNT_H_INCLUDED

#include <ripple/basics/Number.h>
#include <ripple/protocol/Asset.h>

#include <cstdint>
#include <utility>

namespace ripple {

struct AssetAmountConst
{
    static constexpr int cMinOffset = -96;
    static constexpr int cMaxOffset = 80;

    // Maximum native value supported by the code
    static constexpr std::uint64_t cMinValue = 1000000000000000ull;
    static constexpr std::uint64_t cMaxValue = 9999999999999999ull;
    static constexpr std::uint64_t cMaxNative = 9000000000000000000ull;

    // Max native value on network.
    static constexpr std::uint64_t cMaxNativeN = 100000000000000000ull;
    static constexpr std::uint64_t cIssuedCurrency = 0x8000000000000000ull;
    static constexpr std::uint64_t cPositive = 0x4000000000000000ull;
    static constexpr std::uint64_t cMPToken = 0x2000000000000000ull;
    static constexpr std::uint64_t cValueMask = ~(cPositive | cMPToken);
};

template <typename A>
concept AssetType = std::is_same_v<A, Issue> || std::is_same_v<A, MPTIssue> ||
    std::is_same_v<A, Asset>;

template <AssetType TIss>
class AssetAmount : public AssetAmountConst
{
public:
    using mantissa_type = std::uint64_t;
    using exponent_type = int;
    using rep = std::pair<mantissa_type, exponent_type>;

    struct unchecked
    {
        explicit unchecked() = default;
    };

protected:
    TIss asset_;
    mantissa_type value_;
    exponent_type exponent_;
    bool isNative_;
    bool isNegative_;

public:
    AssetAmount(
        TIss const& iss,
        mantissa_type value,
        exponent_type exponent,
        bool isNegative,
        unchecked);

    AssetAmount(
        TIss const& iss,
        mantissa_type value = 0,
        exponent_type exponent = 0,
        bool isNegative = false);

    AssetAmount(TIss const& iss, Number const& n);

    // XRP drops
    AssetAmount(mantissa_type value = 0);

    TIss const&
    asset() const
    {
        return asset_;
    }

    operator Number() const;

    AssetAmount<TIss>&
    operator+=(AssetAmount<TIss> const&);

    AssetAmount<TIss>&
    operator-=(AssetAmount<TIss> const&);

    AssetAmount<TIss>& operator=(beast::Zero);

    AccountID const&
    getIssuer() const;

    mantissa_type
    mantissa() const;

    exponent_type
    exponent() const;

    bool
    negative() const;

    int
    signum() const noexcept;

    AssetAmount<TIss> const&
    value() const noexcept;

    AssetAmount<TIss>
    zeroed() const;

    void
    clear();

    // Zero while copying the asset
    void
    clear(AssetAmount<TIss> const& a);

    void
    clear(TIss const& iss);

    void
    negate();

    void
    setAsset(TIss const& iss);

protected:
    void
    canonicalize();

    friend AssetAmount<TIss>
    operator+(AssetAmount<TIss> const& v1, AssetAmount<TIss> const& v2);
};

template <AssetType TIss>
bool
isNative(TIss const& issue)
{
    if constexpr (std::is_same_v<TIss, MPTIssue>)
        return false;
    else if constexpr (std::is_same_v<TIss, Issue>)
        return isXRP(issue);
    else if constexpr (std::is_same_v<TIss, Asset>)
        return isXRP(issue.issue());
}

template <AssetType TIss>
bool
isMPT(TIss const& issue)
{
    if constexpr (std::is_same_v<TIss, MPTIssue>)
        return true;
    else if constexpr (std::is_same_v<TIss, Issue>)
        return false;
    else if constexpr (std::is_same_v<TIss, Asset>)
        return issue.isMPT();
}

template <AssetType TIss>
AssetAmount<TIss>::AssetAmount(
    TIss const& iss,
    mantissa_type value,
    exponent_type exponent,
    bool isNegative,
    unchecked)
    : asset_(iss)
    , value_(value)
    , exponent_(exponent)
    , isNegative_(isNegative)
    , isNative_(isNative(iss))
{
}

template <AssetType TIss>
AssetAmount<TIss>::AssetAmount(
    TIss const& iss,
    mantissa_type value,
    exponent_type exponent,
    bool isNegative)
    : asset_(iss)
    , value_(value)
    , exponent_(exponent)
    , isNegative_(isNegative)
    , isNative_(isNative(iss))
{
    canonicalize();
}

template <AssetType TIss>
AssetAmount<TIss>::AssetAmount(TIss const& iss, Number const& n)
    : AssetAmount(
          iss,
          n.mantissa() > 0 ? n.mantissa() : -n.mantissa(),
          n.exponent(),
          n.mantissa() < 0)
{
}

template <AssetType TIss>
AssetAmount<TIss>::operator Number() const
{
    return Number(isNegative_ ? -value_ : value_, exponent_);
}

template <AssetType TIss>
AssetAmount<TIss>&
AssetAmount<TIss>::operator+=(AssetAmount<TIss> const& a)
{
    *this = *this + a;
    return *this;
}

template <AssetType TIss>
AssetAmount<TIss>&
AssetAmount<TIss>::operator-=(AssetAmount<TIss> const& a)
{
    *this = *this - a;
    return *this;
}

template <AssetType TIss>
AssetAmount<TIss>& AssetAmount<TIss>::operator=(beast::Zero)
{
    clear();
    return *this;
}

template <AssetType TIss>
AssetAmount<TIss>::mantissa_type
AssetAmount<TIss>::mantissa() const
{
    return value_;
}

template <AssetType TIss>
AssetAmount<TIss>::exponent_type
AssetAmount<TIss>::exponent() const
{
    return exponent_;
}

template <AssetType TIss>
bool
AssetAmount<TIss>::negative() const
{
    return isNegative_;
}

template <AssetType TIss>
int
AssetAmount<TIss>::signum() const noexcept
{
    return value_ ? (isNegative_ ? -1 : 1) : 0;
}

template <AssetType TIss>
AccountID const&
AssetAmount<TIss>::getIssuer() const
{
    return asset_.getIssuer();
}

template <AssetType TIss>
AssetAmount<TIss> const&
AssetAmount<TIss>::value() const noexcept
{
    return *this;
}

template <AssetType TIss>
AssetAmount<TIss>
AssetAmount<TIss>::zeroed() const
{
    return AssetAmount<TIss>(asset_);
}

template <AssetType TIss>
void
AssetAmount<TIss>::clear()
{
    exponent_ = isNative_ ? 0 : -100;
    value_ = 0;
    isNegative_ = false;
}

template <AssetType TIss>
void
AssetAmount<TIss>::clear(TIss const& iss)
{
    asset_ = iss;
    isNative_ = isNative(iss);
    clear();
}

template <AssetType TIss>
void
AssetAmount<TIss>::clear(AssetAmount<TIss> const& a)
{
    clear(a.asset());
}

template <AssetType TIss>
void
AssetAmount<TIss>::negate()
{
    if (*this != beast::zero)
        isNegative_ = !isNegative_;
}

template <AssetType TIss>
void
AssetAmount<TIss>::setAsset(TIss const& iss)
{
    asset_ = iss;
}

namespace detail {

static constexpr std::uint64_t tenTo14 = 100000000000000ull;
static constexpr std::uint64_t tenTo14m1 = tenTo14 - 1;
static constexpr std::uint64_t tenTo17 = tenTo14 * 1000;

template <AssetType TIss>
bool
areComparable(AssetAmount<TIss> const& v1, AssetAmount<TIss> const& v2)
{
    if constexpr (std::is_same_v<TIss, Issue> || std::is_same_v<TIss, MPTIssue>)
        return v1.getAssetID() == v2.getAssetID();
    else if constexpr (std::is_same_v<TIss, Asset>)
    {
        return (
            (isMPT(v1) && isMPT(v2) && v1.asset() == v2.asset()) ||
            (v1.isIssue() && isNative(v1) == isNative(v2) &&
             v1.issue().getAssetID() == v2.issue().getAssetID()));
    }
}

// Calculate (a * b) / c when all three values are 64-bit
// without loss of precision:
inline std::uint64_t
muldiv(
    std::uint64_t multiplier,
    std::uint64_t multiplicand,
    std::uint64_t divisor)
{
    boost::multiprecision::uint128_t ret;

    boost::multiprecision::multiply(ret, multiplier, multiplicand);
    ret /= divisor;

    if (ret > std::numeric_limits<std::uint64_t>::max())
    {
        Throw<std::overflow_error>(
            "overflow: (" + std::to_string(multiplier) + " * " +
            std::to_string(multiplicand) + ") / " + std::to_string(divisor));
    }

    return static_cast<uint64_t>(ret);
}

inline std::uint64_t
muldiv_round(
    std::uint64_t multiplier,
    std::uint64_t multiplicand,
    std::uint64_t divisor,
    std::uint64_t rounding)
{
    boost::multiprecision::uint128_t ret;

    boost::multiprecision::multiply(ret, multiplier, multiplicand);
    ret += rounding;
    ret /= divisor;

    if (ret > std::numeric_limits<std::uint64_t>::max())
    {
        Throw<std::overflow_error>(
            "overflow: ((" + std::to_string(multiplier) + " * " +
            std::to_string(multiplicand) + ") + " + std::to_string(rounding) +
            ") / " + std::to_string(divisor));
    }

    return static_cast<uint64_t>(ret);
}

template <AssetType TIss>
std::int64_t
getSNValue(AssetAmount<TIss> const& amount)
{
    if (!isNative(amount))
        Throw<std::runtime_error>("amount is not native!");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    assert(static_cast<std::uint64_t>(ret) == amount.mantissa());

    if (amount.negative())
        ret = -ret;

    return ret;
}

template <AssetType TIss>
std::int64_t
getMPTValue(AssetAmount<TIss> const& amount)
{
    if (!isMPT(amount))
        Throw<std::runtime_error>("amount is not MPT!");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    assert(static_cast<std::uint64_t>(ret) == amount.mantissa());

    if (amount.negative())
        ret = -ret;

    return ret;
}

// This is the legacy version of canonicalizeRound.  It's been in use
// for years, so it is deeply embedded in the behavior of cross-currency
// transactions.
//
// However in 2022 it was noticed that the rounding characteristics were
// surprising.  When the code converts from IOU-like to XRP-like there may
// be a fraction of the IOU-like representation that is too small to be
// represented in drops.  `canonicalizeRound()` currently does some unusual
// rounding.
//
//  1. If the fractional part is greater than or equal to 0.1, then the
//     number of drops is rounded up.
//
//  2. However, if the fractional part is less than 0.1 (for example,
//     0.099999), then the number of drops is rounded down.
//
// The XRP Ledger has this rounding behavior baked in.  But there are
// situations where this rounding behavior led to undesirable outcomes.
// So an alternative rounding approach was introduced.  You'll see that
// alternative below.
inline void
canonicalizeRound(bool native, std::uint64_t& value, int& offset, bool)
{
    if (native)
    {
        if (offset < 0)
        {
            int loops = 0;

            while (offset < -1)
            {
                value /= 10;
                ++offset;
                ++loops;
            }

            value += (loops >= 2) ? 9 : 10;  // add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > AssetAmountConst::cMaxValue)
    {
        while (value > (10 * AssetAmountConst::cMaxValue))
        {
            value /= 10;
            ++offset;
        }

        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

// The original canonicalizeRound did not allow the rounding direction to
// be specified.  It also ignored some of the bits that could contribute to
// rounding decisions.  canonicalizeRoundStrict() tracks all of the bits in
// the value being rounded.
void
canonicalizeRoundStrict(
    bool native,
    std::uint64_t& value,
    int& offset,
    bool roundUp)
{
    if (native)
    {
        if (offset < 0)
        {
            bool hadRemainder = false;

            while (offset < -1)
            {
                // It would be better to use std::lldiv than to separately
                // compute the remainder.  But std::lldiv does not support
                // unsigned arguments.
                std::uint64_t const newValue = value / 10;
                hadRemainder |= (value != (newValue * 10));
                value = newValue;
                ++offset;
            }
            value +=
                (hadRemainder && roundUp) ? 10 : 9;  // Add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > AssetAmountConst::cMaxValue)
    {
        while (value > (10 * AssetAmountConst::cMaxValue))
        {
            value /= 10;
            ++offset;
        }
        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

// saveNumberRoundMode doesn't do quite enough for us.  What we want is a
// Number::RoundModeGuard that sets the new mode and restores the old mode
// when it leaves scope.  Since Number doesn't have that facility, we'll
// build it here.
class NumberRoundModeGuard
{
    saveNumberRoundMode saved_;

public:
    explicit NumberRoundModeGuard(Number::rounding_mode mode) noexcept
        : saved_{Number::setround(mode)}
    {
    }

    NumberRoundModeGuard(NumberRoundModeGuard const&) = delete;

    NumberRoundModeGuard&
    operator=(NumberRoundModeGuard const&) = delete;
};

// We need a class that has an interface similar to NumberRoundModeGuard
// but does nothing.
class DontAffectNumberRoundMode
{
public:
    explicit DontAffectNumberRoundMode(Number::rounding_mode mode) noexcept
    {
    }

    DontAffectNumberRoundMode(DontAffectNumberRoundMode const&) = delete;

    DontAffectNumberRoundMode&
    operator=(DontAffectNumberRoundMode const&) = delete;
};

// Pass the canonicalizeRound function pointer as a template parameter.
//
// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <
    void (*CanonicalizeFunc)(bool, std::uint64_t&, int&, bool),
    typename MightSaveRound,
    AssetType TIss>
AssetAmount<TIss>
mulRoundImpl(
    AssetAmount<TIss> const& v1,
    AssetAmount<TIss> const& v2,
    TIss const& asset,
    bool roundUp)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return {asset};

    bool const xrp = isXRP(asset);

    // TODO MPT
    if (isNative(v1) && isNative(v2) && xrp)
    {
        std::uint64_t minV = (detail::getSNValue(v1) < detail::getSNValue(v2))
            ? detail::getSNValue(v1)
            : detail::getSNValue(v2);
        std::uint64_t maxV = (detail::getSNValue(v1) < detail::getSNValue(v2))
            ? detail::getSNValue(v2)
            : detail::getSNValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return AssetAmount<TIss>(minV * maxV);
    }
    // TODO MPT
    if (isMPT(v1) && isMPT(v2) && isMPT(asset))
    {
        std::uint64_t minV = (detail::getMPTValue(v1) < detail::getMPTValue(v2))
            ? detail::getMPTValue(v1)
            : detail::getMPTValue(v2);
        std::uint64_t maxV = (detail::getMPTValue(v1) < detail::getMPTValue(v2))
            ? detail::getMPTValue(v2)
            : detail::getMPTValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Asset value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Asset value overflow");

        return AssetAmount<TIss>(asset, minV * maxV);
    }

    std::uint64_t value1 = v1.mantissa(), value2 = v2.mantissa();
    int offset1 = v1.exponent(), offset2 = v2.exponent();

    // TODO MPT
    if (isNative(v1) || isMPT(v1))
    {
        while (value1 < AssetAmount<TIss>::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    // TODO MPT
    if (isNative(v2) || isMPT(v2))
    {
        while (value2 < AssetAmount<TIss>::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    bool const resultNegative = v1.negative() != v2.negative();

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    //
    // If the we're rounding up, we want to round up away
    // from zero, and if we're rounding down, truncation
    // is implicit.
    std::uint64_t amount = detail::muldiv_round(
        value1,
        value2,
        detail::tenTo14,
        (resultNegative != roundUp) ? detail::tenTo14m1 : 0);

    int offset = offset1 + offset2 + 14;
    if (resultNegative != roundUp)
    {
        CanonicalizeFunc(xrp, amount, offset, roundUp);
    }
    AssetAmount<TIss> result = [&]() {
        // If appropriate, tell Number to round down.  This gives the desired
        // result from STAmount::canonicalize.
        MightSaveRound const savedRound(Number::towards_zero);
        return AssetAmount<TIss>(asset, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (xrp)
        {
            // return the smallest value above zero
            amount = 1;
            offset = 0;
        }
        else
        {
            // return the smallest value above zero
            amount = AssetAmount<TIss>::cMinValue;
            offset = AssetAmount<TIss>::cMinOffset;
        }
        return AssetAmount<TIss>(asset, amount, offset, resultNegative);
    }
    return result;
}

// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <typename MightSaveRound, AssetType TIss>
static AssetAmount<TIss>
divRoundImpl(
    AssetAmount<TIss> const& num,
    AssetAmount<TIss> const& den,
    TIss const& asset,
    bool roundUp)
{
    if (den == beast::zero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::zero)
        return {asset};

    std::uint64_t numVal = num.mantissa(), denVal = den.mantissa();
    int numOffset = num.exponent(), denOffset = den.exponent();

    // TODO MPT
    if (isNative(num) || isMPT(num))
    {
        while (numVal < AssetAmount<TIss>::cMinValue)
        {
            numVal *= 10;
            --numOffset;
        }
    }

    // TODO MPT
    if (isNative(den) || isMPT(den))
    {
        while (denVal < AssetAmount<TIss>::cMinValue)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    bool const resultNegative = (num.negative() != den.negative());

    // We divide the two mantissas (each is between 10^15
    // and 10^16). To maintain precision, we multiply the
    // numerator by 10^17 (the product is in the range of
    // 10^32 to 10^33) followed by a division, so the result
    // is in the range of 10^16 to 10^15.
    //
    // We round away from zero if we're rounding up or
    // truncate if we're rounding down.
    std::uint64_t amount = detail::muldiv_round(
        numVal,
        detail::tenTo17,
        denVal,
        (resultNegative != roundUp) ? denVal - 1 : 0);

    int offset = numOffset - denOffset - 17;

    // TODO MPT
    if (resultNegative != roundUp)
        canonicalizeRound(
            isXRP(asset) || isMPT(asset), amount, offset, roundUp);

    AssetAmount<TIss> result = [&]() {
        // If appropriate, tell Number the rounding mode we are using.
        // Note that "roundUp == true" actually means "round away from zero".
        // Otherwise round toward zero.
        using enum Number::rounding_mode;
        MightSaveRound const savedRound(
            roundUp ^ resultNegative ? upward : downward);
        return AssetAmount<TIss>(asset, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (isNative(asset) || isMPT(asset))
        {
            // return the smallest value above zero
            amount = 1;
            offset = 0;
        }
        else
        {
            // return the smallest value above zero
            amount = AssetAmount<TIss>::cMinValue;
            offset = AssetAmount<TIss>::cMinOffset;
        }
        return AssetAmount<TIss>(asset, amount, offset, resultNegative);
    }
    return result;
}

}  // namespace detail

template <AssetType TIss>
AssetAmount<TIss>
operator+(AssetAmount<TIss> const& v1, AssetAmount<TIss> const& v2)
{
    if (!areComparable(v1, v2))
        Throw<std::runtime_error>("Can't add amounts that aren't comparable!");

    if (v2 == beast::zero)
        return v1;

    if (v1 == beast::zero)
    {
        // Result must be in terms of v1 currency and issuer.
        return AssetAmount<TIss>{
            v1.asset(), v2.mantissa(), v2.exponent(), v2.negative()};
    }

    // TODO
    if (isNative(v1))
        return {detail::getSNValue(v1) + detail::getSNValue(v2)};
    if (isMPT(v1))
        return {v1.mAsset, v1.mpt().mpt() + v2.mpt().mpt()};

    if (getSTNumberSwitchover())
        return AssetAmount{
            v1.asset(), static_cast<Number>(v1) + static_cast<Number>(v2)};

    int ov1 = v1.exponent(), ov2 = v2.exponent();
    std::int64_t vv1 = static_cast<std::int64_t>(v1.mantissa());
    std::int64_t vv2 = static_cast<std::int64_t>(v2.mantissa());

    if (v1.negative())
        vv1 = -vv1;

    if (v2.negative())
        vv2 = -vv2;

    while (ov1 < ov2)
    {
        vv1 /= 10;
        ++ov1;
    }

    while (ov2 < ov1)
    {
        vv2 /= 10;
        ++ov2;
    }

    // This addition cannot overflow an std::int64_t. It can overflow an
    // STAmount and the constructor will throw.

    std::int64_t fv = vv1 + vv2;

    if ((fv >= -10) && (fv <= 10))
        return {v1.asset()};

    if (fv >= 0)
        return AssetAmount<TIss>{
            v1.asset(), static_cast<std::uint64_t>(fv), ov1, false};

    return AssetAmount<TIss>{
        v1.asset(), static_cast<std::uint64_t>(-fv), ov1, true};
}

template <AssetType TIss>
AssetAmount<TIss>
operator-(AssetAmount<TIss> const& v1, AssetAmount<TIss> const& v2)
{
    return v1 + (-v2);
}

template <AssetType TIss>
AssetAmount<TIss>
divide(
    AssetAmount<TIss> const& num,
    AssetAmount<TIss> const& den,
    TIss const& asset)
{
    if (den == beast::zero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::zero)
        return {asset};

    std::uint64_t numVal = num.mantissa();
    std::uint64_t denVal = den.mantissa();
    int numOffset = num.exponent();
    int denOffset = den.exponent();

    if (num.native() || num.isMPT())
    {
        while (numVal < AssetAmount<TIss>::cMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.native() || den.isMPT())
    {
        while (denVal < AssetAmount<TIss>::cMinValue)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    // We divide the two mantissas (each is between 10^15
    // and 10^16). To maintain precision, we multiply the
    // numerator by 10^17 (the product is in the range of
    // 10^32 to 10^33) followed by a division, so the result
    // is in the range of 10^16 to 10^15.
    return STAmount(
        asset,
        detail::muldiv(numVal, detail::tenTo17, denVal) + 5,
        numOffset - denOffset - 17,
        num.negative() != den.negative());
}

template <AssetType TIss>
AssetAmount<TIss>
multiply(
    AssetAmount<TIss> const& v1,
    AssetAmount<TIss> const& v2,
    TIss const& asset)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return AssetAmount<TIss>(asset);

    if (isNative(v1) && isNative(v2) && isXRP(asset))
    {
        std::uint64_t const minV =
            detail::getSNValue(v1) < detail::getSNValue(v2)
            ? detail::getSNValue(v1)
            : detail::getSNValue(v2);
        std::uint64_t const maxV =
            detail::getSNValue(v1) < detail::getSNValue(v2)
            ? detail::getSNValue(v2)
            : detail::getSNValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(v1.getFName(), minV * maxV);
    }
    if (v1.isMPT() && v2.isMPT() && asset.isMPT())
    {
        std::uint64_t const minV =
            detail::getMPTValue(v1) < detail::getMPTValue(v2)
            ? detail::getMPTValue(v1)
            : detail::getMPTValue(v2);
        std::uint64_t const maxV =
            detail::getMPTValue(v1) < detail::getMPTValue(v2)
            ? detail::getMPTValue(v2)
            : detail::getMPTValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Asset value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Asset value overflow");

        return AssetAmount<TIss>(asset, minV * maxV);
    }

    if (getSTNumberSwitchover())
        return {Number{v1} * Number{v2}, asset};

    std::uint64_t value1 = v1.mantissa();
    std::uint64_t value2 = v2.mantissa();
    int offset1 = v1.exponent();
    int offset2 = v2.exponent();

    // TODO MPT
    if (isNative(v1) || isMPT(v1))
    {
        while (value1 < AssetAmount<TIss>::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (isNative(v2) || isMPT(v2))
    {
        while (value2 < AssetAmount<TIss>::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    return AssetAmount<TIss>(
        asset,
        detail::muldiv(value1, value2, detail::tenTo14) + 7,
        offset1 + offset2 + 14,
        v1.negative() != v2.negative());
}

template <AssetType TIss>
AssetAmount<TIss>
mulRound(
    AssetAmount<TIss> const& v1,
    AssetAmount<TIss> const& v2,
    TIss const& asset,
    bool roundUp)
{
    return detail::mulRoundImpl<
        detail::canonicalizeRound,
        detail::DontAffectNumberRoundMode,
        TIss>(v1, v2, asset, roundUp);
}

template <AssetType TIss>
AssetAmount<TIss>
mulRoundStrict(
    AssetAmount<TIss> const& v1,
    AssetAmount<TIss> const& v2,
    TIss const& asset,
    bool roundUp)
{
    return detail::mulRoundImpl<
        detail::canonicalizeRoundStrict,
        detail::NumberRoundModeGuard,
        TIss>(v1, v2, asset, roundUp);
}

template <AssetType TIss>
AssetAmount<TIss>
divRound(
    AssetAmount<TIss> const& num,
    AssetAmount<TIss> const& den,
    TIss const& asset,
    bool roundUp)
{
    return detail::divRoundImpl<detail::DontAffectNumberRoundMode, TIss>(
        num, den, asset, roundUp);
}

template <AssetType TIss>
AssetAmount<TIss>
divRoundStrict(
    AssetAmount<TIss> const& num,
    AssetAmount<TIss> const& den,
    TIss const& asset,
    bool roundUp)
{
    return detail::divRoundImpl<detail::NumberRoundModeGuard, TIss>(
        num, den, asset, roundUp);
}

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_TOKENAMOUNT_H_INCLUDED
