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
    Asset(MPTIssue const& mpt) : asset_(mpt)
    {
    }
    Asset(MPT const& mpt) : asset_(MPTIssue{mpt})
    {
    }
    Asset(uint192 const& mptID);
    Asset() = default;

    explicit operator Issue const&() const
    {
        return issue();
    }

    explicit operator MPTIssue const&() const
    {
        return mptIssue();
    }

    explicit
    operator Issue&()
    {
        return issue();
    }

    explicit
    operator MPTIssue&()
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

    AccountID const&
    getIssuer() const;

    friend constexpr bool
    operator==(Asset const& lhs, Asset const& rhs)
    {
        if (lhs.isIssue() != rhs.isIssue())
            Throw<std::logic_error>("Assets are not comparable");
        if (lhs.isIssue())
            return lhs.issue() == rhs.issue();
        return lhs.mptIssue() == lhs.mptIssue();
    }

    friend constexpr bool
    operator!=(Asset const& lhs, Asset const& rhs)
    {
        return !(lhs == rhs);
    }

    bool
    badAsset() const;
};

std::string
to_string(Asset const& asset);

std::string
to_string(MPTIssue const& mpt);

std::string
to_string(MPT const& mpt);

bool
validJSONAsset(Json::Value const& jv);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ASSET_H_INCLUDED
