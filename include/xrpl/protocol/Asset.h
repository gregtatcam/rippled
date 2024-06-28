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

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>

namespace ripple {

class Asset
{
private:
    std::variant<Issue, MPTIssue> asset_;

public:
    Asset() = default;

    Asset(Issue const& issue);

    Asset(MPTIssue const& mpt);

    Asset(MPT const& mpt);

    Asset(uint192 const& mptID);

    explicit operator Issue() const;

    explicit operator MPTIssue() const;

    AccountID const&
    getIssuer() const;

    constexpr Issue const&
    issue() const;

    Issue&
    issue();

    constexpr MPTIssue const&
    mptIssue() const;

    MPTIssue&
    mptIssue();

    constexpr bool
    isMPT() const;

    constexpr bool
    isIssue() const;

    std::string
    getText() const;

    friend constexpr bool
    operator==(Asset const& lhs, Asset const& rhs);

    friend constexpr bool
    operator!=(Asset const& lhs, Asset const& rhs);
};

constexpr bool
Asset::isMPT() const
{
    return std::holds_alternative<MPTIssue>(asset_);
}

constexpr bool
Asset::isIssue() const
{
    return std::holds_alternative<Issue>(asset_);
}

constexpr Issue const&
Asset::issue() const
{
    if (!std::holds_alternative<Issue>(asset_))
        Throw<std::logic_error>("Asset is not Issue");
    return std::get<Issue>(asset_);
}

constexpr MPTIssue const&
Asset::mptIssue() const
{
    if (!std::holds_alternative<MPTIssue>(asset_))
        Throw<std::logic_error>("Asset is not MPT");
    return std::get<MPTIssue>(asset_);
}

constexpr bool
operator==(Asset const& lhs, Asset const& rhs)
{
    if (lhs.isIssue() != rhs.isIssue())
        Throw<std::logic_error>("Assets are not comparable");
    if (lhs.isIssue())
        return lhs.issue() == rhs.issue();
    return lhs.mptIssue() == lhs.mptIssue();
}

constexpr bool
operator!=(Asset const& lhs, Asset const& rhs)
{
    return !(lhs == rhs);
}

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
