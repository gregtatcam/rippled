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

#include <xrpl/protocol/Book.h>

namespace ripple {

bool
isConsistent(Book const& book)
{
    return std::visit(
        [&](auto&& in, auto&& out) {
            bool constexpr same = std::is_same_v<decltype(in), decltype(out)>;
            return isConsistent(in) && isConsistent(out) &&
                (!same || book.in != book.out);
        },
        book.in,
        book.out);
}

std::string
to_string(Book const& book)
{
    return std::visit(
        [&](auto&& in, auto&& out) {
            return to_string(in) + "->" + to_string(out);
        },
        book.in,
        book.out);
}

std::ostream&
operator<<(std::ostream& os, Book const& x)
{
    os << to_string(x);
    return os;
}

Book
reversed(Book const& book)
{
    return std::visit(
        [&](auto const& in, auto const& out) { return Book(out, in); },
        book.in,
        book.out);
}

}  // namespace ripple
