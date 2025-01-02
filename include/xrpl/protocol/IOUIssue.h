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

#ifndef RIPPLE_PROTOCOL_IOUISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_IOUISSUE_H_INCLUDED

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/UintTypes.h>

#include <functional>
#include <type_traits>

namespace ripple {

/** A currency issued by an account.
    @see Currency, AccountID, Issue, Book
*/
class IOUIssue
{
private:
    Currency currency_;
    AccountID account_;

public:
    IOUIssue() = default;

    IOUIssue(Currency const& c, AccountID const& a) : currency_(c), account_(a)
    {
    }

    AccountID const&
    getIssuer() const
    {
        return account_;
    }

    void
    setIssuer(AccountID const& account)
    {
        account_ = account;
    }

    Currency const&
    getCurrency() const
    {
        return currency_;
    }

    void
    setCurrency(Currency const& currency)
    {
        currency_ = currency;
    }

    std::string
    getText() const;

    void
    setJson(Json::Value& jv) const;

    bool
    native() const;

    friend constexpr std::weak_ordering
    operator<=>(IOUIssue const& lhs, IOUIssue const& rhs);
};

bool
isConsistent(IOUIssue const& ac);

std::string
to_string(IOUIssue const& ac);

Json::Value
to_json(IOUIssue const& is);

IOUIssue
iouIssueFromJson(Json::Value const& v);

std::ostream&
operator<<(std::ostream& os, IOUIssue const& x);

template <class Hasher>
void
hash_append(Hasher& h, IOUIssue const& r)
{
    using beast::hash_append;
    hash_append(h, r.getCurrency(), r.getIssuer());
}

/** Equality comparison. */
/** @{ */
[[nodiscard]] inline constexpr bool
operator==(IOUIssue const& lhs, IOUIssue const& rhs)
{
    return (lhs.getCurrency() == rhs.getCurrency()) &&
        (isXRP(lhs.getCurrency()) || lhs.getIssuer() == rhs.getIssuer());
}
/** @} */

/** Strict weak ordering. */
/** @{ */
[[nodiscard]] constexpr std::weak_ordering
operator<=>(IOUIssue const& lhs, IOUIssue const& rhs)
{
    if (auto const c{lhs.getCurrency() <=> rhs.getCurrency()}; c != 0)
        return c;

    if (isXRP(lhs.getCurrency()))
        return std::weak_ordering::equivalent;

    return (lhs.getIssuer() <=> rhs.getIssuer());
}
/** @} */

//------------------------------------------------------------------------------

/** Returns an asset specifier that represents XRP. */
inline IOUIssue const&
xrpIssue()
{
    static IOUIssue issue{xrpCurrency(), xrpAccount()};
    return issue;
}

/** Returns an asset specifier that represents no account and currency. */
inline IOUIssue const&
noIssue()
{
    static IOUIssue issue{noCurrency(), noAccount()};
    return issue;
}

inline bool
isXRP(IOUIssue const& issue)
{
    return issue.native();
}

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_IOUISSUE_H_INCLUDED
