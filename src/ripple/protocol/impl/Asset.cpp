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

#include <ripple/protocol/Asset.h>

namespace ripple {

void
Asset::addBitString(ripple::Serializer& s) const
{
    if (isCurrency())
        s.addBitString(std::get<Currency>(asset_));
    else
    {
        s.add32(std::get<MPT>(asset_).first);
        s.addBitString(std::get<MPT>(asset_).second);
    }
}

Asset::operator Currency const&() const
{
    assert(std::holds_alternative<Currency>(asset_));
    if (!std::holds_alternative<Currency>(asset_))
        Throw<std::logic_error>("Invalid Currency cast");
    return std::get<Currency>(asset_);
}

Asset::operator MPT const&() const
{
    // assert(std::holds_alternative<MPT>(asset_));
    if (!std::holds_alternative<MPT>(asset_))
        Throw<std::logic_error>("Invalid MPT cast");
    return std::get<MPT>(asset_);
}

std::string
to_string(Asset const& a)
{
    if (a.isCurrency())
        return to_string((Currency&)a);
    // TODO, common getMptID()
    uint192 u;
    auto const sequence =
        boost::endian::native_to_big(std::get<MPT>(a.asset_).first);
    auto const& account = std::get<MPT>(a.asset_).second;
    memcpy(u.data(), &sequence, sizeof(sequence));
    memcpy(u.data() + sizeof(sequence), account.data(), sizeof(account));
    return to_string(u);
}

}  // namespace ripple
