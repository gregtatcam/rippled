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

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PathAsset.h>

namespace ripple {

PathAsset
PathAsset::toPathAsset(const Asset& asset)
{
    return std::visit(
        [&]<typename TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                return PathAsset{issue.currency};
            else
                return PathAsset{issue.getMptID()};
        },
        asset.value());
}

std::optional<PathAsset>
PathAsset::toPathAsset(std::optional<Asset> const& asset)
{
    if (asset)
        return toPathAsset(*asset);
    return std::nullopt;
}

std::string
to_string(PathAsset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return to_string(issue); }, asset.value());
}

std::ostream&
operator<<(std::ostream& os, PathAsset const& x)
{
    os << to_string(x);
    return os;
}

bool
equalAssets(PathAsset const& asset1, Asset const& asset2)
{
    return std::visit(
        [&]<typename TPa, typename TIss>(
            TPa const& element, TIss const& issue) {
            if constexpr (
                std::is_same_v<TPa, Currency> && std::is_same_v<TIss, Issue>)
                return element == issue.currency;
            else if constexpr (
                std::is_same_v<TPa, MPTID> && std::is_same_v<TIss, MPTIssue>)
                return element == issue.getMptID();
            else
                return false;
        },
        asset1.value(),
        asset2.value());
}

bool
equalAssets(Asset const& asset1, PathAsset const& asset2)
{
    return equalAssets(asset2, asset1);
}

}  // namespace ripple
