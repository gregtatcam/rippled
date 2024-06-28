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
#include <xrpl/protocol/MPTIssue.h>

namespace ripple {

MPTIssue::MPTIssue(MPT const& mpt) : mpt_(mpt)
{
}

AccountID const&
MPTIssue::getIssuer() const
{
    return mpt_.second;
}

MPT const&
MPTIssue::mpt() const
{
    return mpt_;
}

MPT&
MPTIssue::mpt()
{
    return mpt_;
}

uint192
MPTIssue::getMptID() const
{
    return ripple::getMptID(mpt_.second, mpt_.first);
}

}  // namespace ripple
