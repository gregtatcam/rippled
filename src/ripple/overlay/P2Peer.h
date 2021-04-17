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
#ifndef RIPPLE_OVERLAY_P2PEER_H_INCLUDED
#define RIPPLE_OVERLAY_P2PEER_H_INCLUDED

#include <ripple/beast/net/IPEndpoint.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/resource/Consumer.h>

#include <chrono>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/fields.hpp>

namespace ripple {

namespace Resource {
class Charge;
}
class Message;
class Application;
class Journal;
namespace PeerFinder {
class Slot;
}

/** This class represents peer-2-peer interface.
 */
class P2Peer
{
public:
    virtual ~P2Peer() = default;

    ////////////////////////////////////////////////////////////////
    // P2p methods
    ////////////////////////////////////////////////////////////////

    /** Uniquely identifies a peer.
        This can be stored in tables to find the peer later. Callers
        can discover if the peer is no longer connected and make
        adjustments as needed.
    */
    using id_t = std::uint32_t;

    virtual void
    send(std::shared_ptr<Message> const& m) = 0;

    virtual beast::IP::Endpoint
    getRemoteAddress() const = 0;

    virtual id_t
    id() const = 0;

    /** Returns `true` if this connection is a member of the cluster. */
    virtual bool
    cluster() const = 0;

    virtual PublicKey const&
    getNodePublic() const = 0;

    virtual bool
    compressionEnabled() const = 0;

    virtual Json::Value
    json() = 0;
    ////////////////////////////////////////////////////////////////
    // Utility
    ////////////////////////////////////////////////////////////////
    static std::string
    makePrefix(std::uint32_t id)
    {
        std::stringstream ss;
        ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
        return ss.str();
    }
};

class P2PeerInternal : public P2Peer
{
public:
    virtual ~P2PeerInternal() = default;
    ////////////////////////////////////////////////////////////////
    // Getters and other methods shared with the application layer
    ////////////////////////////////////////////////////////////////
protected:
    virtual Application&
    app() const = 0;

    virtual boost::asio::strand<boost::asio::executor>&
    strand() = 0;

    virtual beast::Journal const&
    journal() = 0;

    virtual bool
    isSocketOpen() const = 0;

    virtual int
    queueSize() const = 0;

    virtual bool
    isInbound() const = 0;

    virtual ProtocolVersion
    protocol() const = 0;

    virtual int
    incLargeSendq() = 0;

    virtual int
    largeSendq() const = 0;

    virtual void
    fail(std::string const&) = 0;

    virtual void
    close() = 0;

public:  // TODO
    virtual std::string
    getVersion() const = 0;

    virtual std::shared_ptr<PeerFinder::Slot> const&
    slot() = 0;

protected:
    virtual std::mutex&
    recentLock() const = 0;

    virtual boost::beast::http::fields const&
    headers() const = 0;

    /*virtual void
    run() = 0;*/

    virtual std::string
    domain() const = 0;

    virtual std::string
    name() const = 0;

    decltype(auto)
    get_executor() const
    {
        return socket().get_executor();
    }

    ////////////////////////////////////////////////////////////////
    // Hooks, must be implemented in the application layer
    ////////////////////////////////////////////////////////////////
protected:
    virtual bool
    squelched(std::shared_ptr<Message> const&) = 0;

    virtual void
    onEvtProtocolStart() = 0;

    virtual void
    onEvtRun() = 0;

    virtual void
    onEvtClose() = 0;

    virtual void
    onEvtGracefulClose() = 0;

    virtual void
    onEvtShutdown() = 0;

    virtual std::pair<size_t, boost::system::error_code>
    onEvtProtocolMessage(boost::beast::multi_buffer const&, size_t&) = 0;

private:
    virtual boost::asio::ip::tcp::socket&
    socket() const = 0;
};

}  // namespace ripple

#endif
