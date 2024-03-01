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

#ifndef RIPPLE_PROTOCOL_ASSET_H_INCLUDED
#define RIPPLE_PROTOCOL_ASSET_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/MPTIssue.h>

namespace ripple {

class Asset
{
private:
    std::variant<Issue, MPTIssue> asset_;

public:
    Asset(Issue const& issue) : asset_(issue)
    {
    }
    Asset(Currency const& currency, AccountID const& account)
        : asset_(Issue{currency, account})
    {
    }
    Asset(MPTIssue const& mpt) : asset_(mpt)
    {
    }
    Asset(MPT const& mpt) : asset_(MPTIssue{mpt})
    {
    }
    Asset(uint192 const& mptID);
    Asset() = default;

    operator Issue() const
    {
        return issue();
    }

    operator MPTIssue() const
    {
        return mptIssue();
    }

    constexpr Issue const&
    issue() const
    {
        if (!std::holds_alternative<Issue>(asset_))
            Throw<std::logic_error>("Asset is not Issue");
        return std::get<Issue>(asset_);
    }
    Issue&
    issue()
    {
        if (!std::holds_alternative<Issue>(asset_))
            Throw<std::logic_error>("Asset is not Issue");
        return std::get<Issue>(asset_);
    }

    constexpr MPTIssue const&
    mptIssue() const
    {
        if (!std::holds_alternative<MPTIssue>(asset_))
            Throw<std::logic_error>("Asset is not MPT");
        return std::get<MPTIssue>(asset_);
    }
    MPTIssue&
    mptIssue()
    {
        if (!std::holds_alternative<MPTIssue>(asset_))
            Throw<std::logic_error>("Asset is not MPT");
        return std::get<MPTIssue>(asset_);
    }

    constexpr AccountID const&
    account() const
    {
        if (isIssue())
            return issue().account;
        return mptIssue().account();
    }

    constexpr bool
    isMPT() const
    {
        return std::holds_alternative<MPTIssue>(asset_);
    }

    constexpr bool
    isIssue() const
    {
        return std::holds_alternative<Issue>(asset_);
    }

    std::string
    getText() const;

    std::variant<Issue, MPTIssue> const&
    value() const
    {
        return asset_;
    }

    friend constexpr bool
    operator==(Asset const& lhs, Asset const& rhs)
    {
        // it's valid to compare assets of different types.
        // for instance, in book step with MPT/IOU offer
        if (lhs.isIssue() != rhs.isIssue())
            return false;
        if (lhs.isIssue())
            return lhs.issue() == rhs.issue();
        return lhs.mptIssue() == rhs.mptIssue();
    }

    friend constexpr bool
    operator!=(Asset const& lhs, Asset const& rhs)
    {
        return !(lhs == rhs);
    }

    friend constexpr std::weak_ordering
    operator<=>(Asset const& lhs, Asset const& rhs)
    {
        // it's possible to have incompatible types.
        // for instance, in minmax or container search
        // treat issue as greater.
        if (lhs.isIssue() && rhs.isMPT())
            return std::weak_ordering::greater;
        if (lhs.isMPT() && rhs.isIssue())
            return std::weak_ordering::less;
        if (lhs.isIssue())
        {
            if (auto const c{lhs.issue() <=> rhs.issue()}; c != 0)
                return c;
            return lhs.issue() <=> rhs.issue();
        }
        if (auto const c{lhs.mptIssue() <=> rhs.mptIssue()}; c != 0)
            return c;
        return lhs.mptIssue() <=> rhs.mptIssue();
    }

    friend bool
    isXRP(Asset const& asset)
    {
        return asset.isIssue() && isXRP(asset.issue());
    }
};

std::string
to_string(Asset const& asset);

std::string
to_string(MPTIssue const& mpt);

std::string
to_string(MPT const& mpt);

Json::Value
toJson();

Asset
assetFromJson(Json::Value const& jv);

bool
isConsistent(Asset const& asset);

std::ostream&
operator<<(std::ostream& os, Asset const& x);

template <typename Hasher>
void
hash_append(Hasher& h, Asset const& a)
{
    if (a.isIssue())
        hash_append(h, a.issue());
    else
        hash_append(h, a.mptIssue().getMptID());
}

Json::Value
to_json(Asset const& asset);

bool
validAsset(Asset const& asset);

// When comparing assets from path finding perspective,
// should compare currencies if assets represent Issue
// to take into account rippling
bool
equalAssets(Asset const& asset1, Asset const& asset2);

bool
validJSONAsset(Json::Value const& jv);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ASSET_H_INCLUDED
