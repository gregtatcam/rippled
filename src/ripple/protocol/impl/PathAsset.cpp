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

#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/PathAsset.h>

namespace ripple {

PathAsset::PathAsset(const uint192& u) : easset_(getMPT(u))
{
}

uint192
PathAsset::getMptID() const
{
    return ripple::getMptID(mpt());
}

PathAsset
PathAsset::toPathAsset(const Asset& asset)
{
    if (asset.isIssue())
        return asset.issue().currency;
    return asset.mptIssue().mpt();
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
    if (asset.isCurrency())
        return to_string(asset.currency());
    return to_string(asset.getMptID());
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
    if (asset1.isCurrency() != asset2.isIssue())
        return false;
    if (asset1.isCurrency())
        return asset1.currency() == asset2.issue().currency;
    return asset1.mpt() == asset2.mptIssue().mpt();
}

bool
equalAssets(Asset const& asset1, PathAsset const& asset2)
{
    if (asset1.isIssue() != asset2.isCurrency())
        return false;
    if (asset1.isIssue())
        return asset1.issue().currency == asset2.currency();
    return asset1.mptIssue().mpt() == asset2.mpt();
}

}  // namespace ripple
