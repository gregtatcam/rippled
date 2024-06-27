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

#ifndef RIPPLE_PROTOCOL_STAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_STAMOUNT_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/LocalValue.h>
#include <ripple/basics/MPTAmount.h>
#include <ripple/basics/Number.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/Asset.h>
#include <ripple/protocol/AssetAmount.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/json_get_or_throw.h>

namespace ripple {

// Internal form:
// 1: If amount is zero, then value is zero and offset is -100
// 2: Otherwise:
//   legal offset range is -96 to +80 inclusive
//   value range is 10^15 to (10^16 - 1) inclusive
//  amount = value * [10 ^ offset]

// Wire form:
// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive
class STAmount final : public AssetAmount<Asset>,
                       public STBase,
                       public CountedObject<STAmount>
{
public:
    using value_type = STAmount;

    static std::uint64_t const uRateOne;

    //--------------------------------------------------------------------------
    STAmount(SerialIter& sit, SField const& name);

    // Do not call canonicalize
    template <ValidAssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative,
        unchecked);

    template <ValidAssetType A>
    STAmount(
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative,
        unchecked);

    // Call canonicalize
    template <ValidAssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative);

    STAmount(SField const& name, std::int64_t mantissa);

    STAmount(
        SField const& name,
        std::uint64_t mantissa = 0,
        bool negative = false);

    template <ValidAssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false);

    explicit STAmount(std::uint64_t mantissa = 0, bool negative = false);

    explicit STAmount(SField const& name, STAmount const& amt);

    template <ValidAssetType A>
    STAmount(
        A const& asset,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false)
        : AssetAmount<Asset>(asset, mantissa, exponent, negative)
    {
    }

    // VFALCO Is this needed when we have the previous signature?
    template <ValidAssetType A>
    STAmount(
        A const& asset,
        std::uint32_t mantissa,
        int exponent = 0,
        bool negative = false);

    template <ValidAssetType A>
    STAmount(A const& asset, std::int64_t mantissa, int exponent = 0);

    template <ValidAssetType A>
    STAmount(A const& asset, int mantissa, int exponent = 0);

    STAmount(SField const& name, AssetAmount<Asset> const& amount);

    // Legacy support for new-style amounts
    STAmount(IOUAmount const& amount, Asset const& asset);
    STAmount(XRPAmount const& amount);
    STAmount(MPTAmount const& amount, Asset const& asset);
    operator Number() const;

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    bool
    native() const noexcept;

    bool
    isMPT() const noexcept;

    bool
    isIssue() const noexcept;

    bool
    isIOU() const noexcept;

    std::string
    getTypeName() const noexcept;

    Issue const&
    issue() const;

    MPTIssue const&
    mptIssue() const;

    // These three are deprecated
    Currency const&
    getCurrency() const;

    AccountID const&
    getIssuer() const;

    /** Returns a zero value with the same issuer and currency. */
    STAmount
    zeroed() const;

    void
    setJson(Json::Value&) const;

    STAmount const&
    value() const noexcept;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    STAmount&
    operator+=(STAmount const&);
    STAmount&
    operator-=(STAmount const&);

    STAmount& operator=(beast::Zero);

    STAmount&
    operator=(XRPAmount const& amount);

    explicit operator AssetAmount<Issue>() const;
    explicit operator AssetAmount<MPTIssue>() const;

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    void
    setIssuer(AccountID const& uIssuer);

    /** Set the Issue for this amount and update mIsNative. */
    void
    setIssue(Issue const& issue);

    //--------------------------------------------------------------------------
    //
    // STBase
    //
    //--------------------------------------------------------------------------

    SerializedTypeID
    getSType() const override;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    XRPAmount
    xrp() const;
    IOUAmount
    iou() const;
    MPTAmount
    mpt() const;

    template <ValidAssetType A>
    void
    setAsset(A const& a, bool native);

private:
    static std::unique_ptr<STAmount>
    construct(SerialIter&, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    STAmount&
    operator=(IOUAmount const& iou);

    friend class detail::STVar;

    friend STAmount
    operator+(STAmount const& v1, STAmount const& v2);
};

template <ValidAssetType A>
void
STAmount::setAsset(const A& asset, bool native)
{
    if (native)
        mAsset = xrpIssue();
    else
        mAsset = asset;
}

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

// VFALCO TODO The parameter type should be Quality not uint64_t
STAmount
amountFromQuality(std::uint64_t rate);

STAmount
amountFromString(Asset const& issue, std::string const& amount);

STAmount
amountFromJson(SField const& name, Json::Value const& v);

bool
amountFromJsonNoThrow(STAmount& result, Json::Value const& jvSource);

// IOUAmount and XRPAmount define toSTAmount, defining this
// trivial conversion here makes writing generic code easier
inline STAmount const&
toSTAmount(STAmount const& a)
{
    return a;
}

template <ValidAssetType A>
STAmount::STAmount(
    SField const& name,
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : AssetAmount<Asset>(asset, mantissa, exponent, negative, unchecked())
    , STBase(name)
{
    setAsset(asset, native);
}

template <ValidAssetType A>
STAmount::STAmount(
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : AssetAmount<Asset>(asset, mantissa, exponent, negative, unchecked())
{
    setAsset(asset, native);
}

template <ValidAssetType A>
STAmount::STAmount(
    SField const& name,
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative)
    : AssetAmount<Asset>(asset, mantissa, exponent, negative), STBase(name)
{
    setAsset(asset, native);
}

template <ValidAssetType A>
STAmount::STAmount(
    SField const& name,
    A const& asset,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : AssetAmount<Asset>(asset, mantissa, exponent, negative), STBase(name)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
}

template <ValidAssetType A>
STAmount::STAmount(A const& asset, std::int64_t mantissa, int exponent)
    : AssetAmount<Asset>(asset, mantissa, exponent, false, unchecked())
{
    set(mantissa);
    canonicalize();
}

template <ValidAssetType A>
STAmount::STAmount(
    A const& asset,
    std::uint32_t mantissa,
    int exponent,
    bool negative)
    : STAmount(asset, safe_cast<std::uint64_t>(mantissa), exponent, negative)
{
}

template <ValidAssetType A>
STAmount::STAmount(A const& asset, int mantissa, int exponent)
    : STAmount(asset, safe_cast<std::int64_t>(mantissa), exponent)
{
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

inline bool
STAmount::native() const noexcept
{
    return mIsNative;
}

inline bool
STAmount::isMPT() const noexcept
{
    return mAsset.isMPT();
}

inline bool
STAmount::isIssue() const noexcept
{
    return mAsset.isIssue();
}

inline bool
STAmount::isIOU() const noexcept
{
    return mAsset.isIssue() && !mIsNative;
}

inline Issue const&
STAmount::issue() const
{
    return mAsset.issue();
}

inline MPTIssue const&
STAmount::mptIssue() const
{
    return mAsset.mptIssue();
}

inline Currency const&
STAmount::getCurrency() const
{
    return mAsset.issue().currency;
}

inline AccountID const&
STAmount::getIssuer() const
{
    return mAsset.getIssuer();
}

inline STAmount
STAmount::zeroed() const
{
    if (mAsset.isIssue())
        return STAmount(mAsset.issue());
    return STAmount(mAsset.mptIssue());
}

inline STAmount::operator Number() const
{
    if (mIsNative)
        return xrp();
    if (mAsset.isMPT())
        return mpt();
    return iou();
}

inline STAmount& STAmount::operator=(beast::Zero)
{
    AssetAmount<Asset>::clear();
    return *this;
}

inline STAmount&
STAmount::operator=(XRPAmount const& amount)
{
    *this = STAmount(amount);
    return *this;
}

inline void
STAmount::setIssuer(AccountID const& uIssuer)
{
    mAsset.issue().account = uIssuer;
    setIssue(mAsset.issue());
}

inline STAmount const&
STAmount::value() const noexcept
{
    return *this;
}

inline STAmount::operator AssetAmount<Issue>() const
{
    if (!mAsset.isIssue())
        Throw<std::logic_error>("STAmount is not for Issue");
    return AssetAmount<Issue>(mAsset.issue(), mValue, mOffset, mIsNegative);
}

inline STAmount::operator AssetAmount<MPTIssue>() const
{
    if (!mAsset.isMPT())
        Throw<std::logic_error>("STAmount is not for MPTIssue");
    return AssetAmount<MPTIssue>(
        mAsset.mptIssue(), mValue, mOffset, mIsNegative);
}

inline bool
isLegalNet(STAmount const& value)
{
    return !value.native() || (value.mantissa() <= STAmount::cMaxNativeN);
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool
operator==(STAmount const& lhs, STAmount const& rhs);
bool
operator<(STAmount const& lhs, STAmount const& rhs);

inline bool
operator!=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs == rhs);
}

inline bool
operator>(STAmount const& lhs, STAmount const& rhs)
{
    return rhs < lhs;
}

inline bool
operator<=(STAmount const& lhs, STAmount const& rhs)
{
    return !(rhs < lhs);
}

inline bool
operator>=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs < rhs);
}

STAmount
operator-(STAmount const& value);

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

STAmount
operator+(STAmount const& v1, STAmount const& v2);
STAmount
operator-(STAmount const& v1, STAmount const& v2);

STAmount
divide(STAmount const& v1, STAmount const& v2, Asset const& asset);

STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset);

// multiply rounding result in specified direction
STAmount
mulRound(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp);

// multiply following the rounding directions more precisely.
STAmount
mulRoundStrict(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp);

// divide rounding result in specified direction
STAmount
divRound(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp);

// divide following the rounding directions more precisely.
STAmount
divRoundStrict(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp);

// Someone is offering X for Y, what is the rate?
// Rate: smaller is better, the taker wants the most out: in/out
// VFALCO TODO Return a Quality object
std::uint64_t
getRate(STAmount const& offerOut, STAmount const& offerIn);

//------------------------------------------------------------------------------

inline bool
isXRP(STAmount const& amount)
{
    return !amount.isMPT() && isXRP(amount.issue().currency);
}

inline bool
isMPT(STAmount const& amount)
{
    return amount.isMPT();
}

/** RAII class to set and restore the STAmount canonicalize switchover.
 */

class STAmountSO
{
public:
    explicit STAmountSO(bool v) : saved_(getSTAmountCanonicalizeSwitchover())
    {
        setSTAmountCanonicalizeSwitchover(v);
    }

    ~STAmountSO()
    {
        setSTAmountCanonicalizeSwitchover(saved_);
    }

private:
    bool saved_;
};

}  // namespace ripple

//------------------------------------------------------------------------------
namespace Json {
template <>
inline ripple::STAmount
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    Json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    Json::Value const& inner = v[key];
    return amountFromJson(field, inner);
}
}  // namespace Json
#endif
