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
#include <ripple/protocol/MPTIssue.h>

namespace ripple {

uint192
MPTIssue::getMptID() const
{
    uint192 u;
    auto const sequence = boost::endian::native_to_big(mpt_.first);
    memcpy(u.data(), &sequence, sizeof(sequence));
    memcpy(u.data() + sizeof(sequence), mpt_.second.data(), sizeof(mpt_.second));
    return u;
}

uint192
MPTIssue::getAssetID() const
{
    return getMptID();
}

MPT
getMPT(uint192 const& u)
{
    std::uint32_t sequence;
    AccountID account;
    memcpy(&sequence, u.data(), sizeof(sequence));
    sequence = boost::endian::big_to_native(sequence);
    memcpy(account.data(), u.data() + sizeof(sequence), sizeof(AccountID));
    return std::make_pair(sequence, account);
}

}  // namespace ripple
