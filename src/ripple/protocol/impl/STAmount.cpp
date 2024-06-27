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
#include <ripple/protocol/Indexes.h>
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

STAmount::STAmount(SerialIter& sit, SField const& name)
    : AssetAmount<Asset>(xrpIssue()), STBase(name)
{
    // TODO MPT make sure backward compatible

    std::uint64_t value = sit.get64();
    // TODO must fix serialization for IOU, it incorrectly sets cMPToken
    bool isMPT = (value & cMPToken) && !(value & cIssuedCurrency);

    // native or MPT
    if ((value & cIssuedCurrency) == 0 || isMPT)
    {
        if (isMPT)
        {
            // mAsset = std::make_pair(
            //     sit.get32(), static_cast<AccountID>(sit.get160()));
            mAsset = sit.get192();
        }
        else
            mAsset = xrpIssue();
        // positive
        if ((value & cPositive) != 0)
        {
            mValue = value & cValueMask;
            mOffset = 0;
            mIsNative = !isMPT;
            mIsNegative = false;
            return;
        }

        // negative
        if (value == 0)
            Throw<std::runtime_error>("negative zero is not canonical");

        mValue = value & cValueMask;
        mOffset = 0;
        mIsNative = !isMPT;
        mIsNegative = true;
        return;
    }

    Issue issue;
    issue.currency = sit.get160();

    if (isXRP(issue.currency))
        Throw<std::runtime_error>("invalid native currency");

    issue.account = sit.get160();

    if (isXRP(issue.account))
        Throw<std::runtime_error>("invalid native account");

    // 10 bits for the offset, sign and "not native" flag
    int offset = static_cast<int>(value >> (64 - 10));

    value &= ~(1023ull << (64 - 10));

    if (value)
    {
        bool isNegative = (offset & 256) == 0;
        offset = (offset & 255) - 97;  // center the range

        if (value < cMinValue || value > cMaxValue || offset < cMinOffset ||
            offset > cMaxOffset)
        {
            Throw<std::runtime_error>("invalid currency value");
        }

        mAsset = issue;
        mValue = value;
        mOffset = offset;
        mIsNegative = isNegative;
        canonicalize();
        return;
    }

    if (offset != 512)
        Throw<std::runtime_error>("invalid currency value");

    mAsset = issue;
    mValue = 0;
    mOffset = 0;
    mIsNegative = false;
    canonicalize();
}

#if 0
STAmount::STAmount(
    SField const& name,
    Asset const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : STBase(name)
    , mAsset(native ? Asset{xrpIssue()} : asset)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNative(native)
    , mIsNegative(negative)
{
}

STAmount::STAmount(
    Asset const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative,
    unchecked)
    : mAsset(native ? Asset{xrpIssue()} : asset)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNative(native)
    , mIsNegative(negative)
{
}

STAmount::STAmount(
    SField const& name,
    Asset const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool native,
    bool negative)
    : STBase(name)
    , mAsset(native ? Asset{xrpIssue()} : asset)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNative(native)
    , mIsNegative(negative)
{
    canonicalize();
}
#endif

STAmount::STAmount(SField const& name, std::int64_t mantissa)
    : AssetAmount<Asset>(xrpIssue(), mantissa, 0, false), STBase(name)
{
    set(mantissa);
}

STAmount::STAmount(SField const& name, std::uint64_t mantissa, bool negative)
    : AssetAmount<Asset>(xrpIssue(), mantissa, 0, negative), STBase(name)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
}

#if 0
STAmount::STAmount(
    SField const& name,
    Asset const& asset,
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
#endif

STAmount::STAmount(SField const& name, STAmount const& from)
    : AssetAmount<Asset>(
          from.mAsset,
          from.mValue,
          from.mOffset,
          from.mIsNegative,
          unchecked())
    , STBase(name)
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
    canonicalize();
}

//------------------------------------------------------------------------------

STAmount::STAmount(std::uint64_t mantissa, bool negative)
    : AssetAmount<Asset>(xrpIssue(), mantissa, 0, negative, unchecked())
{
    assert(mValue <= std::numeric_limits<std::int64_t>::max());
}

#if 0
STAmount::STAmount(
    Asset const& asset,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : mAsset(asset), mValue(mantissa), mOffset(exponent), mIsNegative(negative)
{
    canonicalize();
}

STAmount::STAmount(Asset const& asset, std::int64_t mantissa, int exponent)
    : mAsset(asset), mOffset(exponent)
{
    set(mantissa);
    canonicalize();
}

STAmount::STAmount(
    Asset const& asset,
    std::uint32_t mantissa,
    int exponent,
    bool negative)
    : STAmount(asset, safe_cast<std::uint64_t>(mantissa), exponent, negative)
{
}

STAmount::STAmount(Asset const& asset, int mantissa, int exponent)
    : STAmount(asset, safe_cast<std::int64_t>(mantissa), exponent)
{
}

// Legacy support for new-style amounts
STAmount::STAmount(IOUAmount const& amount, Asset const& asset)
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
#endif

STAmount::STAmount(XRPAmount const& amount)
    : AssetAmount<Asset>(
          xrpIssue(),
          amount.drops(),
          0,
          amount < beast::zero,
          unchecked())
{
    if (mIsNegative)
        mValue = -mValue;

    canonicalize();
}

STAmount::STAmount(SField const& name, AssetAmount<Asset> const& amount)
    : STAmount{
          name,
          amount.asset(),
          amount.mantissa(),
          amount.exponent(),
          amount.negative()}
{
}

// Legacy support for new-style amounts
STAmount::STAmount(IOUAmount const& amount, Asset const& asset)
    : AssetAmount<Asset>(
          asset,
          amount.mantissa(),
          amount.exponent(),
          amount < beast::zero,
          unchecked())
{
    if (mIsNegative)
        mValue = -mValue;

    canonicalize();
}

STAmount::STAmount(MPTAmount const& amount, Asset const& asset)
    : AssetAmount<Asset>(
          asset,
          amount.mpt(),
          0,
          amount < beast::zero,
          unchecked())
{
    if (mIsNegative)
        mValue = -mValue;

    canonicalize();
}

#if 0
STAmount::STAmount(MPTAmount const& amount, Asset const& asset)
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
#endif

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
    if (!mIsNative)
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
    if (mIsNative || isMPT())
        Throw<std::logic_error>("Cannot return native STAmount as IOUAmount");

    auto mantissa = static_cast<std::int64_t>(mValue);
    auto exponent = mOffset;

    if (mIsNegative)
        mantissa = -mantissa;

    return {mantissa, exponent};
}

MPTAmount
STAmount::mpt() const
{
    if (!isMPT())
        Throw<std::logic_error>("Cannot return STAmount as MPTAmount");

    auto value = static_cast<MPTAmount::mpt_type>(mValue);

    if (mIsNegative)
        value = -value;

    return MPTAmount{value};
}

STAmount&
STAmount::operator=(IOUAmount const& iou)
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
    auto const res = static_cast<AssetAmount<Asset> const&>(v1) +
        static_cast<AssetAmount<Asset> const&>(v2);
    return STAmount{v1.getFName(), res};
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
    mAsset = issue;
    mIsNative = isXRP(*this);
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

    if (!mIsNative)
    {
        // It is an error for currency or issuer not to be specified for valid
        // json.
        elem[jss::value] = getText();
        if (mAsset.isMPT())
            elem[jss::mpt_issuance_id] = to_string(mAsset.mptIssue());
        else
        {
            elem[jss::currency] = to_string(mAsset.issue().currency);
            elem[jss::issuer] = to_string(mAsset.issue().account);
        }
    }
    else
    {
        elem = getText();
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
    ret = getText() + "/" + mAsset.getText();
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

    if (mIsNative || mAsset.isMPT() || scientific)
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
    // TODO MPT make sure backward compatible
    if (mIsNative)
    {
        assert(mOffset == 0);

        if (!mIsNegative)
            s.add64(mValue | cPositive);
        else
            s.add64(mValue);
    }
    else
    {
        if (mAsset.isMPT())
        {
            if (mIsNegative)
                s.add64(mValue | cMPToken);
            else
                s.add64(mValue | cMPToken | cPositive);
            // s.add32(mAsset.mptIssue().sequence());
            // s.addBitString(mAsset.mptIssue().account());
            auto const& mptIssue = mAsset.mptIssue();
            s.addBitString(getMptID(mptIssue.getIssuer(), mptIssue.sequence()));
        }
        else
        {
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
            s.addBitString(mAsset.issue().currency);
            s.addBitString(mAsset.issue().account);
        }
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
    return (mValue == 0) && mIsNative;
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
amountFromString(Asset const& asset, std::string const& amount)
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
    if (isXRP(asset) && match[3].matched)
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

    return {asset, mantissa, exponent, negative};
}

STAmount
amountFromJson(SField const& name, Json::Value const& v)
{
    STAmount::mantissa_type mantissa = 0;
    STAmount::exponent_type exponent = 0;
    bool negative = false;
    Asset asset;

    Json::Value value;
    Json::Value currencyOrMPTID;
    Json::Value issuer;
    bool isMPT = false;

    if (v.isNull())
    {
        Throw<std::runtime_error>(
            "XRP may not be specified with a null Json value");
    }
    else if (v.isObject())
    {
        if (!validJSONAsset(v))
            Throw<std::runtime_error>("Invalid Asset's Json specification");

        value = v[jss::value];
        if (v.isMember(jss::mpt_issuance_id))
        {
            isMPT = true;
            currencyOrMPTID = v[jss::mpt_issuance_id];
        }
        else
        {
            currencyOrMPTID = v[jss::currency];
            issuer = v[jss::issuer];
        }
    }
    else if (v.isArray())
    {
        value = v.get(Json::UInt(0), 0);
        currencyOrMPTID = v.get(Json::UInt(1), Json::nullValue);
        issuer = v.get(Json::UInt(2), Json::nullValue);
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
            currencyOrMPTID = elements[1];

        if (elements.size() > 2)
            issuer = elements[2];
    }
    else
    {
        value = v;
    }

    bool const native = !currencyOrMPTID.isString() ||
        currencyOrMPTID.asString().empty() ||
        (currencyOrMPTID.asString() == systemCurrencyCode());

    if (native)
    {
        if (v.isObjectOrNull())
            Throw<std::runtime_error>("XRP may not be specified as an object");
        asset = xrpIssue();
    }
    else
    {
        if (isMPT)
        {
            // sequence (32 bits) + account (160 bits)
            uint192 u;
            if (!u.parseHex(currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid MPTokenIssuanceID");
            asset = u;
        }
        else
        {
            Issue issue;
            if (!to_currency(issue.currency, currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid currency");
            if (!issuer.isString() ||
                !to_issuer(issue.account, issuer.asString()))
                Throw<std::runtime_error>("invalid issuer");
            if (isXRP(issue))
                Throw<std::runtime_error>("invalid issuer");
            asset = issue;
        }
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
        auto const ret = amountFromString(asset, value.asString());

        mantissa = ret.mantissa();
        exponent = ret.exponent();
        negative = ret.negative();
    }
    else
    {
        Throw<std::runtime_error>("invalid amount type");
    }

    return {name, asset, mantissa, exponent, native, negative};
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
    return detail::areComparable(lhs, rhs) &&
        lhs.negative() == rhs.negative() && lhs.exponent() == rhs.exponent() &&
        lhs.mantissa() == rhs.mantissa();
}

bool
operator<(STAmount const& lhs, STAmount const& rhs)
{
    if (!detail::areComparable(lhs, rhs))
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
        value.asset(),
        value.mantissa(),
        value.exponent(),
        value.native(),
        !value.negative(),
        STAmount::unchecked{});
}

STAmount
divide(STAmount const& num, STAmount const& den, Asset const& asset)
{
    auto const res = divide(
        static_cast<AssetAmount<Asset> const&>(num),
        static_cast<AssetAmount<Asset> const&>(den),
        asset);
    return STAmount{num.getFName(), res};
}

STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset)
{
    auto const res = multiply(
        static_cast<AssetAmount<Asset> const&>(v1),
        static_cast<AssetAmount<Asset> const&>(v2),
        asset);
    return STAmount{v1.getFName(), res};
}

STAmount
mulRound(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp)
{
    auto const res = mulRound(
        static_cast<AssetAmount<Asset> const&>(v1),
        static_cast<AssetAmount<Asset> const&>(v2),
        asset,
        roundUp);
    return STAmount{v1.getFName(), res};
}

STAmount
mulRoundStrict(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp)
{
    auto const res = mulRoundStrict(
        static_cast<AssetAmount<Asset> const&>(v1),
        static_cast<AssetAmount<Asset> const&>(v2),
        asset,
        roundUp);
    return STAmount{v1.getFName(), res};
}

STAmount
divRound(
    STAmount const& num,
    STAmount const& den,
    Asset const& asset,
    bool roundUp)
{
    auto const res = divRound(
        static_cast<AssetAmount<Asset> const&>(num),
        static_cast<AssetAmount<Asset> const&>(den),
        asset,
        roundUp);
    return STAmount{num.getFName(), res};
}

STAmount
divRoundStrict(
    STAmount const& num,
    STAmount const& den,
    Asset const& asset,
    bool roundUp)
{
    auto const res = divRoundStrict(
        static_cast<AssetAmount<Asset> const&>(num),
        static_cast<AssetAmount<Asset> const&>(den),
        asset,
        roundUp);
    return STAmount{num.getFName(), res};
}

}  // namespace ripple
