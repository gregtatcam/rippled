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

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/STEitherAmount.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string.hpp>

namespace ripple {

STEitherAmount::STEitherAmount(SerialIter& sit, SField const& name)
    : STBase(name)
{
    auto const value = sit.get64();
    if ((value & STAmount::cNotNative) == 0 &&
        (value & STMPTAmount::cMPToken) != 0)
        amount_.emplace<STMPTAmount>(value, sit, name);
    else
        amount_.emplace<STAmount>(value, sit, name);
}

STEitherAmount::STEitherAmount(XRPAmount const& amount) : amount_{amount}
{
}

STEitherAmount::STEitherAmount(STAmount const& amount)
    : STBase(amount.getFName()), amount_{amount}
{
}

STEitherAmount::STEitherAmount(SField const& name, STAmount const& amount)
    : STBase(name), amount_{amount}
{
}

STEitherAmount::STEitherAmount(STMPTAmount const& amount)
    : STBase(amount.getFName()), amount_{amount}
{
}

STEitherAmount&
STEitherAmount::operator=(STAmount const& amount)
{
    setFName(amount.getFName());
    amount_ = amount;
    return *this;
}

STEitherAmount&
STEitherAmount::operator=(STMPTAmount const& amount)
{
    setFName(amount.getFName());
    amount_ = amount;
    return *this;
}

STEitherAmount&
STEitherAmount::operator=(XRPAmount const& amount)
{
    amount_ = amount;
    return *this;
}

SerializedTypeID
STEitherAmount::getSType() const
{
    return STI_AMOUNT;
}

std::string
STEitherAmount::getFullText() const
{
    return std::visit([&](auto&& a) { return a.getFullText(); }, amount_);
}

std::string
STEitherAmount::getText() const
{
    return std::visit([&](auto&& a) { return a.getText(); }, amount_);
}

Json::Value STEitherAmount::getJson(JsonOptions) const
{
    return std::visit(
        [&](auto&& a) { return a.getJson(JsonOptions::none); }, amount_);
}

void
STEitherAmount::setJson(Json::Value& jv) const
{
    std::visit([&](auto&& a) { a.setJson(jv); }, amount_);
}

void
STEitherAmount::add(Serializer& s) const
{
    std::visit([&](auto&& a) { a.add(s); }, amount_);
}

bool
STEitherAmount::isEquivalent(const STBase& t) const
{
    const STEitherAmount* v = dynamic_cast<const STEitherAmount*>(&t);
    return v &&
        std::visit(
               [&](auto&& a, auto&& a1) { return a.isEquivalent(a1); },
               amount_,
               v->amount_);
}

bool
STEitherAmount::isDefault() const
{
    return std::visit([&](auto&& a) { return a.isDefault(); }, amount_);
}
//------------------------------------------------------------------------------
bool
STEitherAmount::isMPT() const
{
    return std::holds_alternative<STMPTAmount>(amount_);
}

bool
STEitherAmount::isIssue() const
{
    return std::holds_alternative<STAmount>(amount_);
}

bool
STEitherAmount::badAsset() const
{
    if (isIssue())
        return badCurrency() == std::get<STAmount>(amount_).getCurrency();
    return badMPT() == std::get<STMPTAmount>(amount_).getCurrency();
}

bool
STEitherAmount::negative() const
{
    if (isIssue())
        return std::get<STAmount>(amount_).negative();
    return false;
}

bool
STEitherAmount::native() const
{
    if (isIssue())
        return std::get<STAmount>(amount_).native();
    return false;
}

STEitherAmount
STEitherAmount::zeroed() const
{
    return std::visit(
        [&](auto&& a) { return STEitherAmount{a.zeroed()}; }, amount_);
}

bool
STEitherAmount::sameAsset(STEitherAmount const& amount) const
{
    return std::visit(
        [&]<typename T1, typename T2>(T1&& a1, T2&& a2) {
            if constexpr (std::is_same_v<T1, T2>)
                return a1.getCurrency() == a2.getCurrency();
            else
                return false;
        },
        amount_,
        amount.amount_);
}

bool
STEitherAmount::sameIssue(STEitherAmount const& amount) const
{
    return std::visit(
        [&]<typename T1, typename T2>(T1&& a1, T2&& a2) {
            if constexpr (std::is_same_v<T1, T2>)
                return a1.issue() == a2.issue();
            else
                return false;
        },
        amount_,
        amount.amount_);
}

STEitherAmount const&
STEitherAmount::value() const
{
    return *this;
}

std::variant<STAmount, STMPTAmount> const&
STEitherAmount::getValue() const
{
    return amount_;
}

std::variant<STAmount, STMPTAmount>&
STEitherAmount::getValue()
{
    return amount_;
}

AccountID const&
STEitherAmount::getIssuer() const
{
    if (isIssue())
        return get<STAmount>().getIssuer();
    return get<STMPTAmount>().getIssuer();
}

STBase*
STEitherAmount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STEitherAmount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

int
STEitherAmount::signum() const noexcept
{
    return std::visit([&](auto&& a) { return a.signum(); }, amount_);
}

static bool
validJSONIssue(Json::Value const& jv)
{
    return (jv.isMember(jss::currency) && !jv.isMember(jss::mpt_issuance_id)) ||
        (!jv.isMember(jss::currency) && !jv.isMember(jss::issuer) &&
         jv.isMember(jss::mpt_issuance_id));
}

STEitherAmount
amountFromJson(SField const& name, Json::Value const& v)
{
    STAmount::mantissa_type mantissa = 0;
    STAmount::exponent_type exponent = 0;
    bool negative = false;
    std::variant<Issue, MPTIssue> issue;

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
        if (!validJSONIssue(v))
            Throw<std::runtime_error>("Invalid Issue's Json specification");

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
        issue = xrpIssue();
    }
    else
    {
        if (isMPT)
        {
            // sequence (32 bits) + account (160 bits)
            uint192 u;
            if (!u.parseHex(currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid MPTokenIssuanceID");
            issue = u;
        }
        else
        {
            issue = Issue{};
            if (!to_currency(
                    std::get<Issue>(issue).currency,
                    currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid currency");
            if (!issuer.isString() ||
                !to_issuer(std::get<Issue>(issue).account, issuer.asString()))
                Throw<std::runtime_error>("invalid issuer");
            if (isXRP(std::get<Issue>(issue)))
                Throw<std::runtime_error>("invalid issuer");
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
        if (std::holds_alternative<Issue>(issue))
        {
            STAmount const ret =
                amountFromString(std::get<Issue>(issue), value.asString());
            mantissa = ret.mantissa();
            exponent = ret.exponent();
            negative = ret.negative();
        }
        else
        {
            STMPTAmount const ret =
                amountFromString(std::get<MPTIssue>(issue), value.asString());
            mantissa = ret.value();
            exponent = 0;
            negative = false;
        }
    }
    else
    {
        Throw<std::runtime_error>("invalid amount type");
    }

    if (std::holds_alternative<Issue>(issue))
        return STAmount{
            name, std::get<Issue>(issue), mantissa, exponent, native, negative};
    while (exponent-- > 0)
        mantissa *= 10;
    if (mantissa > 0x8000000000000000)
        Throw<std::runtime_error>("MPT amount out of range");
    return STMPTAmount{
        name, std::get<MPTIssue>(issue), static_cast<std::int64_t>(mantissa)};
}

bool
amountFromJsonNoThrow(STEitherAmount& result, Json::Value const& jvSource)
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

bool
amountFromJsonNoThrow(STAmount& result, Json::Value const& jvSource)
{
    try
    {
        STEitherAmount amount;
        const bool res = amountFromJsonNoThrow(amount, jvSource);
        if (res)
            result = get<STAmount>(amount);
        return res;
    }
    catch (const std::exception& e)
    {
        JLOG(debugLog().warn())
            << "amountFromJsonNoThrow: caught: " << e.what();
    }
    return false;
}

}  // namespace ripple
