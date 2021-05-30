//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MAIN_CONFIGREQUESTOR_H_INCLUDED
#define RIPPLE_APP_MAIN_CONFIGREQUESTOR_H_INCLUDED

#include <ripple/protocol/PublicKey.h>
#include <optional>
#include <string>
#include <utility>

namespace ripple {

/** Request dynamically changing properties from Application
 */
class AppConfigRequestor
{
public:
    AppConfigRequestor() = default;
    virtual ~AppConfigRequestor() = default;
    virtual std::optional<std::string>
    clusterMember(PublicKey const& key) = 0;
    virtual bool
    reservedPeer(PublicKey const& key) = 0;
    virtual std::optional<std::pair<uint256, uint256>>
    clHashes() = 0;
};

}  // namespace ripple

#endif  // RIPPLE_APP_MAIN_CONFIGREQUESTOR_H_INCLUDED
