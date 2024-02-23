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

#ifndef RIPPLE_PROTOCOL_MPTISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_MPTISSUE_H_INCLUDED

#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/UintTypes.h>

namespace ripple {

class MPTIssue
{
private:
    MPT mpt_;

public:
    MPTIssue(MPT const& mpt) : mpt_(mpt)
    {
    }

    AccountID const&
    account() const
    {
        return mpt_.second;
    }

    std::uint32_t
    sequence() const
    {
        return mpt_.first;
    }

    MPT const&
    mpt() const
    {
        return mpt_;
    }

    MPT&
    mpt()
    {
        return mpt_;
    }

    uint192
    getMptID() const;

    friend constexpr bool
    operator==(MPTIssue const& lhs, MPTIssue const& rhs)
    {
        return lhs.mpt_ == rhs.mpt_;
    }
};

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_MPTISSUE_H_INCLUDED
