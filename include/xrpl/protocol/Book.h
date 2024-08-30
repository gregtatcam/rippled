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

#ifndef RIPPLE_PROTOCOL_BOOK_H_INCLUDED
#define RIPPLE_PROTOCOL_BOOK_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/CommonConstraints.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <boost/utility/base_from_member.hpp>

namespace ripple {

/** Specifies an order book.
    The order book is a pair of Issues called in and out.
    @see Issue.
*/
class Book final : public CountedObject<Book>
{
public:
    std::variant<Issue, MPTIssue> in;
    std::variant<Issue, MPTIssue> out;

    Book()
    {
    }

    template <typename TIn, typename TOut>
    Book(TIn const& in_, TOut const& out_) : in(in_), out(out_)
    {
    }
};

bool
isConsistent(Book const& book);

std::string
to_string(Book const& book);

std::ostream&
operator<<(std::ostream& os, Book const& x);

template <class Hasher>
void
hash_append(Hasher& h, Book const& b)
{
    using beast::hash_append;
    std::visit(
        [&](auto&& in, auto&& out) { hash_append(h, in, out); }, b.in, b.out);
}

Book
reversed(Book const& book);

/** Equality comparison. */
/** @{ */
[[nodiscard]] inline constexpr bool
operator==(Book const& lhs, Book const& rhs)
{
    return (lhs.in == rhs.in) && (lhs.out == rhs.out);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
[[nodiscard]] inline constexpr std::weak_ordering
operator<=>(Book const& lhs, Book const& rhs)
{
    return std::visit(
        [&]<typename LIn, typename LOut, typename RIn, typename ROut>(
            LIn&& lin, LOut&& lout, RIn& rin, ROut&& rout) {
            if constexpr (
                std::is_same_v<LIn, RIn> && std::is_same_v<LOut, ROut>)
            {
                if (auto const c{lin <=> rin}; c != 0)
                    return c;
                return lout <=> rout;
            }
            else
                return std::weak_ordering::less;
        },
        lhs.in,
        lhs.out,
        rhs.in,
        rhs.out);
}
/** @} */

}  // namespace ripple

//------------------------------------------------------------------------------

namespace std {

template <>
struct hash<ripple::Issue>
    : private boost::base_from_member<std::hash<ripple::Currency>, 0>,
      private boost::base_from_member<std::hash<ripple::AccountID>, 1>
{
private:
    using currency_hash_type =
        boost::base_from_member<std::hash<ripple::Currency>, 0>;
    using issuer_hash_type =
        boost::base_from_member<std::hash<ripple::AccountID>, 1>;

public:
    explicit hash() = default;

    using value_type = std::size_t;
    using argument_type = ripple::Issue;

    value_type
    operator()(argument_type const& value) const
    {
        value_type result(currency_hash_type::member(value.currency));
        if (!isXRP(value.currency))
            boost::hash_combine(
                result, issuer_hash_type::member(value.account));
        return result;
    }
};

template <>
struct hash<ripple::MPTIssue>
{
public:
    explicit hash() = default;
    using value_type = std::size_t;
    using argument_type = ripple::MPTIssue;

    value_type
    operator()(argument_type const& value) const
    {
        return ::beast::uhash<>{}(value.getMptID());
    }
};

//------------------------------------------------------------------------------

template <>
struct hash<ripple::Book>
{
private:
    template <ripple::ValidIssueType Iss>
    struct hasher
    {
        std::size_t
        operator()(Iss const& issue) const
        {
            return hash<Iss>{}(issue);
        }
    };

public:
    explicit hash() = default;

    using value_type = std::size_t;
    using argument_type = ripple::Book;

    value_type
    operator()(argument_type const& value) const
    {
        return std::visit(
            [&]<typename TIn, typename TOut>(TIn const& in, TOut const& out) {
                value_type result(hasher<TIn>()(in));
                boost::hash_combine(result, hasher<TOut>()(out));
                return result;
            },
            value.in,
            value.out);
    }
};

}  // namespace std

//------------------------------------------------------------------------------

namespace boost {

template <>
struct hash<ripple::Issue> : std::hash<ripple::Issue>
{
    explicit hash() = default;

    using Base = std::hash<ripple::Issue>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

template <>
struct hash<ripple::Book> : std::hash<ripple::Book>
{
    explicit hash() = default;

    using Base = std::hash<ripple::Book>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

}  // namespace boost

#endif
