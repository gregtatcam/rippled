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

#ifndef RIPPLE_APP_PATHASSET_H_INCLUDED
#define RIPPLE_APP_PATHASSET_H_INCLUDED

#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>

namespace ripple {

/* Represent STPathElement's asset, which can be Currency or MPTID.
 */
class PathIssue
{
private:
    std::variant<Currency, MPTID> easset_;

public:
    PathIssue() = default;
    // Enables comparing Asset and PathIssue
    PathIssue(Issue const& asset);
    PathIssue(Currency const& currency) : easset_(currency)
    {
    }
    PathIssue(MPTID const& mpt) : easset_(mpt)
    {
    }

    template <ValidPathIssue T>
    constexpr bool
    holds() const;

    constexpr bool
    isXRP() const;

    template <ValidPathIssue T>
    T const&
    get() const;

    constexpr std::variant<Currency, MPTID> const&
    value() const;

    friend constexpr bool
    operator==(PathIssue const& lhs, PathIssue const& rhs);
};

inline PathIssue::PathIssue(Issue const& asset)
{
    std::visit(
        [&]<typename TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, IOUIssue>)
                easset_ = issue.getCurrency();
            else
                easset_ = issue.getMptID();
        },
        asset.value());
}

template <ValidPathIssue T>
constexpr bool
PathIssue::holds() const
{
    return std::holds_alternative<T>(easset_);
}

template <ValidPathIssue T>
T const&
PathIssue::get() const
{
    if (!holds<T>())
        Throw<std::runtime_error>("PathIssue doesn't hold requested asset.");
    return std::get<T>(easset_);
}

constexpr std::variant<Currency, MPTID> const&
PathIssue::value() const
{
    return easset_;
}

constexpr bool
PathIssue::isXRP() const
{
    return std::visit(
        [&]<ValidPathIssue A>(A const& a) { return ripple::isXRP(a); },
        easset_);
}

constexpr bool
operator==(PathIssue const& lhs, PathIssue const& rhs)
{
    return std::visit(
        []<ValidPathIssue TLhs, ValidPathIssue TRhs>(
            TLhs const& lhs_, TRhs const& rhs_) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
                return lhs_ == rhs_;
            else
                return false;
        },
        lhs.value(),
        rhs.value());
}

template <typename Hasher>
void
hash_append(Hasher& h, PathIssue const& pathAsset)
{
    std::visit(
        [&]<typename T>(T const& e) { hash_append(h, e); }, pathAsset.value());
}

inline bool
isXRP(PathIssue const& asset)
{
    return asset.isXRP();
}

std::string
to_string(PathIssue const& asset);

std::ostream&
operator<<(std::ostream& os, PathIssue const& x);

}  // namespace ripple

#endif  // RIPPLE_APP_PATHASSET_H_INCLUDED
