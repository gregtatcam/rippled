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

#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <boost/algorithm/string.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/regex.hpp>
#include <iostream>
#include <iterator>
#include <memory>

namespace ripple {

namespace {

// Use a static inside a function to help prevent order-of-initialzation issues
LocalValue<bool>&
getStaticSTAmountCanonicalizeSwitchover()
{
    static LocalValue<bool> r{true};
    return r;
}
}  // namespace

bool
getSTAmountCanonicalizeSwitchover()
{
    return *getStaticSTAmountCanonicalizeSwitchover();
}

void
setSTAmountCanonicalizeSwitchover(bool v)
{
    *getStaticSTAmountCanonicalizeSwitchover() = v;
}

static const std::uint64_t tenTo14 = 100000000000000ull;
static const std::uint64_t tenTo14m1 = tenTo14 - 1;
static const std::uint64_t tenTo17 = tenTo14 * 1000;

//------------------------------------------------------------------------------
static std::int64_t
getSNValue(STAmount const& amount)
{
    if (!amount.native())
        Throw<std::runtime_error>("amount is not native!");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    assert(static_cast<std::uint64_t>(ret) == amount.mantissa());

    if (amount.negative())
        ret = -ret;

    return ret;
}

static std::int64_t
getCFTValue(STAmount const& amount)
{
    if (!amount.isCFT())
        Throw<std::runtime_error>("amount is not native!");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    assert(static_cast<std::uint64_t>(ret) == amount.mantissa());

    if (amount.negative())
        ret = -ret;

    return ret;
}

static bool
areComparable(STAmount const& v1, STAmount const& v2)
{
    return (v1.native() == v2.native() || v1.isCFT() == v2.isCFT()) &&
        v1.issue().currency == v2.issue().currency;
}

std::string
STAmount::getTypeName() const noexcept
{
    switch (mType)
    {
        case STAmount::Type::xrp:
            return "xrp";
        case STAmount::Type::issued_currency:
            return "issued_currency";
        case STAmount::Type::cft:
            return "cft";
        default:
            return "";
    }
}

STAmount::STAmount(SerialIter& sit, SField const& name) : STBase(name)
{
    std::uint64_t value = sit.get64();

    if ((value & cIssuedCurrency) == cIssuedCurrency)
    {
        mType = Type::issued_currency;

        // 10 bits for the offset, sign and "is IOU" flag
        int offset = static_cast<int>(value >> (64 - 10));

        auto const pValue = value & ~(1023ull << (64 - 10));

        if (pValue)
        {
            offset = (offset & 255) - 97;  // center the range

            if (pValue < cMinValue || pValue > cMaxValue ||
                offset < cMinOffset || offset > cMaxOffset)
                Throw<std::runtime_error>("invalid currency value");

            mValue = pValue;
            mOffset = offset;
            mIsNegative = (value & cSign) == 0;
        }
        else if (offset != 512)
        {
            Throw<std::runtime_error>("invalid currency value");
        }
        else
        {
            mValue = 0;
            mOffset = 0;
            mIsNegative = false;
        }

        Issue issue;
        issue.currency = sit.get160();

        if (isXRP(issue.currency))
            Throw<std::runtime_error>("invalid native currency");

        issue.account = sit.get160();

        mIssue = issue;
        // TODO....
        canonicalize();
        return;
    }

    if ((value & cCFToken) == cCFToken)
    {
        mType = Type::cft;

        // We're supporting this deserialization, but negative CFT values are
        // not currently supported by any application logic
        mIsNegative = (value & cSign) != cSign;
        mValue = value & cValueMask;
        mOffset = 0;

        if (mIsNegative && mValue == 0)
            Throw<std::runtime_error>("Negative zero CFT amounts are illegal");

        Issue issue;
        issue.currency = sit.get160();
        issue.isCFT = true;

        if (isXRP(issue.currency))
            Throw<std::runtime_error>("invalid native currency");

        issue.account = sit.get160();

        mIssue = issue;
        // TODO....
        // canonicalize();
        return;
    }

    // else it must be native
    mType = Type::xrp;

    // positive
    if (value & cSign)
    {
        mValue = value & cValueMask;
        mOffset = 0;
        mType = Type::xrp;
        mIsNegative = false;
        return;
    }

    // negative
    if (value == 0)
        Throw<std::runtime_error>("negative zero is not canonical");

    mValue = value;
    mOffset = 0;
    mIsNegative = true;
}

STAmount::STAmount(
    SField const& name,
    Issue const& issue,
    mantissa_type mantissa,
    exponent_type exponent,
    STAmount::Type typ,
    bool negative)
    : STBase(name)
    , mIssue(issue)
    , mValue(mantissa)
    , mOffset(exponent)
    , mType(typ)
    , mIsNegative(negative)
{
}

STAmount::STAmount(
    SField const& name,
    Issue const& issue,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : STBase(name)
    , mIssue(issue)
    , mValue(mantissa)
    , mOffset(exponent)
    , mType(
          native ? Type::xrp
                 : (issue.isCFT ? Type::cft : Type::issued_currency))
    , mIsNegative(negative)
{
}

STAmount::STAmount(
    Issue const& issue,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : mIssue(issue)
    , mValue(mantissa)
    , mOffset(exponent)
    , mType(
          native ? Type::xrp
                 : (issue.isCFT ? Type::cft : Type::issued_currency))
    , mIsNegative(negative)
{
}

STAmount::STAmount(
    SField const& name,
    Issue const& issue,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative)
    : STBase(name)
    , mIssue(issue)
    , mValue(mantissa)
    , mOffset(exponent)
    , mType(
          native ? Type::xrp
                 : (issue.isCFT ? Type::cft : Type::issued_currency))
    , mIsNegative(negative)
{
    canonicalize();
}

STAmount::STAmount(SField const& name, std::int64_t mantissa, bool isCFT)
    : STBase(name), mOffset(0), mType(isCFT ? Type::cft : Type::xrp)
{
    mIssue.isCFT = isCFT;
    set(mantissa);
}

STAmount::STAmount(
    SField const& name,
    std::uint64_t mantissa,
    bool negative,
    bool isCFT)
    : STBase(name)
    , mValue(mantissa)
    , mOffset(0)
    , mType(isCFT ? Type::cft : Type::xrp)
    , mIsNegative(negative)
{
    mIssue.isCFT = isCFT;
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
}

STAmount::STAmount(
    SField const& name,
    Issue const& issue,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : STBase(name)
    , mIssue(issue)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNegative(negative)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
    canonicalize();
}

STAmount::STAmount(SField const& name, STAmount const& from)
    : STBase(name)
    , mIssue(from.mIssue)
    , mValue(from.mValue)
    , mOffset(from.mOffset)
    , mIsNegative(from.mIsNegative)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
    canonicalize();
}

//------------------------------------------------------------------------------

STAmount::STAmount(std::uint64_t mantissa, bool negative, bool isCFT)
    : mValue(mantissa)
    , mOffset(0)
    , mType(isCFT ? Type::cft : Type::xrp)
    , mIsNegative(mantissa != 0 && negative)
{
    mIssue.isCFT = isCFT;
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
}

STAmount::STAmount(
    Issue const& issue,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : mIssue(issue), mValue(mantissa), mOffset(exponent), mIsNegative(negative)
{
    canonicalize();
}

STAmount::STAmount(Issue const& issue, std::int64_t mantissa, int exponent)
    : mIssue(issue), mOffset(exponent)
{
    set(mantissa);
    canonicalize();
}

STAmount::STAmount(
    Issue const& issue,
    std::uint32_t mantissa,
    int exponent,
    bool negative)
    : STAmount(issue, safe_cast<std::uint64_t>(mantissa), exponent, negative)
{
}

STAmount::STAmount(Issue const& issue, int mantissa, int exponent)
    : STAmount(issue, safe_cast<std::int64_t>(mantissa), exponent)
{
}

// Legacy support for new-style amounts
STAmount::STAmount(IOUAmount const& amount, Issue const& issue)
    : mIssue(issue)
    , mOffset(amount.exponent())
    , mType(Type::issued_currency)
    , mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = static_cast<std::uint64_t>(-amount.mantissa());
    else
        mValue = static_cast<std::uint64_t>(amount.mantissa());

    canonicalize();
}

STAmount::STAmount(XRPAmount const& amount)
    : mOffset(0), mType(Type::xrp), mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = unsafe_cast<std::uint64_t>(-amount.drops());
    else
        mValue = unsafe_cast<std::uint64_t>(amount.drops());

    canonicalize();
}

STAmount::STAmount(CFTAmount const& amount)
    : mOffset(0), mType(Type::cft), mIsNegative(amount < beast::zero)
{
    mIssue.isCFT = true;
    if (mIsNegative)
        mValue = unsafe_cast<std::uint64_t>(-amount.cft());
    else
        mValue = unsafe_cast<std::uint64_t>(amount.cft());

    canonicalize();
}

std::unique_ptr<STAmount>
STAmount::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STAmount>(sit, name);
}

STBase*
STAmount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STAmount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

//------------------------------------------------------------------------------
//
// Conversion
//
//------------------------------------------------------------------------------
XRPAmount
STAmount::xrp() const
{
    if (!native())
        Throw<std::logic_error>(
            "Cannot return non-native STAmount as XRPAmount");

    auto drops = static_cast<XRPAmount::value_type>(mValue);

    if (mIsNegative)
        drops = -drops;

    return XRPAmount{drops};
}

IOUAmount
STAmount::iou() const
{
    if (native() || isCFT())
        Throw<std::logic_error>("Cannot return native STAmount as IOUAmount");

    auto mantissa = static_cast<std::int64_t>(mValue);
    auto exponent = mOffset;

    if (mIsNegative)
        mantissa = -mantissa;

    return {mantissa, exponent};
}

CFTAmount
STAmount::cft() const
{
    if (!isCFT())
        Throw<std::logic_error>("Cannot return STAmount as CFTAmount");

    auto value = static_cast<CFTAmount::cft_type>(mValue);

    if (mIsNegative)
        value = -value;

    return CFTAmount{value};
}

STAmount&
STAmount::operator=(IOUAmount const& iou)
{
    assert(!native());
    mOffset = iou.exponent();
    mIsNegative = iou < beast::zero;
    if (mIsNegative)
        mValue = static_cast<std::uint64_t>(-iou.mantissa());
    else
        mValue = static_cast<std::uint64_t>(iou.mantissa());
    return *this;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

STAmount&
STAmount::operator+=(STAmount const& a)
{
    *this = *this + a;
    return *this;
}

STAmount&
STAmount::operator-=(STAmount const& a)
{
    *this = *this - a;
    return *this;
}

STAmount
operator+(STAmount const& v1, STAmount const& v2)
{
    if (!areComparable(v1, v2))
        Throw<std::runtime_error>("Can't add amounts that are't comparable!");

    if (v2 == beast::zero)
        return v1;

    if (v1 == beast::zero)
    {
        // Result must be in terms of v1 currency and issuer.
        return {
            v1.getFName(),
            v1.issue(),
            v2.mantissa(),
            v2.exponent(),
            v2.negative()};
    }

    // TODO
    if (v1.native())
        return {v1.getFName(), getSNValue(v1) + getSNValue(v2)};
    if (v1.isCFT())
        return {v1.mIssue, v1.cft().cft() + v2.cft().cft()};

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
        return {v1.getFName(), v1.issue()};

    if (fv >= 0)
        return STAmount{
            v1.getFName(),
            v1.issue(),
            static_cast<std::uint64_t>(fv),
            ov1,
            false};

    return STAmount{
        v1.getFName(), v1.issue(), static_cast<std::uint64_t>(-fv), ov1, true};
}

STAmount
operator-(STAmount const& v1, STAmount const& v2)
{
    return v1 + (-v2);
}

//------------------------------------------------------------------------------

std::uint64_t const STAmount::uRateOne = getRate(STAmount(1), STAmount(1));

void
STAmount::setIssue(Issue const& issue)
{
    mIssue = issue;
    mType = isXRP(*this) ? Type::xrp
                         : (mIssue.isCFT ? Type::cft : Type::issued_currency);
}

// Convert an offer into an index amount so they sort by rate.
// A taker will take the best, lowest, rate first.
// (e.g. a taker will prefer pay 1 get 3 over pay 1 get 2.
// --> offerOut: takerGets: How much the offerer is selling to the taker.
// -->  offerIn: takerPays: How much the offerer is receiving from the taker.
// <--    uRate: normalize(offerIn/offerOut)
//             A lower rate is better for the person taking the order.
//             The taker gets more for less with a lower rate.
// Zero is returned if the offer is worthless.
std::uint64_t
getRate(STAmount const& offerOut, STAmount const& offerIn)
{
    if (offerOut == beast::zero)
        return 0;
    try
    {
        STAmount r = divide(offerIn, offerOut, noIssue());
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

void
STAmount::setJson(Json::Value& elem) const
{
    elem = Json::objectValue;

    // It is an error for currency or issuer not to be specified for valid
    // json, unless XRP
    switch (mType)
    {
        case Type::xrp:
            elem = getText();
            break;
        case Type::issued_currency:
            elem[jss::value] = getText();
            elem[jss::currency] = to_string(mIssue.currency);
            elem[jss::issuer] = to_string(mIssue.account);
            break;
        case Type::cft:
            elem[jss::value] = getText();
            elem[jss::cft_asset] = to_string(mIssue.currency);
            elem[jss::issuer] = to_string(mIssue.account);
            break;
        default:
            Throw<std::runtime_error>("Invalid currency type");
    }
}

//------------------------------------------------------------------------------
//
// STBase
//
//------------------------------------------------------------------------------

SerializedTypeID
STAmount::getSType() const
{
    return STI_AMOUNT;
}

std::string
STAmount::getFullText() const
{
    std::string ret;

    ret.reserve(64);
    ret = getText() + "/" + to_string(mIssue.currency);

    if (!native())
    {
        ret += "/";

        if (isXRP(*this))
            ret += "0";
        else if (mIssue.account == noAccount())
            ret += "1";
        else
            ret += to_string(mIssue.account);
    }

    return ret;
}

std::string
STAmount::getText() const
{
    // keep full internal accuracy, but make more human friendly if posible
    if (*this == beast::zero)
        return "0";

    std::string const raw_value(std::to_string(mValue));
    std::string ret;

    if (mIsNegative)
        ret.append(1, '-');

    bool const scientific(
        (mOffset != 0) && ((mOffset < -25) || (mOffset > -5)));

    if (native() || mType == Type::cft || scientific)
    {
        ret.append(raw_value);

        if (scientific)
        {
            ret.append(1, 'e');
            ret.append(std::to_string(mOffset));
        }

        return ret;
    }

    assert(mOffset + 43 > 0);

    size_t const pad_prefix = 27;
    size_t const pad_suffix = 23;

    std::string val;
    val.reserve(raw_value.length() + pad_prefix + pad_suffix);
    val.append(pad_prefix, '0');
    val.append(raw_value);
    val.append(pad_suffix, '0');

    size_t const offset(mOffset + 43);

    auto pre_from(val.begin());
    auto const pre_to(val.begin() + offset);

    auto const post_from(val.begin() + offset);
    auto post_to(val.end());

    // Crop leading zeroes. Take advantage of the fact that there's always a
    // fixed amount of leading zeroes and skip them.
    if (std::distance(pre_from, pre_to) > pad_prefix)
        pre_from += pad_prefix;

    assert(post_to >= post_from);

    pre_from = std::find_if(pre_from, pre_to, [](char c) { return c != '0'; });

    // Crop trailing zeroes. Take advantage of the fact that there's always a
    // fixed amount of trailing zeroes and skip them.
    if (std::distance(post_from, post_to) > pad_suffix)
        post_to -= pad_suffix;

    assert(post_to >= post_from);

    post_to = std::find_if(
                  std::make_reverse_iterator(post_to),
                  std::make_reverse_iterator(post_from),
                  [](char c) { return c != '0'; })
                  .base();

    // Assemble the output:
    if (pre_from == pre_to)
        ret.append(1, '0');
    else
        ret.append(pre_from, pre_to);

    if (post_to != post_from)
    {
        ret.append(1, '.');
        ret.append(post_from, post_to);
    }

    return ret;
}

Json::Value STAmount::getJson(JsonOptions) const
{
    Json::Value elem;
    setJson(elem);
    return elem;
}

void
STAmount::add(Serializer& s) const
{
    switch (mType)
    {
        case STAmount::Type::xrp:
            /*
             * Serialized format:
             *
             * Value encoding (first 64 bits):
             *  0 [sign bit] 0 [4 unused bits, set to 0] [57 bits for value]
             */
            // TODO why?
            assert(mOffset == 0);
            // Ensure that value is no greater than 57 bits
            assert(mValue == (mValue & cValueMask));

            if (mIsNegative)
                s.add64(mValue);
            else
                s.add64(mValue | cSign);
            break;

        case STAmount::Type::cft:
            /*
             * Serialized format:
             *
             * Value encoding (first 64 bits):
             * TODO should we use 64 bits for value? do we need to?
             *  0 [sign bit] 1 [4 unused bits, set to 0] [57 bits for value]
             *
             * CFT ID
             * Currency encoding (next 160 bits)
             *
             * Issuer encoding (next 160 bits)
             */
            // Ensure that value is no greater than 57 bits
            assert(mValue == (mValue & cValueMask));

            if (mIsNegative)
                s.add64(mValue | cCFToken);
            else
                s.add64(mValue | cCFToken | cSign);

            s.addBitString(mIssue.currency);
            s.addBitString(mIssue.account);
            break;

        case STAmount::Type::issued_currency:
            /*
             * Serialized format:
             *
             * Value encoding (first 64 bits):
             *  1 [sign bit] [8 bits for (exponent + 97)] [54 bits for mantissa]
             * Special case for 0 value:
             *  1 [63 bits of 0]
             *
             *
             * Currency encoding (next 160 bits)
             *
             * Issuer encoding (next 160 bits)
             */
            if (*this == beast::zero)
                s.add64(cIssuedCurrency);
            else if (mIsNegative)  // 512 = not native
                s.add64(
                    mValue |
                    (static_cast<std::uint64_t>(mOffset + 512 + 97)
                     << (64 - 10)));
            else  // 256 = positive
                s.add64(
                    mValue |
                    (static_cast<std::uint64_t>(mOffset + 512 + 256 + 97)
                     << (64 - 10)));

            s.addBitString(mIssue.currency);
            s.addBitString(mIssue.account);
            break;

        default:
            Throw<std::runtime_error>(
                "Invalid STAmount type for serialization");
    }
}

bool
STAmount::isEquivalent(const STBase& t) const
{
    const STAmount* v = dynamic_cast<const STAmount*>(&t);
    return v && (*v == *this);
}

bool
STAmount::isDefault() const
{
    return (mValue == 0) && native();
}

//------------------------------------------------------------------------------

// amount = mValue * [10 ^ mOffset]
// Representation range is 10^80 - 10^(-80).
//
// On the wire:
// - high bit is 1 for issued currency, 0 for XRP/CFT
// - next bit is 1 for positive, 0 for negative (except 0 issued currency, which
//    is a special case of 0x8000000000000000)
// - for issued currencies:
//     - the next 8 bits are (mOffset+97).
//       The +97 is so that this value is always positive.
//     - The remaining 54 bits are significant digits (mantissa)
// - for XRP:
//     - the next bit is 0 (indicates XRP)
//     - the next four bits are 0 (reserved, but must remain 0 because they
//       might be used for future additional amount types).
//     - the next 57 bits are for the value of drops.
// - for CFT:
//     - the next bit is 1 (indicates CFT)
//     - the next four bits are 0 (reserved, but must remain 0 because they
//       might be used for future additional amount types).
//     - the next 57 bits are for the value of CFT at the lowest AssetScale.
//
//
// mValue is zero if the amount is zero, otherwise it's within the range
//    10^15 to (10^16 - 1) inclusive.
// mOffset is in the range -96 to +80.
void
STAmount::canonicalize()
{
    if (isXRP(*this) || mIssue.isCFT)
    {
        // native currency amounts should always have an offset of zero
        mType = mIssue.isCFT ? Type::cft : Type::xrp;

        // log(2^64,10) ~ 19.2
        if (mValue == 0 || mOffset <= -20)
        {
            mValue = 0;
            mOffset = 0;
            mIsNegative = false;
            return;
        }

        if (getSTAmountCanonicalizeSwitchover())
        {
            // log(cMaxNativeN, 10) == 17
            if (mOffset > 17)
                Throw<std::runtime_error>(
                    "Native currency amount out of range");
        }

        if (getSTNumberSwitchover() && getSTAmountCanonicalizeSwitchover())
        {
            Number num(
                mIsNegative ? -mValue : mValue, mOffset, Number::unchecked{});
            if (isXRP(*this))
            {
                XRPAmount xrp{num};
                mIsNegative = xrp.drops() < 0;
                mValue = mIsNegative ? -xrp.drops() : xrp.drops();
            }
            else
            {
                CFTAmount c{num};
                mIsNegative = c.cft() < 0;
                mValue = mIsNegative ? -c.cft() : c.cft();
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
                if (getSTAmountCanonicalizeSwitchover())
                {
                    // N.B. do not move the overflow check to after the
                    // multiplication
                    if (mValue > cMaxNativeN)
                        Throw<std::runtime_error>(
                            "Native currency amount out of range");
                }
                mValue *= 10;
                --mOffset;
            }
        }

        if (mValue > cMaxNativeN)
            Throw<std::runtime_error>("Native currency amount out of range");

        return;
    }

    mType = Type::issued_currency;

    if (getSTNumberSwitchover())
    {
        *this = iou();
        return;
    }

    if (mValue == 0)
    {
        mOffset = -100;
        mIsNegative = false;
        return;
    }

    while ((mValue < cMinValue) && (mOffset > cMinOffset))
    {
        mValue *= 10;
        --mOffset;
    }

    while (mValue > cMaxValue)
    {
        if (mOffset >= cMaxOffset)
            Throw<std::runtime_error>("value overflow");

        mValue /= 10;
        ++mOffset;
    }

    if ((mOffset < cMinOffset) || (mValue < cMinValue))
    {
        mValue = 0;
        mIsNegative = false;
        mOffset = -100;
        return;
    }

    if (mOffset > cMaxOffset)
        Throw<std::runtime_error>("value overflow");

    assert((mValue == 0) || ((mValue >= cMinValue) && (mValue <= cMaxValue)));
    assert(
        (mValue == 0) || ((mOffset >= cMinOffset) && (mOffset <= cMaxOffset)));
    assert((mValue != 0) || (mOffset != -100));
}

void
STAmount::set(std::int64_t v)
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

STAmount
amountFromQuality(std::uint64_t rate)
{
    if (rate == 0)
        return STAmount(noIssue());

    std::uint64_t mantissa = rate & ~(255ull << (64 - 8));
    int exponent = static_cast<int>(rate >> (64 - 8)) - 100;

    return STAmount(noIssue(), mantissa, exponent);
}

STAmount
amountFromString(Issue const& issue, std::string const& amount)
{
    static boost::regex const reNumber(
        "^"                       // the beginning of the string
        "([-+]?)"                 // (optional) + or - character
        "(0|[1-9][0-9]*)"         // a number (no leading zeroes, unless 0)
        "(\\.([0-9]+))?"          // (optional) period followed by any number
        "([eE]([+-]?)([0-9]+))?"  // (optional) E, optional + or -, any number
        "$",
        boost::regex_constants::optimize);

    boost::smatch match;

    if (!boost::regex_match(amount, match, reNumber))
        Throw<std::runtime_error>("Number '" + amount + "' is not valid");

    // Match fields:
    //   0 = whole input
    //   1 = sign
    //   2 = integer portion
    //   3 = whole fraction (with '.')
    //   4 = fraction (without '.')
    //   5 = whole exponent (with 'e')
    //   6 = exponent sign
    //   7 = exponent number

    // CHECKME: Why 32? Shouldn't this be 16?
    if ((match[2].length() + match[4].length()) > 32)
        Throw<std::runtime_error>("Number '" + amount + "' is overlong");

    bool negative = (match[1].matched && (match[1] == "-"));

    // Can't specify XRP using fractional representation
    if (isXRP(issue) && match[3].matched)
        Throw<std::runtime_error>("XRP must be specified in integral drops.");

    std::uint64_t mantissa;
    int exponent;

    if (!match[4].matched)  // integer only
    {
        mantissa =
            beast::lexicalCastThrow<std::uint64_t>(std::string(match[2]));
        exponent = 0;
    }
    else
    {
        // integer and fraction
        mantissa = beast::lexicalCastThrow<std::uint64_t>(match[2] + match[4]);
        exponent = -(match[4].length());
    }

    if (match[5].matched)
    {
        // we have an exponent
        if (match[6].matched && (match[6] == "-"))
            exponent -= beast::lexicalCastThrow<int>(std::string(match[7]));
        else
            exponent += beast::lexicalCastThrow<int>(std::string(match[7]));
    }

    return {issue, mantissa, exponent, negative};
}

STAmount
amountFromJson(SField const& name, Json::Value const& v)
{
    STAmount::mantissa_type mantissa = 0;
    STAmount::exponent_type exponent = 0;
    bool negative = false;
    Issue issue;
    STAmount::Type typ;

    Json::Value value;
    Json::Value currency;
    Json::Value issuer;

    if (v.isNull())
    {
        Throw<std::runtime_error>(
            "XRP may not be specified with a null Json value");
    }
    else if (v.isObject())
    {
        if (v.isMember(jss::cft_asset))
        {
            typ = STAmount::Type::cft;
            currency = v[jss::cft_asset];
            issue.isCFT = true;
        }
        else
        {
            typ = STAmount::Type::issued_currency;
            currency = v[jss::currency];
            issue.isCFT = false;
        }

        value = v[jss::value];
        issuer = v[jss::issuer];
    }
    else if (v.isArray())
    {
        value = v.get(Json::UInt(0), 0);
        currency = v.get(Json::UInt(1), Json::nullValue);
        issuer = v.get(Json::UInt(2), Json::nullValue);
        // TODO -.... when does this happen
        typ = STAmount::Type::issued_currency;
    }
    else if (v.isString())
    {
        std::string val = v.asString();
        std::vector<std::string> elements;
        boost::split(elements, val, boost::is_any_of("\t\n\r ,/"));

        if (elements.size() > 3)
            Throw<std::runtime_error>("invalid amount string");

        value = elements[0];

        if (elements.size() > 1)
            currency = elements[1];

        if (elements.size() > 2)
        {
            issuer = elements[2];
            // TODO -.... when does this happen
            typ = STAmount::Type::issued_currency;
        }
    }
    else
    {
        value = v;
    }

    bool const native = !currency.isString() || currency.asString().empty() ||
        (currency.asString() == systemCurrencyCode());

    if (native)
    {
        if (v.isObjectOrNull())
            Throw<std::runtime_error>("XRP may not be specified as an object");
        issue = xrpIssue();
        typ = STAmount::Type::xrp;
    }
    else
    {
        // non-XRP
        if (!to_currency(issue.currency, currency.asString()))
            Throw<std::runtime_error>("invalid currency");

        if (!issuer.isString() || !to_issuer(issue.account, issuer.asString()))
            Throw<std::runtime_error>("invalid issuer");

        if (isXRP(issue.currency))
            Throw<std::runtime_error>("invalid issuer");
    }

    if (value.isInt())
    {
        if (value.asInt() >= 0)
        {
            mantissa = value.asInt();
        }
        else
        {
            mantissa = -value.asInt();
            negative = true;
        }
    }
    else if (value.isUInt())
    {
        mantissa = v.asUInt();
    }
    else if (value.isString())
    {
        auto const ret = amountFromString(issue, value.asString());

        mantissa = ret.mantissa();
        exponent = ret.exponent();
        negative = ret.negative();
    }
    else
    {
        Throw<std::runtime_error>("invalid amount type");
    }
    // TODO need to do something to validate no mantissa/exp/neg for cft

    return {name, issue, mantissa, exponent, typ, negative};
}

bool
amountFromJsonNoThrow(STAmount& result, Json::Value const& jvSource)
{
    try
    {
        result = amountFromJson(sfGeneric, jvSource);
        return true;
    }
    catch (const std::exception& e)
    {
        JLOG(debugLog().warn())
            << "amountFromJsonNoThrow: caught: " << e.what();
    }
    return false;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool
operator==(STAmount const& lhs, STAmount const& rhs)
{
    return areComparable(lhs, rhs) && lhs.negative() == rhs.negative() &&
        lhs.exponent() == rhs.exponent() && lhs.mantissa() == rhs.mantissa();
}

bool
operator<(STAmount const& lhs, STAmount const& rhs)
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

STAmount
operator-(STAmount const& value)
{
    if (value.mantissa() == 0)
        return value;
    return STAmount(
        value.getFName(),
        value.issue(),
        value.mantissa(),
        value.exponent(),
        value.native(),
        !value.negative(),
        STAmount::unchecked{});
}

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

// Calculate (a * b) / c when all three values are 64-bit
// without loss of precision:
static std::uint64_t
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

static std::uint64_t
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

STAmount
divide(STAmount const& num, STAmount const& den, Issue const& issue)
{
    if (den == beast::zero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::zero)
        return {issue};

    std::uint64_t numVal = num.mantissa();
    std::uint64_t denVal = den.mantissa();
    int numOffset = num.exponent();
    int denOffset = den.exponent();

    if (num.native() || num.isCFT())
    {
        while (numVal < STAmount::cMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.native() || den.isCFT())
    {
        while (denVal < STAmount::cMinValue)
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
        issue,
        muldiv(numVal, tenTo17, denVal) + 5,
        numOffset - denOffset - 17,
        num.negative() != den.negative());
}

STAmount
multiply(STAmount const& v1, STAmount const& v2, Issue const& issue)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return STAmount(issue);

    // TODO
    if (v1.native() && v2.native() && isXRP(issue))
    {
        std::uint64_t const minV =
            getSNValue(v1) < getSNValue(v2) ? getSNValue(v1) : getSNValue(v2);
        std::uint64_t const maxV =
            getSNValue(v1) < getSNValue(v2) ? getSNValue(v2) : getSNValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(v1.getFName(), minV * maxV);
    }
    if (v1.isCFT() && v2.isCFT() && issue.isCFT)
    {
        std::uint64_t const minV = getCFTValue(v1) < getCFTValue(v2)
            ? getCFTValue(v1)
            : getCFTValue(v2);
        std::uint64_t const maxV = getCFTValue(v1) < getCFTValue(v2)
            ? getCFTValue(v2)
            : getCFTValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(issue, minV * maxV);
    }

    if (getSTNumberSwitchover())
        return {IOUAmount{Number{v1} * Number{v2}}, issue};

    std::uint64_t value1 = v1.mantissa();
    std::uint64_t value2 = v2.mantissa();
    int offset1 = v1.exponent();
    int offset2 = v2.exponent();

    // TODO
    if (v1.native() || v1.isCFT())
    {
        while (value1 < STAmount::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.native() || v2.isCFT())
    {
        while (value2 < STAmount::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    return STAmount(
        issue,
        muldiv(value1, value2, tenTo14) + 7,
        offset1 + offset2 + 14,
        v1.negative() != v2.negative());
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
static void
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
    else if (value > STAmount::cMaxValue)
    {
        while (value > (10 * STAmount::cMaxValue))
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
static void
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
    else if (value > STAmount::cMaxValue)
    {
        while (value > (10 * STAmount::cMaxValue))
        {
            value /= 10;
            ++offset;
        }
        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

namespace {

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

}  // anonymous namespace

// Pass the canonicalizeRound function pointer as a template parameter.
//
// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <
    void (*CanonicalizeFunc)(bool, std::uint64_t&, int&, bool),
    typename MightSaveRound>
static STAmount
mulRoundImpl(
    STAmount const& v1,
    STAmount const& v2,
    Issue const& issue,
    bool roundUp)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return {issue};

    bool const xrp = isXRP(issue);

    // TODO
    if (v1.native() && v2.native() && xrp)
    {
        std::uint64_t minV =
            (getSNValue(v1) < getSNValue(v2)) ? getSNValue(v1) : getSNValue(v2);
        std::uint64_t maxV =
            (getSNValue(v1) < getSNValue(v2)) ? getSNValue(v2) : getSNValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(v1.getFName(), minV * maxV);
    }
    if (v1.isCFT() && v2.isCFT() && issue.isCFT)
    {
        std::uint64_t minV = (getCFTValue(v1) < getCFTValue(v2))
            ? getCFTValue(v1)
            : getCFTValue(v2);
        std::uint64_t maxV = (getCFTValue(v1) < getCFTValue(v2))
            ? getCFTValue(v2)
            : getCFTValue(v1);

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(issue, minV * maxV);
    }

    std::uint64_t value1 = v1.mantissa(), value2 = v2.mantissa();
    int offset1 = v1.exponent(), offset2 = v2.exponent();

    // TODO
    if (v1.native() || v1.isCFT())
    {
        while (value1 < STAmount::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    // TODO
    if (v2.native() || v2.isCFT())
    {
        while (value2 < STAmount::cMinValue)
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
    std::uint64_t amount = muldiv_round(
        value1, value2, tenTo14, (resultNegative != roundUp) ? tenTo14m1 : 0);

    int offset = offset1 + offset2 + 14;
    if (resultNegative != roundUp)
    {
        CanonicalizeFunc(xrp, amount, offset, roundUp);
    }
    STAmount result = [&]() {
        // If appropriate, tell Number to round down.  This gives the desired
        // result from STAmount::canonicalize.
        MightSaveRound const savedRound(Number::towards_zero);
        return STAmount(issue, amount, offset, resultNegative);
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
            amount = STAmount::cMinValue;
            offset = STAmount::cMinOffset;
        }
        return STAmount(issue, amount, offset, resultNegative);
    }
    return result;
}

STAmount
mulRound(
    STAmount const& v1,
    STAmount const& v2,
    Issue const& issue,
    bool roundUp)
{
    return mulRoundImpl<canonicalizeRound, DontAffectNumberRoundMode>(
        v1, v2, issue, roundUp);
}

STAmount
mulRoundStrict(
    STAmount const& v1,
    STAmount const& v2,
    Issue const& issue,
    bool roundUp)
{
    return mulRoundImpl<canonicalizeRoundStrict, NumberRoundModeGuard>(
        v1, v2, issue, roundUp);
}

// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <typename MightSaveRound>
static STAmount
divRoundImpl(
    STAmount const& num,
    STAmount const& den,
    Issue const& issue,
    bool roundUp)
{
    if (den == beast::zero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::zero)
        return {issue};

    std::uint64_t numVal = num.mantissa(), denVal = den.mantissa();
    int numOffset = num.exponent(), denOffset = den.exponent();

    // TODO
    if (num.native() || num.isCFT())
    {
        while (numVal < STAmount::cMinValue)
        {
            numVal *= 10;
            --numOffset;
        }
    }

    // TODO
    if (den.native() || den.isCFT())
    {
        while (denVal < STAmount::cMinValue)
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
    std::uint64_t amount = muldiv_round(
        numVal, tenTo17, denVal, (resultNegative != roundUp) ? denVal - 1 : 0);

    int offset = numOffset - denOffset - 17;

    if (resultNegative != roundUp)  // TODO
        canonicalizeRound(isXRP(issue) || issue.isCFT, amount, offset, roundUp);

    STAmount result = [&]() {
        // If appropriate, tell Number the rounding mode we are using.
        // Note that "roundUp == true" actually means "round away from zero".
        // Otherwise round toward zero.
        using enum Number::rounding_mode;
        MightSaveRound const savedRound(
            roundUp ^ resultNegative ? upward : downward);
        return STAmount(issue, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (isXRP(issue) || issue.isCFT)
        {
            // return the smallest value above zero
            amount = 1;
            offset = 0;
        }
        else
        {
            // return the smallest value above zero
            amount = STAmount::cMinValue;
            offset = STAmount::cMinOffset;
        }
        return STAmount(issue, amount, offset, resultNegative);
    }
    return result;
}

STAmount
divRound(
    STAmount const& num,
    STAmount const& den,
    Issue const& issue,
    bool roundUp)
{
    return divRoundImpl<DontAffectNumberRoundMode>(num, den, issue, roundUp);
}

STAmount
divRoundStrict(
    STAmount const& num,
    STAmount const& den,
    Issue const& issue,
    bool roundUp)
{
    return divRoundImpl<NumberRoundModeGuard>(num, den, issue, roundUp);
}

}  // namespace ripple
