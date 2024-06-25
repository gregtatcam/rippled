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
#include <ripple/protocol/STMPTAmount.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <boost/algorithm/string.hpp>
#include <boost/math_fwd.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/regex.hpp>
#include <iostream>
#include <iterator>
#include <memory>

namespace ripple {

static bool
areComparable(STMPTAmount const& v1, STMPTAmount const& v2)
{
    return v1.issue().mpt() == v2.issue().mpt();
}

//------------------------------------------------------------------------------

STMPTAmount::STMPTAmount(SerialIter& sit, SField const& name) : STBase(name)
{
    std::uint64_t value = sit.get64();

    assert(value & cMPToken);

    MPTIssue issue{getMPT(sit.get192())};
}

STMPTAmount::STMPTAmount(
    SField const& name,
    MPTIssue const& issue,
    value_type value)
    : STBase(name)
    , mIssue(issue)
    , mValue(value)
{
}

STMPTAmount::STMPTAmount(
    MPTIssue const& issue,
    value_type value)
    : mIssue(issue)
    , mValue(value)
{
}

//------------------------------------------------------------------------------

std::unique_ptr<STMPTAmount>
STMPTAmount::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STMPTAmount>(sit, name);
}

STBase*
STMPTAmount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STMPTAmount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

//------------------------------------------------------------------------------
//
// Conversion
//
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

STMPTAmount&
STMPTAmount::operator+=(STMPTAmount const& a)
{
    *this = *this + a;
    return *this;
}

STMPTAmount&
STMPTAmount::operator-=(STMPTAmount const& a)
{
    *this = *this - a;
    return *this;
}

STMPTAmount
operator+(STMPTAmount const& v1, STMPTAmount const& v2)
{
    if (!areComparable(v1, v2))
        Throw<std::runtime_error>("Can't add amounts that are't comparable!");

    return STMPTAmount{v1.getFName(), v1.issue(), v1.mValue + v2.mValue};
}

STMPTAmount
operator-(STMPTAmount const& v1, STMPTAmount const& v2)
{
    return v1 + (-v2);
}

//------------------------------------------------------------------------------

void
STMPTAmount::setIssue(MPTIssue const& issue)
{
    mIssue = issue;
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
getRate(STMPTAmount const& offerOut, STMPTAmount const& offerIn)
{
    // TODO
    if (offerOut == beast::zero)
        return 0;
    // overflow -- very bad offer
    return 0;
}

void
STMPTAmount::setJson(Json::Value& elem) const
{
    elem = Json::objectValue;

    elem = getText();
}

//------------------------------------------------------------------------------
//
// STBase
//
//------------------------------------------------------------------------------

SerializedTypeID
STMPTAmount::getSType() const
{
    return STI_MPT_AMOUNT;
}

std::string
STMPTAmount::getFullText() const
{
    std::string ret;

    ret.reserve(64);
    ret = getText() + "/" + to_string(mIssue.getMptID());
    return ret;
}

std::string
STMPTAmount::getText() const
{
    // keep full internal accuracy, but make more human friendly if posible
    if (*this == beast::zero)
        return "0";

    std::string const raw_value(std::to_string(mValue));
    return raw_value;
}

Json::Value STMPTAmount::getJson(JsonOptions) const
{
    Json::Value elem;
    setJson(elem);
    return elem;
}

void
STMPTAmount::add(Serializer& s) const
{
    s.add64(mValue | cMPToken);
    s.addBitString(mIssue.getMptID());
}

bool
STMPTAmount::isEquivalent(const STBase& t) const
{
    const STMPTAmount* v = dynamic_cast<const STMPTAmount*>(&t);
    return v && (*v == *this);
}

bool
STMPTAmount::isDefault() const
{
    return mValue == 0; //??
}

MPT const&
STMPTAmount::getAssetID() const
{
    return mIssue.mpt();
}

//------------------------------------------------------------------------------

STMPTAmount
mptAmountFromString(MPTIssue const& issue, std::string const& amount)
{
    static boost::regex const reNumber(
        "^"                       // the beginning of the string
        "([+]?)"                 // (optional) + or - character
        "(0|[1-9][0-9]*)"         // a number (no leading zeroes, unless 0)
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
    //   3 = whole exponent (with 'e')
    //   4 = exponent sign
    //   5 = exponent number

    // CHECKME: Why 32? Shouldn't this be 16?
    if ((match[2].length()) > 32)
        Throw<std::runtime_error>("Number '" + amount + "' is overlong");

    std::uint64_t mantissa;
    int exponent;

    mantissa = beast::lexicalCastThrow<std::uint64_t>(std::string(match[2]));
    exponent = 0;

    if (match[3].matched)
    {
        // we have an exponent
        if (match[4].matched && (match[4] == "-"))
            exponent -= beast::lexicalCastThrow<int>(std::string(match[5]));
        else
            exponent += beast::lexicalCastThrow<int>(std::string(match[5]));
    }

    if (exponent < 0)
    {
        if (static_cast<unsigned int>(-exponent) > sizeof(std::uint64_t) * 8)
            Throw<std::runtime_error>("Number is overlong");
        if (mantissa >> (-exponent) != 0)
            Throw<std::runtime_error>("Number is floating point");
    }

    return {issue, mantissa}; // TODO
}

STMPTAmount
mptAmountFromJson(SField const& name, Json::Value const& v)
{
    return STMPTAmount{name, MPTIssue{}, 0}; // TODO
}

bool
amountFromJsonNoThrow(STMPTAmount& result, Json::Value const& jvSource)
{
    try
    {
        result = mptAmountFromJson(sfGeneric, jvSource);
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
operator==(STMPTAmount const& lhs, STMPTAmount const& rhs)
{
    return areComparable(lhs, rhs) && lhs.negative() == rhs.negative() &&
        lhs.exponent() == rhs.exponent() && lhs.mantissa() == rhs.mantissa();
}

bool
operator<(STMPTAmount const& lhs, STMPTAmount const& rhs)
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

STMPTAmount
operator-(STMPTAmount const& value)
{
    if (value.mantissa() == 0)
        return value;
    return STMPTAmount(
        value.getFName(),
        value.issue(),
        value.mantissa());
}

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

}  // namespace ripple
