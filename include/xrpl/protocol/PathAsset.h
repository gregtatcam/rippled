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

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>

namespace ripple {

class PathAsset
{
private:
    std::variant<Currency, MPTID> easset_;

public:
    PathAsset() = default;
    PathAsset(Asset const& asset);
    PathAsset(Currency const& currency) : easset_(currency)
    {
    }
    PathAsset(MPTID const& mpt) : easset_(mpt)
    {
    }

    template <ValidPathAsset T>
    constexpr bool
    holds() const;

    constexpr bool
    isXRP() const;

    template <ValidPathAsset T>
    T const&
    get() const;

    constexpr std::variant<Currency, MPTID> const&
    value() const;

    static PathAsset
    toPathAsset(Asset const& asset);

    static std::optional<PathAsset>
    toPathAsset(std::optional<Asset> const& asset);

    friend constexpr bool
    operator==(PathAsset const& lhs, PathAsset const& rhs);
};

inline PathAsset::PathAsset(Asset const& asset)
{
    std::visit(
        [&]<typename TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                easset_ = issue.currency;
            else
                easset_ = issue.getMptID();
        },
        asset.value());
}

template <ValidPathAsset T>
constexpr bool
PathAsset::holds() const
{
    return std::holds_alternative<T>(easset_);
}

template <ValidPathAsset T>
T const&
PathAsset::get() const
{
    if (!holds<T>())
        Throw<std::runtime_error>("PathAsset doesn't hold requested asset.");
    return std::get<T>(easset_);
}

constexpr std::variant<Currency, MPTID> const&
PathAsset::value() const
{
    return easset_;
}

constexpr bool
PathAsset::isXRP() const
{
    return holds<Currency>() && get<Currency>() == xrpCurrency();
}

constexpr bool
operator==(PathAsset const& lhs, PathAsset const& rhs)
{
    return std::visit(
        []<ValidPathAsset TLhs, ValidPathAsset TRhs>(
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
hash_append(Hasher& h, PathAsset const& pathAsset)
{
    std::visit(
        [&]<typename T>(T const& e) { hash_append(h, e); }, pathAsset.value());
}

inline bool
isXRP(PathAsset const& asset)
{
    return asset.isXRP();
}

std::string
to_string(PathAsset const& asset);

std::ostream&
operator<<(std::ostream& os, PathAsset const& x);

bool
equalAssets(PathAsset const& asset1, Asset const& asset2);

bool
equalAssets(Asset const& asset1, PathAsset const& asset2);

}  // namespace ripple

#endif  // RIPPLE_APP_PATHASSET_H_INCLUDED
