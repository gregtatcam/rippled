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

#ifndef RIPPLE_OVERLAY_PEERIMPFACTORY_H_INCLUDED
#define RIPPLE_OVERLAY_PEERIMPFACTORY_H_INCLUDED

#include <boost/asio.hpp>
#include <boost/beast/core/multi_buffer.hpp>

namespace ripple {

namespace {
using middle_type = boost::beast::tcp_stream;
using stream_type = boost::beast::ssl_stream<middle_type>;
}  // namespace

template <typename PeerImplmnt>
class P2POverlayInternal
{
public:
    virtual ~P2POverlayInternal() = default;

protected:
    // HOOKS implemented in the app layer

    /** Handles non-peer protocol requests.
     @return true if the request was handled.
    */
    virtual bool
    processRequest(http_request_type const& req, Handoff& handoff) = 0;

    /** Creates an inbound peer instance.
     @return inbound peer instance
     */
    virtual std::shared_ptr<PeerImplmnt>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        Resource::Consumer consumer,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr) = 0;

    /** Creates an outbound peer instance.
     @return outbound peer instance
     */
    virtual std::shared_ptr<PeerImplmnt>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) = 0;
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_PEERIMPFACTORY_H_INCLUDED
