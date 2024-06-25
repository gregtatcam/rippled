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
#include <ripple/protocol/STEitherAmount.h>
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

STEitherAmount::STEitherAmount(SerialIter& sit, SField const& name) : STBase(name)
{
    // TODO have to look at the first few bits to see if this is STAmount
    // or STMPTAmount
}

STEitherAmount::STEitherAmount(STAmount const& amount)
{
    amount_ = amount;
}

STEitherAmount::STEitherAmount(STMPTAmount const& amount)
{
    amount_ = amount;
}

std::unique_ptr<STEitherAmount>
STEitherAmount::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STEitherAmount>(sit, name);
}

void
STEitherAmount::setJson(Json::Value& elem) const
{
    if (std::holds_alternative<STAmount>(amount_))
        return std::get<STAmount>(amount_).setJson(elem);
    return std::get<STMPTAmount>(amount_).setJson(elem);
}

//------------------------------------------------------------------------------
//
// STBase
//
//------------------------------------------------------------------------------

SerializedTypeID
STEitherAmount::getSType() const
{
    return STI_EITHER_AMOUNT;
}

std::string
STEitherAmount::getFullText() const
{
    if (std::holds_alternative<STAmount>(amount_))
        return std::get<STAmount>(amount_).getFullText();
    return std::get<STMPTAmount>(amount_).getFullText();
}

std::string
STEitherAmount::getText() const
{
    if (std::holds_alternative<STAmount>(amount_))
        return std::get<STAmount>(amount_).getText();
    return std::get<STMPTAmount>(amount_).getText();
}

Json::Value STEitherAmount::getJson(JsonOptions) const
{
    if (std::holds_alternative<STAmount>(amount_))
        return std::get<STAmount>(amount_).getJson(JsonOptions::none);
    return std::get<STMPTAmount>(amount_).getJson(JsonOptions::none);
}

void
STEitherAmount::add(Serializer& s) const
{
    if (std::holds_alternative<STAmount>(amount_))
        std::get<STAmount>(amount_).add(s);
    else
        std::get<STMPTAmount>(amount_).add(s);
}

bool
STEitherAmount::isEquivalent(const STBase& t) const
{
    if (std::holds_alternative<STAmount>(amount_))
        return std::get<STAmount>(amount_).isEquivalent(t);
    return std::get<STMPTAmount>(amount_).isEquivalent(t);
}

bool
STEitherAmount::isDefault() const
{
    if (std::holds_alternative<STAmount>(amount_))
        return std::get<STAmount>(amount_).isDefault();
    return std::get<STMPTAmount>(amount_).isDefault();
}

XRPAmount
STEitherAmount::xrp() const
{
    if (!native())
        Throw<std::runtime_error>("STEitherAmount is not XRP");
    return std::get<STAmount>(amount_).xrp();
}

IOUAmount
STEitherAmount::iou() const
{
    if (!isSTAmount() || !native())
        Throw<std::runtime_error>("STEitherAmount is not IOU");
    return std::get<STAmount>(amount_).iou();
}

std::uint64_t
STEitherAmount::mantissa() const
{
    if (isSTAmount())
        return std::get<STAmount>(amount_).mantissa();
    return std::get<STMPTAmount>(amount_).mantissa();
}

int
STEitherAmount::exponent() const
{
    if (isSTAmount())
        return std::get<STAmount>(amount_).exponent();
    return std::get<STMPTAmount>(amount_).exponent();
}

STEitherAmount
eitherAmountFromJson(SField const& name, Json::Value const& v)
{
    if (v.isMember(jss::mpt_issuance_id))
        return mptAmountFromJson(name, v);
    return amountFromJson(name, v);
}

bool
amountFromJsonNoThrow(STEitherAmount& result, Json::Value const& jvSource)
{
    try
    {
        result = eitherAmountFromJson(sfGeneric, jvSource);
        return true;
    }
    catch (const std::exception& e)
    {
        JLOG(debugLog().warn())
            << "amountFromJsonNoThrow: caught: " << e.what();
    }
    return false;
}

}  // namespace ripple
