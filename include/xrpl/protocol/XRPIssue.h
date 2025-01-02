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

#ifndef RIPPLE_PROTOCOL_XRPISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_XRPISSUE_H_INCLUDED

#include <xrpl/json/json_errors.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class XRPAmount;

class XRPIssue
{
public:
    using amount_type = XRPAmount;
    static inline AccountID const account = xrpAccount();
    static inline Currency const currency = xrpCurrency();
    XRPIssue() = default;

    AccountID const&
    getIssuer() const
    {
        return account;
    }

    std::string
    getText() const
    {
        return to_string(currency);
    }

    void
    setJson(Json::Value& jv) const
    {
        jv[jss::currency] = to_string(currency);
    }

    bool
    native() const
    {
        return true;
    }

    friend constexpr bool
    operator==(XRPIssue const& lhs, XRPIssue const& rhs)
    {
        return true;
    }

    friend constexpr std::weak_ordering
    operator<=>(XRPIssue const& lhs, XRPIssue const& rhs)
    {
        return std::weak_ordering::equivalent;
    }
};

inline bool
isConsistent(XRPIssue const& ac)
{
    return true;
}

inline Json::Value
to_json(XRPIssue const& is)
{
    Json::Value jv;
    jv[jss::currency] = to_string(is.currency);
    return jv;
}

inline std::string
to_string(XRPIssue const& is)
{
    return to_string(is.currency);
}

inline XRPIssue
xrpIssueFromJson(Json::Value const& v)
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "xrpIssueFromJson can only be specified with an 'object' Json "
            "value");
    }

    if (v.isMember(jss::account) || v.isMember(jss::mpt_issuance_id))
    {
        Throw<std::runtime_error>(
            "xrpIssueFromJson, IOUIssue should not have mpt_issuance_id");
    }

    Json::Value const curStr = v[jss::currency];
    if (!curStr.isString())
    {
        Throw<Json::error>(
            "xrpIssueFromJson currency must be a string Json value");
    }

    auto const currency = to_currency(curStr.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<Json::error>(
            "xrpIssueFromJson currency must be a valid currency");
    }

    return XRPIssue();
}

inline std::ostream&
operator<<(std::ostream& os, XRPIssue const& x)
{
    os << to_string(x.currency);
    return os;
}

template <class Hasher>
void
hash_append(Hasher& h, XRPIssue const& r)
{
    using beast::hash_append;
    hash_append(h, r.currency, r.account);
}

/** Returns an asset specifier that represents XRP. */
inline XRPIssue const&
xrpIssue()
{
    static XRPIssue issue;
    return issue;
}

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_XRPISSUE_H_INCLUDED
