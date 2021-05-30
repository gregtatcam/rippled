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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/base_uint.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/overlay/impl/AppConfigRequestor.h>

#ifndef RIPPLE_OVERLAY_APPCONFIGREQUESTORIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_APPCONFIGREQUESTORIMPL_H_INCLUDED

namespace ripple {

class AppConfigRequestorImpl : public AppConfigRequestor
{
private:
    Application& app_;

public:
    AppConfigRequestorImpl(Application& app) : app_(app)
    {
    }

    std::optional<std::string>
    clusterMember(PublicKey const& key) override
    {
        return app_.cluster().member(key);
    }

    bool
    reservedPeer(PublicKey const& key) override
    {
        return app_.peerReservations().contains(key);
    }

    std::optional<std::pair<uint256, uint256>>
    clHashes() override
    {
        if (auto cl = app_.getLedgerMaster().getClosedLedger())
            return std::make_pair(cl->info().hash, cl->info().parentHash);
        return std::nullopt;
    }
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_APPCONFIGREQUESTORIMPL_H_INCLUDED
