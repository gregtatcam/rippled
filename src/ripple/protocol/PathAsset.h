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

#include <ripple/protocol/Asset.h>

namespace ripple {

class PathAsset
{
private:
    std::variant<Currency, MPT> easset_;

public:
    PathAsset(Asset const& asset)
    {
        if (asset.isIssue())
            easset_ = asset.issue().currency;
        else
            easset_ = asset.mptIssue().mpt();
    }
    PathAsset(Currency const& currency) : easset_(currency)
    {
    }
    PathAsset(MPT const& mpt) : easset_(mpt)
    {
    }
    PathAsset(uint192 const& mpt);
    PathAsset() = default;

    bool constexpr isCurrency() const
    {
        return std::holds_alternative<Currency>(easset_);
    }
    bool constexpr isXRP() const
    {
        return isCurrency() && ripple::isXRP(currency());
    }
    bool constexpr isMPT() const
    {
        return std::holds_alternative<MPT>(easset_);
    }
    Currency const&
    currency() const
    {
        if (!std::holds_alternative<Currency>(easset_))
            Throw<std::logic_error>("PathAsset is not Currency");
        return std::get<Currency>(easset_);
    }
    MPT const&
    mpt() const
    {
        if (!std::holds_alternative<MPT>(easset_))
            Throw<std::logic_error>("PathAsset is not MPT");
        return std::get<MPT>(easset_);
    }
    uint192
    getMptID() const;

    static PathAsset
    toPathAsset(Asset const& asset);

    static std::optional<PathAsset>
    toPathAsset(std::optional<Asset> const& asset);

    friend constexpr bool
    operator==(PathAsset const& lhs, PathAsset const& rhs)
    {
        // It's valid to have different type
        if (lhs.isCurrency() != rhs.isCurrency())
            return false;
        if (lhs.isCurrency())
            return lhs.currency() == rhs.currency();
        return lhs.mpt() == rhs.mpt();
    }
};

template <typename Hasher>
void
hash_append(Hasher& h, PathAsset const& a)
{
    if (a.isCurrency())
        hash_append(h, a.currency());
    else
        hash_append(h, a.getMptID());
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
