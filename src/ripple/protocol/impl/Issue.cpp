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

#include <ripple/protocol/Issue.h>

#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>

namespace ripple {

bool
isConsistent(Issue const& ac)
{
    return isXRP(ac.currency) == isXRP(ac.account);
}

std::string
to_string(Issue const& ac)
{
    if (isXRP(ac.account))
        return to_string(ac.currency);

    return to_string(ac.account) + "/" + to_string(ac.currency);
}

Json::Value
to_json(Issue const& is)
{
    if (isXRP(is.account))
        return Json::Value{to_string(is.currency)};

    Json::Value jv;
    jv[jss::currency] = to_string(is.currency);
    jv[jss::issuer] = toBase58(is.account);
    return jv;
}

Issue
issueFromJson(Json::Value const& v)
{
    if (v.isString())
    {
        if (v.asString() == "XRP")
        {
            return xrpIssue();
        }
        else
        {
            Throw<std::runtime_error>(
                "issueFromJson string values can only be 'XRP'");
        }
    }

    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "issueFromJson can only be specified with a 'object' or string "
            "Json value");
    }

    Json::Value const curStr = v[jss::currency];
    Json::Value const issStr = v[jss::issuer];

    if (!curStr.isString())
    {
        Throw<std::runtime_error>(
            "issueFromJson currency must be a string Json value");
    }
    if (!issStr.isString())
    {
        Throw<std::runtime_error>(
            "issueFromJson issuer must be a string Json value");
    }
    auto const issuer = parseBase58<AccountID>(issStr.asString());
    auto const currency = to_currency(curStr.asString());

    if (!issuer)
    {
        Throw<std::runtime_error>(
            "issueFromJson issuer must be a valid account");
    }

    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<std::runtime_error>(
            "issueFromJson currency must be a valid currency");
    }

    return Issue{currency, *issuer};
}

std::ostream&
operator<<(std::ostream& os, Issue const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace ripple
