//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_BASEAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_BASEAMOUNT_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/LocalValue.h>
#include <ripple/basics/MPTAmount.h>
#include <ripple/basics/Number.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/Asset.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/MPTIssue.h>
#include <ripple/protocol/json_get_or_throw.h>
#include <ripple/protocol/jss.h>

namespace ripple {

// Since `canonicalize` does not have access to a ledger, this is needed to put
// the low-level routine stAmountCanonicalize on an amendment switch. Only
// transactions need to use this switchover. Outside of a transaction it's safe
// to unconditionally use the new behavior.

bool
getSTAmountCanonicalizeSwitchover1();

void
setSTAmountCanonicalizeSwitchover1(bool v);

/** RAII class to set and restore the STAmount canonicalize switchover.
 */

class STAmountSO1
{
public:
    explicit STAmountSO1(bool v) : saved_(getSTAmountCanonicalizeSwitchover1())
    {
        setSTAmountCanonicalizeSwitchover1(v);
    }

    ~STAmountSO1()
    {
        setSTAmountCanonicalizeSwitchover1(saved_);
    }

private:
    bool saved_;
};

namespace BaseAmountConst {

constexpr std::uint64_t tenTo14 = 100000000000000ull;
constexpr std::uint64_t tenTo14m1 = tenTo14 - 1;
constexpr std::uint64_t tenTo17 = tenTo14 * 1000;

constexpr int cMinOffset = -96;
constexpr int cMaxOffset = 80;

// Maximum native value supported by the code
constexpr std::uint64_t cMinValue = 1000000000000000ull;
constexpr std::uint64_t cMaxValue = 9999999999999999ull;
constexpr std::uint64_t cMaxNative = 9000000000000000000ull;

// Max native value on network.
constexpr std::uint64_t cMaxNativeN = 100000000000000000ull;
constexpr std::uint64_t cIssuedCurrency = 0x8000000000000000ull;
constexpr std::uint64_t cPositive = 0x4000000000000000ull;
constexpr std::uint64_t cMPToken = 0x2000000000000000ull;
constexpr std::uint64_t cValueMask = ~(cPositive | cMPToken);

}  // namespace BaseAmountConst

template <typename A>
concept ValidAsset = std::is_same_v<A, Issue> || std::is_same_v<A, MPTIssue> ||
    std::is_same_v<A, Asset> || std::is_convertible_v<A, Issue> ||
    std::is_convertible_v<A, MPTIssue> || std::is_convertible_v<A, Asset>;

// Internal form:
// 1: If amount is zero, then value is zero and offset is -100
// 2: Otherwise:
//   legal offset range is -96 to +80 inclusive
//   value range is 10^15 to (10^16 - 1) inclusive
//  amount = value * [10 ^ offset]

// Wire form:
// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive
template <ValidAsset T>
class BaseAmount
{
public:
    using mantissa_type = std::uint64_t;
    using exponent_type = int;
    using rep = std::pair<mantissa_type, exponent_type>;

protected:
    T mAsset;
    mantissa_type mValue;
    exponent_type mOffset;
    bool mIsNative;  // A shorthand for isXRP(mIssue).
    bool mIsNegative;

public:
    static std::uint64_t const uRateOne;

    //--------------------------------------------------------------------------

    struct unchecked
    {
        explicit unchecked() = default;
    };

    // Do not call canonicalize
    BaseAmount(
        T const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative,
        unchecked);

    // Call canonicalize
    BaseAmount(
        T const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative);

    BaseAmount(std::int64_t mantissa);

    BaseAmount(
        T const& asset,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false);

    BaseAmount(std::uint64_t mantissa = 0, bool negative = false);

    BaseAmount(BaseAmount<T> const& amt);

    // VFALCO Is this needed when we have the previous signature?
    BaseAmount(
        T const& asset,
        std::uint32_t mantissa,
        int exponent = 0,
        bool negative = false);

    BaseAmount(T const& asset, std::int64_t mantissa, int exponent = 0);

    BaseAmount(T const& asset, int mantissa, int exponent = 0);

    BaseAmount(IOUAmount const& amount, Issue const& issue);
    BaseAmount(XRPAmount const& amount);
    BaseAmount(MPTAmount const& amount, MPTIssue const& issue);
    operator Number() const;

    BaseAmount<T>&
    operator=(BaseAmount<T> const& amount);

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    int
    exponent() const noexcept;

    bool
    negative() const noexcept;

    std::uint64_t
    mantissa() const noexcept;

    T const&
    asset() const;

    AccountID const&
    getIssuer() const;

    int
    signum() const noexcept;

    BaseAmount const&
    value() const noexcept;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    explicit operator bool() const noexcept;

    BaseAmount<T>&
    operator+=(BaseAmount<T> const&);
    BaseAmount<T>&
    operator-=(BaseAmount<T> const&);

    BaseAmount<T>& operator=(beast::Zero);

    BaseAmount<T>&
    operator=(XRPAmount const& amount);

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    void
    negate();

    void
    clear();

    // Zero while copying currency and issuer.
    void
    clear(BaseAmount<T> const& saTmpl);

    void
    clear(T const& issue);

    void
    setAsset(T const& a, bool native);

protected:
    void
    set(std::int64_t v);

    void
    canonicalize();

    BaseAmount<T>&
    operator=(IOUAmount const& iou);

    friend BaseAmount<T>
    operator+(BaseAmount<T> const& v1, BaseAmount<T> const& v2);
};

template <ValidAsset T>
bool
isNative(T const& asset)
{
    if constexpr (std::is_same_v<T, MPTIssue>)
        return false;
    else if constexpr (std::is_same_v<T, Issue>)
        return isXRP(asset);
    else if constexpr (std::is_same_v<T, Asset>)
        return asset.isIssue() && isXRP(asset.issue());
    else
    {
        constexpr bool alwaysFalse = !std::is_same_v<T, T>;
        static_assert(alwaysFalse, "Unsupported type for isNative");
    }
}

template <ValidAsset T>
bool
isMPT(T const& asset)
{
    if constexpr (std::is_same_v<T, MPTIssue>)
        return true;
    else if constexpr (std::is_same_v<T, Issue>)
        return false;
    else if constexpr (std::is_same_v<T, Asset>)
        return asset.isMPT();
    else
    {
        constexpr bool alwaysFalse = !std::is_same_v<T, T>;
        static_assert(alwaysFalse, "Unsupported type for isMPT");
    }
}

template <ValidAsset T>
bool
isNative(BaseAmount<T> const& amount)
{
    return isNative(amount.asset());
}

template <ValidAsset T>
bool
isXRP(BaseAmount<T> const& amount)
{
    return isNative(amount);
}

template <ValidAsset T>
bool
isMPT(BaseAmount<T> const& amount)
{
    return isMPT(amount.asset());
}

namespace detail {

template <ValidAsset T>
bool
areComparable(BaseAmount<T> const& v1, BaseAmount<T> const& v2)
{
    return (isMPT(v1) && isMPT(v2) &&
            v1.asset().mptIssue() == v2.asset().mptIssue()) ||
        (v1.isIssue() && isNative(v1) == isNative(v2) &&
         v1.asset().issue().currency == v2.asset().issue().currency);
}

}  // namespace detail

template <ValidAsset T>
BaseAmount<T>
operator+(BaseAmount<T> const& v1, BaseAmount<T> const& v2)
{
    if (!detail::areComparable(v1, v2))
        Throw<std::runtime_error>("Can't add amounts that are't comparable!");

    if (v2 == beast::zero)
        return v1;

    if (v1 == beast::zero)
    {
        // Result must be in terms of v1 currency and issuer.
        return BaseAmount<T>{
            v1.asset(), v2.mantissa(), v2.exponent(), v2.negative()};
    }

    // TODO
    if (isNative(v1.asset()))
        return {v1.getFName(), getSNValue(v1) + getSNValue(v2)};
    if (isMPT(v1.asset()))
        return {v1.mAsset, v1.mpt().mpt() + v2.mpt().mpt()};

    if (getSTNumberSwitchover())
    {
        auto x = v1;
        x = v1.iou() + v2.iou();
        return x;
    }

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
        return {v1.getFName(), v1.asset()};

    if (fv >= 0)
        return BaseAmount<T>{
            v1.asset(), static_cast<std::uint64_t>(fv), ov1, false};

    return BaseAmount<T>{
        v1.asset(), static_cast<std::uint64_t>(-fv), ov1, true};
}

template <ValidAsset T>
BaseAmount<T>
operator-(BaseAmount<T> const& v1, BaseAmount<T> const& v2)
{
    return v1 + (-v2);
}

template <ValidAsset T>
std::int64_t
getSNValue(BaseAmount<T> const& amount)
{
    if (!isNative(amount.asset()))
        Throw<std::runtime_error>("amount is not native!");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    assert(static_cast<std::uint64_t>(ret) == amount.mantissa());

    if (amount.negative())
        ret = -ret;

    return ret;
}

template <ValidAsset T>
std::int64_t
getMPTValue(BaseAmount<T> const& amount)
{
    if (!isMPT(amount.asset()))
        Throw<std::runtime_error>("amount is not MPT!");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    assert(static_cast<std::uint64_t>(ret) == amount.mantissa());

    if (amount.negative())
        ret = -ret;

    return ret;
}

namespace detail {

void
canonicalizeRound(bool nativeOrMPT, std::uint64_t& value, int& offset, bool)
{
    if (nativeOrMPT)
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
    else if (value > BaseAmountConst::cMaxValue)
    {
        while (value > (10 * BaseAmountConst::cMaxValue))
        {
            value /= 10;
            ++offset;
        }

        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

inline LocalValue<bool>&
getStaticSTAmountCanonicalizeSwitchover1()
{
    static LocalValue<bool> r{true};
    return r;
}

}  // namespace detail

inline bool
getSTAmountCanonicalizeSwitchover1()
{
    return *getStaticSTAmountCanonicalizeSwitchover1();
}

inline void
setSTAmountCanonicalizeSwitchover1(bool v)
{
    *getStaticSTAmountCanonicalizeSwitchover1() = v;
}

void
canonicalizeRoundStrict(
    bool nativeOrMPT,
    std::uint64_t& value,
    int& offset,
    bool roundUp)
{
    if (nativeOrMPT)
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
    else if (value > BaseAmountConst::cMaxValue)
    {
        while (value > (10 * BaseAmountConst::cMaxValue))
        {
            value /= 10;
            ++offset;
        }
        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

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

inline static std::uint64_t
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

inline static std::uint64_t
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

// Pass the canonicalizeRound function pointer as a template parameter.
//
// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <
    void (*CanonicalizeFunc)(bool, std::uint64_t&, int&, bool),
    typename MightSaveRound,
    ValidAsset T>
inline static BaseAmount<T>
mulRoundImpl(
    BaseAmount<T> const& v1,
    BaseAmount<T> const& v2,
    T const& asset,
    bool roundUp)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return {asset};

    bool const xrp = isNative(asset);

    // TODO MPT
    if (isNative(v1) && isNative(v2) && isNative(asset))
    {
        std::uint64_t minV =
            (getSNValue(v1) < getSNValue(v2)) ? getSNValue(v1) : getSNValue(v2);
        std::uint64_t maxV =
            (getSNValue(v1) < getSNValue(v2)) ? getSNValue(v2) : getSNValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return BaseAmount<T>(minV * maxV);
    }
    // TODO MPT
    if (isMPT(v1) && isMPT(v2) && isMPT(asset))
    {
        std::uint64_t minV = (getMPTValue(v1) < getMPTValue(v2))
            ? getMPTValue(v1)
            : getMPTValue(v2);
        std::uint64_t maxV = (getMPTValue(v1) < getMPTValue(v2))
            ? getMPTValue(v2)
            : getMPTValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Asset value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Asset value overflow");

        return BaseAmount<T>(asset, minV * maxV);
    }

    std::uint64_t value1 = v1.mantissa(), value2 = v2.mantissa();
    int offset1 = v1.exponent(), offset2 = v2.exponent();

    // TODO MPT
    if (isNative(v1) || isMPT(v1))
    {
        while (value1 < BaseAmountConst::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    // TODO MPT
    if (isNative(v2) || isMPT(v2))
    {
        while (value2 < BaseAmountConst::cMinValue)
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
        BaseAmountConst::tenTo14,
        (resultNegative != roundUp) ? BaseAmountConst::tenTo14m1 : 0);

    int offset = offset1 + offset2 + 14;
    if (resultNegative != roundUp)
    {
        CanonicalizeFunc(xrp, amount, offset, roundUp);
    }
    BaseAmount<T> result = [&]() {
        // If appropriate, tell Number to round down.  This gives the desired
        // result from STAmount::canonicalize.
        MightSaveRound const savedRound(Number::towards_zero);
        return BaseAmount<T>(asset, amount, offset, resultNegative);
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
            amount = BaseAmountConst::cMinValue;
            offset = BaseAmountConst::cMinOffset;
        }
        return BaseAmount<T>(asset, amount, offset, resultNegative);
    }
    return result;
}

// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <typename MightSaveRound, ValidAsset T>
static BaseAmount<T>
divRoundImpl(
    BaseAmount<T> const& num,
    BaseAmount<T> const& den,
    Asset const& asset,
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
        while (numVal < BaseAmountConst::cMinValue)
        {
            numVal *= 10;
            --numOffset;
        }
    }

    // TODO MPT
    if (isNative(den) || isMPT(den))
    {
        while (denVal < BaseAmountConst::cMinValue)
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
        BaseAmountConst::tenTo17,
        denVal,
        (resultNegative != roundUp) ? denVal - 1 : 0);

    int offset = numOffset - denOffset - 17;

    // TODO MPT
    if (resultNegative != roundUp)
        canonicalizeRound(
            isNative(asset) || isMPT(asset), amount, offset, roundUp);

    BaseAmount<T> result = [&]() {
        // If appropriate, tell Number the rounding mode we are using.
        // Note that "roundUp == true" actually means "round away from zero".
        // Otherwise round toward zero.
        using enum Number::rounding_mode;
        MightSaveRound const savedRound(
            roundUp ^ resultNegative ? upward : downward);
        return BaseAmount<T>(asset, amount, offset, resultNegative);
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
            amount = BaseAmountConst::cMinValue;
            offset = BaseAmountConst::cMinOffset;
        }
        return BaseAmount<T>(asset, amount, offset, resultNegative);
    }
    return result;
}

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

template <ValidAsset T1, ValidAsset T2, ValidAsset TA>
BaseAmount<TA>
divide(BaseAmount<T1> const& num, BaseAmount<T2> const& den, TA const& asset)
{
    if (den == beast::zero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::zero)
        return {asset};

    std::uint64_t numVal = num.mantissa();
    std::uint64_t denVal = den.mantissa();
    int numOffset = num.exponent();
    int denOffset = den.exponent();

    if (isNative(num) || isMPT(num))
    {
        while (numVal < BaseAmountConst::cMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }
    }

    if (isNative(den) || isMPT(den))
    {
        while (denVal < BaseAmountConst::cMinValue)
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
    return BaseAmount<TA>(
        asset,
        detail::muldiv(numVal, BaseAmountConst::tenTo17, denVal) + 5,
        numOffset - denOffset - 17,
        num.negative() != den.negative());
}

template <ValidAsset T1, ValidAsset T2, ValidAsset TA>
BaseAmount<TA>
multiply(BaseAmount<T1> const& v1, BaseAmount<T2> const& v2, TA const& asset)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return BaseAmount<TA>(asset);

    if (isNative(v1) && isNative(v2) && isNative(asset))
    {
        std::uint64_t const minV =
            getSNValue(v1) < getSNValue(v2) ? getSNValue(v1) : getSNValue(v2);
        std::uint64_t const maxV =
            getSNValue(v1) < getSNValue(v2) ? getSNValue(v2) : getSNValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return BaseAmount<TA>(v1.getFName(), minV * maxV);
    }
    if (isMPT(v1) && isMPT(v2) && isMPT(asset))
    {
        std::uint64_t const minV = getMPTValue(v1) < getMPTValue(v2)
            ? getMPTValue(v1)
            : getMPTValue(v2);
        std::uint64_t const maxV = getMPTValue(v1) < getMPTValue(v2)
            ? getMPTValue(v2)
            : getMPTValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Asset value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Asset value overflow");

        return BaseAmount<TA>(asset, minV * maxV);
    }

    if (getSTNumberSwitchover())
        return {IOUAmount{Number{v1} * Number{v2}}, asset};

    std::uint64_t value1 = v1.mantissa();
    std::uint64_t value2 = v2.mantissa();
    int offset1 = v1.exponent();
    int offset2 = v2.exponent();

    // TODO MPT
    if (isNative(v1) || isMPT(v1))
    {
        while (value1 < BaseAmountConst::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (isNative(v2) || isMPT(v2))
    {
        while (value2 < BaseAmountConst::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    return BaseAmount<TA>(
        asset,
        detail::muldiv(value1, value2, BaseAmountConst::tenTo14) + 7,
        offset1 + offset2 + 14,
        v1.negative() != v2.negative());
}

// multiply rounding result in specified direction
template <ValidAsset T>
BaseAmount<T>
mulRound(
    BaseAmount<T> const& v1,
    BaseAmount<T> const& v2,
    T const& asset,
    bool roundUp)
{
    return detail::mulRoundImpl<
        detail::canonicalizeRound,
        detail::DontAffectNumberRoundMode,
        T>(v1, v2, asset, roundUp);
}

// multiply following the rounding directions more precisely.
template <ValidAsset T>
BaseAmount<T>
mulRoundStrict(
    BaseAmount<T> const& v1,
    BaseAmount<T> const& v2,
    T const& asset,
    bool roundUp)
{
    return detail::
        mulRoundImpl<detail::canonicalizeRoundStrict, NumberRoundModeGuard, T>(
            v1, v2, asset, roundUp);
}

// divide rounding result in specified direction
template <ValidAsset T>
BaseAmount<T>
divRound(
    BaseAmount<T> const& num,
    BaseAmount<T> const& den,
    T const& asset,
    bool roundUp)
{
    return divRoundImpl<detail::DontAffectNumberRoundMode, T>(
        num, den, asset, roundUp);
}

// divide following the rounding directions more precisely.
template <ValidAsset T>
BaseAmount<T>
divRoundStrict(
    BaseAmount<T> const& num,
    BaseAmount<T> const& den,
    T const& asset,
    bool roundUp)
{
    return divRoundImpl<NumberRoundModeGuard, T>(num, den, asset, roundUp);
}

// Someone is offering X for Y, what is the rate?
// Rate: smaller is better, the taker wants the most out: in/out
// Convert an offer into an index amount so they sort by rate.
// A taker will take the best, lowest, rate first.
// (e.g. a taker will prefer pay 1 get 3 over pay 1 get 2.
// --> offerOut: takerGets: How much the offerer is selling to the taker.
// -->  offerIn: takerPays: How much the offerer is receiving from the taker.
// <--    uRate: normalize(offerIn/offerOut)
//             A lower rate is better for the person taking the order.
//             The taker gets more for less with a lower rate.
// Zero is returned if the offer is worthless.
template <ValidAsset T1, ValidAsset T2>
std::uint64_t
getRate(BaseAmount<T1> const& offerOut, BaseAmount<T2> const& offerIn)
{
    if (offerOut == beast::zero)
        return 0;
    try
    {
        auto const r = divide(offerIn, offerOut, noIssue());
        if (r == beast::zero)  // offer is too good
            return 0;
        assert((r.exponent() >= -100) && (r.exponent() <= 155));
        std::uint64_t ret = r.exponent() + 100;
        return (ret << (64 - 8)) | r.mantissa();
    }
    catch (std::exception const&)
    {
    }

    // overflow -- very bad offer
    return 0;
}

template <ValidAsset T>
std::uint64_t const BaseAmount<T>::uRateOne = getRate(
    BaseAmount<T>(static_cast<std::uint64_t>(1)),
    BaseAmount<T>(static_cast<std::uint64_t>(1)));

template <ValidAsset T>
void
BaseAmount<T>::setAsset(T const& asset, bool native)
{
    if (native)
        mAsset = xrpIssue();
    else
        mAsset = asset;
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(
    T const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : mValue(mantissa)
    , mOffset(exponent)
    , mIsNative(native)
    , mIsNegative(negative)
{
    setAsset(asset, native);
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(
    T const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative)
    : mValue(mantissa)
    , mOffset(exponent)
    , mIsNative(native)
    , mIsNegative(negative)
{
    setAsset(asset, native);
    canonicalize();
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(
    T const& asset,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : mAsset(asset), mValue(mantissa), mOffset(exponent), mIsNegative(negative)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
    canonicalize();
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(T const& asset, std::int64_t mantissa, int exponent)
    : mAsset(asset), mOffset(exponent)
{
    set(mantissa);
    canonicalize();
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(
    T const& asset,
    std::uint32_t mantissa,
    int exponent,
    bool negative)
    : BaseAmount<T>(
          asset,
          safe_cast<std::uint64_t>(mantissa),
          exponent,
          negative)
{
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(T const& asset, int mantissa, int exponent)
    : BaseAmount<T>(asset, safe_cast<std::int64_t>(mantissa), exponent)
{
}

// Legacy support for new-style amounts
template <ValidAsset T>
BaseAmount<T>::BaseAmount(IOUAmount const& amount, Issue const& issue)
    : mAsset(issue)
    , mOffset(amount.exponent())
    , mIsNative(false)
    , mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = static_cast<std::uint64_t>(-amount.mantissa());
    else
        mValue = static_cast<std::uint64_t>(amount.mantissa());

    canonicalize();
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(MPTAmount const& amount, MPTIssue const& issue)
    : mAsset(issue)
    , mOffset(0)
    , mIsNative(false)
    , mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = unsafe_cast<std::uint64_t>(-amount.mpt());
    else
        mValue = unsafe_cast<std::uint64_t>(amount.mpt());

    canonicalize();
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(std::int64_t mantissa)
    : mAsset(xrpIssue()), mOffset(0), mIsNative(true)
{
    set(mantissa);
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(std::uint64_t mantissa, bool negative)
    : mAsset(xrpIssue())
    , mValue(mantissa)
    , mOffset(0)
    , mIsNative(true)
    , mIsNegative(negative)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(BaseAmount<T> const& from)
    : mAsset(from.mAsset)
    , mValue(from.mValue)
    , mOffset(from.mOffset)
    , mIsNegative(from.mIsNegative)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
    canonicalize();
}

template <ValidAsset T>
BaseAmount<T>::BaseAmount(XRPAmount const& amount)
    : mAsset(xrpIssue())
    , mOffset(0)
    , mIsNative(true)
    , mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = unsafe_cast<std::uint64_t>(-amount.drops());
    else
        mValue = unsafe_cast<std::uint64_t>(amount.drops());

    canonicalize();
}

template <ValidAsset T>
BaseAmount<T>&
BaseAmount<T>::operator=(BaseAmount<T> const& amount)
{
    mAsset = amount.mAsset;
    mValue = amount.mValue;
    mOffset = amount.mOffset;
    mIsNegative = amount.mIsNegative;
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
    canonicalize();
    return *this;
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

template <ValidAsset T>
int
BaseAmount<T>::exponent() const noexcept
{
    return mOffset;
}

template <ValidAsset T>
bool
BaseAmount<T>::negative() const noexcept
{
    return mIsNegative;
}

template <ValidAsset T>
std::uint64_t
BaseAmount<T>::mantissa() const noexcept
{
    return mValue;
}

template <ValidAsset T>
T const&
BaseAmount<T>::asset() const
{
    return mAsset;
}

template <ValidAsset T>
AccountID const&
BaseAmount<T>::getIssuer() const
{
    return mAsset.getIssuer();
}

template <ValidAsset T>
int
BaseAmount<T>::signum() const noexcept
{
    return mValue ? (mIsNegative ? -1 : 1) : 0;
}

template <ValidAsset T>
BaseAmount<T>::operator bool() const noexcept
{
    return *this != beast::zero;
}

template <ValidAsset T>
BaseAmount<T>&
BaseAmount<T>::operator=(beast::Zero)
{
    clear();
    return *this;
}

template <ValidAsset T>
BaseAmount<T>&
BaseAmount<T>::operator=(XRPAmount const& amount)
{
    *this = BaseAmount<T>(amount);
    return *this;
}

template <ValidAsset T>
BaseAmount<T>&
BaseAmount<T>::operator=(IOUAmount const& iou)
{
    assert(mIsNative == false);
    mOffset = iou.exponent();
    mIsNegative = iou < beast::zero;
    if (mIsNegative)
        mValue = static_cast<std::uint64_t>(-iou.mantissa());
    else
        mValue = static_cast<std::uint64_t>(iou.mantissa());
    return *this;
}

template <ValidAsset T>
BaseAmount<T>&
BaseAmount<T>::operator+=(BaseAmount<T> const& a)
{
    *this = *this + a;
    return *this;
}

template <ValidAsset T>
BaseAmount<T>&
BaseAmount<T>::operator-=(BaseAmount<T> const& a)
{
    *this = *this - a;
    return *this;
}

template <ValidAsset T>
void
BaseAmount<T>::negate()
{
    if (*this != beast::zero)
        mIsNegative = !mIsNegative;
}

template <ValidAsset T>
void
BaseAmount<T>::clear()
{
    // The -100 is used to allow 0 to sort less than a small positive values
    // which have a negative exponent.
    mOffset = mIsNative ? 0 : -100;
    mValue = 0;
    mIsNegative = false;
}

// Zero while copying currency and issuer.
template <ValidAsset T>
void
BaseAmount<T>::clear(BaseAmount<T> const& amnt)
{
    clear(amnt.asset());
}

template <ValidAsset T>
void
BaseAmount<T>::clear(T const& asset)
{
    setAsset(asset, isNative(asset));
    clear();
}

template <ValidAsset T>
BaseAmount<T> const&
BaseAmount<T>::value() const noexcept
{
    return *this;
}

// amount = mValue * [10 ^ mOffset]
// Representation range is 10^80 - 10^(-80).
//
// On the wire:
// - high bit is 0 for XRP, 1 for issued currency
// - next bit is 1 for positive, 0 for negative (except 0 issued currency, which
//      is a special case of 0x8000000000000000
// - for issued currencies, the next 8 bits are (mOffset+97).
//   The +97 is so that this value is always positive.
// - The remaining bits are significant digits (mantissa)
//   That's 54 bits for issued currency and 62 bits for native
//   (but XRP only needs 57 bits for the max value of 10^17 drops)
//
// mValue is zero if the amount is zero, otherwise it's within the range
//    10^15 to (10^16 - 1) inclusive.
// mOffset is in the range -96 to +80.
template <ValidAsset T>
void
BaseAmount<T>::canonicalize()
{
    if (isXRP(*this) || isMPT(mAsset))
    {
        // native currency amounts should always have an offset of zero
        mIsNative = isXRP(*this);

        // log(2^64,10) ~ 19.2
        if (mValue == 0 || mOffset <= -20)
        {
            mValue = 0;
            mOffset = 0;
            mIsNegative = false;
            return;
        }

        if (getSTAmountCanonicalizeSwitchover1())
        {
            // log(cMaxNativeN, 10) == 17
            if (mOffset > 17)
                Throw<std::runtime_error>(
                    "Native currency amount out of range");
        }

        if (getSTNumberSwitchover() && getSTAmountCanonicalizeSwitchover1())
        {
            Number num(
                mIsNegative ? -mValue : mValue, mOffset, Number::unchecked{});
            if (mIsNative)
            {
                XRPAmount xrp{num};
                mIsNegative = xrp.drops() < 0;
                mValue = mIsNegative ? -xrp.drops() : xrp.drops();
            }
            else
            {
                MPTAmount c{num};
                mIsNegative = c.mpt() < 0;
                mValue = mIsNegative ? -c.mpt() : c.mpt();
            }
            mOffset = 0;
        }
        else
        {
            while (mOffset < 0)
            {
                mValue /= 10;
                ++mOffset;
            }

            while (mOffset > 0)
            {
                if (getSTAmountCanonicalizeSwitchover1())
                {
                    // N.B. do not move the overflow check to after the
                    // multiplication
                    if (mValue > BaseAmountConst::cMaxNativeN)
                        Throw<std::runtime_error>(
                            "Native currency amount out of range");
                }
                mValue *= 10;
                --mOffset;
            }
        }

        if (mValue > BaseAmountConst::cMaxNativeN)
            Throw<std::runtime_error>("Native currency amount out of range");

        return;
    }

    mIsNative = false;

    if (getSTNumberSwitchover())
    {
        if (mIsNative || isMPT(mAsset))
            Throw<std::logic_error>(
                "Native/MPT can not be canonicalized as IOU");

        mValue = static_cast<std::int64_t>(mValue);

        if (mIsNegative)
            mValue = -mValue;
        return;
    }

    if (mValue == 0)
    {
        mOffset = -100;
        mIsNegative = false;
        return;
    }

    while ((mValue < BaseAmountConst::cMinValue) &&
           (mOffset > BaseAmountConst::cMinOffset))
    {
        mValue *= 10;
        --mOffset;
    }

    while (mValue > BaseAmountConst::cMaxValue)
    {
        if (mOffset >= BaseAmountConst::cMaxOffset)
            Throw<std::runtime_error>("value overflow");

        mValue /= 10;
        ++mOffset;
    }

    if ((mOffset < BaseAmountConst::cMinOffset) ||
        (mValue < BaseAmountConst::cMinValue))
    {
        mValue = 0;
        mIsNegative = false;
        mOffset = -100;
        return;
    }

    if (mOffset > BaseAmountConst::cMaxOffset)
        Throw<std::runtime_error>("value overflow");

    assert(
        (mValue == 0) ||
        ((mValue >= BaseAmountConst::cMinValue) &&
         (mValue <= BaseAmountConst::cMaxValue)));
    assert(
        (mValue == 0) ||
        ((mOffset >= BaseAmountConst::cMinOffset) &&
         (mOffset <= BaseAmountConst::cMaxOffset)));
    assert((mValue != 0) || (mOffset != -100));
}

template <ValidAsset T>
void
BaseAmount<T>::set(std::int64_t v)
{
    if (v < 0)
    {
        mIsNegative = true;
        mValue = static_cast<std::uint64_t>(-v);
    }
    else
    {
        mIsNegative = false;
        mValue = static_cast<std::uint64_t>(v);
    }
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

template <ValidAsset T>
bool
operator==(BaseAmount<T> const& lhs, BaseAmount<T> const& rhs)
{
    return areComparable(lhs, rhs) && lhs.negative() == rhs.negative() &&
        lhs.exponent() == rhs.exponent() && lhs.mantissa() == rhs.mantissa();
}

template <ValidAsset T>
bool
operator<(BaseAmount<T> const& lhs, BaseAmount<T> const& rhs)
{
    if (!areComparable(lhs, rhs))
        Throw<std::runtime_error>(
            "Can't compare amounts that are't comparable!");

    if (lhs.negative() != rhs.negative())
        return lhs.negative();

    if (lhs.mantissa() == 0)
    {
        if (rhs.negative())
            return false;
        return rhs.mantissa() != 0;
    }

    // We know that lhs is non-zero and both sides have the same sign. Since
    // rhs is zero (and thus not negative), lhs must, therefore, be strictly
    // greater than zero. So if rhs is zero, the comparison must be false.
    if (rhs.mantissa() == 0)
        return false;

    if (lhs.exponent() > rhs.exponent())
        return lhs.negative();
    if (lhs.exponent() < rhs.exponent())
        return !lhs.negative();
    if (lhs.mantissa() > rhs.mantissa())
        return lhs.negative();
    if (lhs.mantissa() < rhs.mantissa())
        return !lhs.negative();

    return false;
}

template <ValidAsset T>
bool
operator!=(BaseAmount<T> const& lhs, BaseAmount<T> const& rhs)
{
    return !(lhs == rhs);
}

template <ValidAsset T>
bool
operator>(BaseAmount<T> const& lhs, BaseAmount<T> const& rhs)
{
    return rhs < lhs;
}

template <ValidAsset T>
bool
operator<=(BaseAmount<T> const& lhs, BaseAmount<T> const& rhs)
{
    return !(rhs < lhs);
}

template <ValidAsset T>
bool
operator>=(BaseAmount<T> const& lhs, BaseAmount<T> const& rhs)
{
    return !(lhs < rhs);
}

template <ValidAsset T>
BaseAmount<T>
operator-(BaseAmount<T> const& value)
{
    if (value.mantissa() == 0)
        return value;
    return BaseAmount<T>(
        value.asset(),
        value.mantissa(),
        value.exponent(),
        isNative(value),
        !value.negative(),
        typename BaseAmount<T>::unchecked());
}

}  // namespace ripple

#endif
