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
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/json_get_or_throw.h>

namespace ripple {

template <typename A>
concept AssetType = std::is_same_v<A, Issue> || std::is_same_v<A, MPT> ||
    std::is_same_v<A, Asset> || std::is_convertible_v<A, Issue> ||
    std::is_convertible_v<A, Asset>;

// Internal form:
// 1: If amount is zero, then value is zero and offset is -100
// 2: Otherwise:
//   legal offset range is -96 to +80 inclusive
//   value range is 10^15 to (10^16 - 1) inclusive
//  amount = value * [10 ^ offset]

// Wire form:
// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive
template <typename TIss>
class TSTAmount final : public STBase, public CountedObject<TSTAmount<TIss>>
{
public:
    using mantissa_type = std::uint64_t;
    using exponent_type = int;
    using rep = std::pair<mantissa_type, exponent_type>;

private:
    TIss mAsset;
    mantissa_type mValue;
    exponent_type mOffset;
    bool mIsNative;  // A shorthand for isXRP(mIssue).
    bool mIsNegative;

public:
    using value_type = TSTAmount<TIss>;

    static const int cMinOffset = -96;
    static const int cMaxOffset = 80;

    // Maximum native value supported by the code
    static const std::uint64_t cMinValue = 1000000000000000ull;
    static const std::uint64_t cMaxValue = 9999999999999999ull;
    static const std::uint64_t cMaxNative = 9000000000000000000ull;

    // Max native value on network.
    static const std::uint64_t cMaxNativeN = 100000000000000000ull;
    static const std::uint64_t cIssuedCurrency = 0x8000000000000000ull;
    static const std::uint64_t cPositive = 0x4000000000000000ull;
    static const std::uint64_t cMPToken = 0x2000000000000000ull;
    static const std::uint64_t cValueMask = ~(cPositive | cMPToken);

    static std::uint64_t const uRateOne;

    //--------------------------------------------------------------------------
    TSTAmount(SerialIter& sit, SField const& name);

    struct unchecked
    {
        explicit unchecked() = default;
    };

    // Do not call canonicalize
    template <AssetType A>
    TSTAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative,
        unchecked);

    template <AssetType A>
    TSTAmount(
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative,
        unchecked);

    // Call canonicalize
    template <AssetType A>
    TSTAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative);

    TSTAmount(SField const& name, std::int64_t mantissa);

    TSTAmount(
        SField const& name,
        std::uint64_t mantissa = 0,
        bool negative = false);

    template <AssetType A>
    TSTAmount(
        SField const& name,
        A const& asset,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false);

    explicit TSTAmount(std::uint64_t mantissa = 0, bool negative = false);

    explicit TSTAmount(SField const& name, TSTAmount const& amt);

    template <AssetType A>
    TSTAmount(
        A const& asset,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false)
        : mAsset(asset)
        , mValue(mantissa)
        , mOffset(exponent)
        , mIsNegative(negative)
    {
        canonicalize();
    }

    // VFALCO Is this needed when we have the previous signature?
    template <AssetType A>
    TSTAmount(
        A const& asset,
        std::uint32_t mantissa,
        int exponent = 0,
        bool negative = false);

    template <AssetType A>
    TSTAmount(A const& asset, std::int64_t mantissa, int exponent = 0);

    template <AssetType A>
    TSTAmount(A const& asset, int mantissa, int exponent = 0);

    // Legacy support for new-style amounts
    template <AssetType A>
    TSTAmount(IOUAmount const& amount, A const& asset);
    TSTAmount(XRPAmount const& amount);
    template <AssetType A>
    TSTAmount(MPTAmount const& amount, A const& asset);
    operator Number() const;

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    int
    exponent() const noexcept;

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

    bool
    negative() const noexcept;

    std::uint64_t
    mantissa() const noexcept;

    Asset const&
    asset() const;

    Issue const&
    issue() const;

    MPTIssue const&
    mptIssue() const;

    // These three are deprecated
    Currency const&
    getCurrency() const;

    AccountID const&
    getIssuer() const;

    int
    signum() const noexcept;

    /** Returns a zero value with the same issuer and currency. */
    TSTAmount
    zeroed() const;

    void
    setJson(Json::Value&) const;

    TSTAmount const&
    value() const noexcept;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    explicit operator bool() const noexcept;

    TSTAmount&
    operator+=(TSTAmount const&);
    TSTAmount&
    operator-=(TSTAmount const&);

    TSTAmount& operator=(beast::Zero);

    TSTAmount&
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
    clear(TSTAmount const& saTmpl);

    void
    clear(Issue const& issue);

    void
    clear(MPT const& mpt);

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

    template <AssetType A>
    void
    setAsset(A const& a, bool native);

private:
    static std::unique_ptr<TSTAmount>
    construct(SerialIter&, SField const& name);

    void
    set(std::int64_t v);
    void
    canonicalize();

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    TSTAmount&
    operator=(IOUAmount const& iou);

    friend class detail::STVar;

    friend TSTAmount
    operator+(TSTAmount const& v1, TSTAmount const& v2);
};

using STAmount = TSTAmount<Asset>;

template <typename TIss>
template <AssetType A>
void
TSTAmount<TIss>::setAsset(const A& asset, bool native)
{
    if (native)
        mAsset = xrpIssue();
    else
        mAsset = asset;
}

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(
    SField const& name,
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : STBase(name)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNative(native)
    , mIsNegative(negative)
{
    setAsset(asset, native);
}

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(
    A const& asset,
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

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(
    SField const& name,
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative)
    : STBase(name)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNative(native)
    , mIsNegative(negative)
{
    setAsset(asset, native);
    canonicalize();
}

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(
    SField const& name,
    A const& asset,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : STBase(name)
    , mAsset(asset)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNegative(negative)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
    canonicalize();
}

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(A const& asset, std::int64_t mantissa, int exponent)
    : mAsset(asset), mOffset(exponent)
{
    set(mantissa);
    canonicalize();
}

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(
    A const& asset,
    std::uint32_t mantissa,
    int exponent,
    bool negative)
    : TSTAmount(asset, safe_cast<std::uint64_t>(mantissa), exponent, negative)
{
}

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(A const& asset, int mantissa, int exponent)
    : TSTAmount(asset, safe_cast<std::int64_t>(mantissa), exponent)
{
}

// Legacy support for new-style amounts
template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(IOUAmount const& amount, A const& asset)
    : mAsset(asset)
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

template <typename TIss>
template <AssetType A>
TSTAmount<TIss>::TSTAmount(MPTAmount const& amount, A const& asset)
    : mAsset(asset)
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

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

template <typename TIss>
int
TSTAmount<TIss>::exponent() const noexcept
{
    return mOffset;
}

template <typename TIss>
bool
TSTAmount<TIss>::native() const noexcept
{
    return mIsNative;
}

template <typename TIss>
bool
TSTAmount<TIss>::isMPT() const noexcept
{
    return mAsset.isMPT();
}

template <typename TIss>
bool
TSTAmount<TIss>::isIssue() const noexcept
{
    return mAsset.isIssue();
}

template <typename TIss>
bool
TSTAmount<TIss>::isIOU() const noexcept
{
    return mAsset.isIssue() && !mIsNative;
}

template <typename TIss>
bool
TSTAmount<TIss>::negative() const noexcept
{
    return mIsNegative;
}

template <typename TIss>
std::uint64_t
TSTAmount<TIss>::mantissa() const noexcept
{
    return mValue;
}

template <typename TIss>
Asset const&
TSTAmount<TIss>::asset() const
{
    return mAsset;
}

template <typename TIss>
Issue const&
TSTAmount<TIss>::issue() const
{
    return mAsset.issue();
}

template <typename TIss>
MPTIssue const&
TSTAmount<TIss>::mptIssue() const
{
    return mAsset.mptIssue();
}

template <typename TIss>
Currency const&
TSTAmount<TIss>::getCurrency() const
{
    return mAsset.issue().currency;
}

template <typename TIss>
AccountID const&
TSTAmount<TIss>::getIssuer() const
{
    return mAsset.issue().account;
}

template <typename TIss>
int
TSTAmount<TIss>::signum() const noexcept
{
    return mValue ? (mIsNegative ? -1 : 1) : 0;
}

template <typename TIss>
TSTAmount<TIss>
TSTAmount<TIss>::zeroed() const
{
    if (mAsset.isIssue())
        return TSTAmount(mAsset.issue());
    return TSTAmount(mAsset.mptIssue());
}

template <typename TIss>
TSTAmount<TIss>::operator bool() const noexcept
{
    return *this != beast::zero;
}

template <typename TIss>
TSTAmount<TIss>::operator Number() const
{
    if (mIsNative)
        return xrp();
    if (mAsset.isMPT())
        return mpt();
    return iou();
}

template <typename TIss>
TSTAmount<TIss>&
TSTAmount<TIss>::operator=(beast::Zero)
{
    clear();
    return *this;
}

template <typename TIss>
TSTAmount<TIss>&
TSTAmount<TIss>::operator=(XRPAmount const& amount)
{
    *this = TSTAmount<TIss>(amount);
    return *this;
}

template <typename TIss>
void
TSTAmount<TIss>::negate()
{
    if (*this != beast::zero)
        mIsNegative = !mIsNegative;
}

template <typename TIss>
void
TSTAmount<TIss>::clear()
{
    // The -100 is used to allow 0 to sort less than a small positive values
    // which have a negative exponent.
    mOffset = mIsNative ? 0 : -100;
    mValue = 0;
    mIsNegative = false;
}

// Zero while copying currency and issuer.
template <typename TIss>
void
TSTAmount<TIss>::clear(TSTAmount<TIss> const& saTmpl)
{
    if (saTmpl.isMPT())
        clear(saTmpl.mAsset.mptIssue());
    else
        clear(saTmpl.issue());
}

template <typename TIss>
void
TSTAmount<TIss>::clear(Issue const& issue)
{
    setIssue(issue);
    clear();
}

template <typename TIss>
void
TSTAmount<TIss>::clear(MPT const& mpt)
{
    mAsset = mpt;
    clear();
}

template <typename TIss>
void
TSTAmount<TIss>::setIssuer(AccountID const& uIssuer)
{
    mAsset.issue().account = uIssuer;
    setIssue(mAsset.issue());
}

template <typename TIss>
TSTAmount<TIss> const&
TSTAmount<TIss>::value() const noexcept
{
    return *this;
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

// Since `canonicalize` does not have access to a ledger, this is needed to put
// the low-level routine stAmountCanonicalize on an amendment switch. Only
// transactions need to use this switchover. Outside of a transaction it's safe
// to unconditionally use the new behavior.

bool
getSTAmountCanonicalizeSwitchover();

void
setSTAmountCanonicalizeSwitchover(bool v);

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
