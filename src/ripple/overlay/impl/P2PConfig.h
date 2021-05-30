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

#ifndef RIPPLE_OVERLAY_P2PCONFIG_H_INCLUDED
#define RIPPLE_OVERLAY_P2PCONFIG_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/core/Config.h>
#include <ripple/overlay/impl/AppConfigRequestor.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>

namespace ripple {

/** Peer-2-peer layer properties
 */
struct P2PConfig
{
    Config const& config;
    Logs& logs;
    bool isValidator;
    std::pair<PublicKey, SecretKey> const& identity;
    NetClock::time_point now;
    AppConfigRequestor& requestor;
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_P2PCONFIG_H_INCLUDED
