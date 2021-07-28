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

#ifndef RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/Resolver.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/server/Handoff.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/container/flat_map.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace ripple {

class BasicConfig;
class P2PeerImp;

class P2POverlayImpl : public Overlay
{
public:
    class Child
    {
    protected:
        P2POverlayImpl& overlay_;

        explicit Child(P2POverlayImpl& overlay);

        virtual ~Child();

    public:
        virtual void
        stop() = 0;
    };

protected:
    using clock_type = std::chrono::steady_clock;
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    Application& app_;
    boost::asio::io_service& io_service_;
    std::optional<boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex
    std::condition_variable_any cond_;
    boost::container::flat_map<Child*, std::weak_ptr<Child>> list_;
    Setup setup_;
    beast::Journal const journal_;
    ServerHandler& serverHandler_;
    Resource::Manager& m_resourceManager;
    std::unique_ptr<PeerFinder::Manager> m_peerFinder;
    Resolver& m_resolver;
    std::atomic<Peer::id_t> next_id_;

    std::optional<std::uint32_t> networkID_;

    //--------------------------------------------------------------------------

public:
    P2POverlayImpl(
        Application& app,
        Setup const& setup,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    P2POverlayImpl(P2POverlayImpl const&) = delete;
    P2POverlayImpl&
    operator=(P2POverlayImpl const&) = delete;

    void
    start() override;

    void
    stop() override;

    PeerFinder::Manager&
    peerFinder()
    {
        return *m_peerFinder;
    }

    Resource::Manager&
    resourceManager()
    {
        return m_resourceManager;
    }

    Setup const&
    setup() const
    {
        return setup_;
    }

    Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        endpoint_type remote_endpoint) override;

    void
    connect(beast::IP::Endpoint const& remote_endpoint) override;

    int
    limit() override;

    //--------------------------------------------------------------------------
    //
    // P2POverlayImpl
    //

    static bool
    isPeerUpgrade(http_request_type const& request);

    template <class Body>
    static bool
    isPeerUpgrade(boost::beast::http::response<Body> const& response)
    {
        if (!is_upgrade(response))
            return false;
        return response.result() ==
            boost::beast::http::status::switching_protocols;
    }

    template <class Fields>
    static bool
    is_upgrade(boost::beast::http::header<true, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (req.method() != boost::beast::http::verb::get)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists(
                "upgrade"))
            return false;
        return true;
    }

    template <class Fields>
    static bool
    is_upgrade(boost::beast::http::header<false, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists(
                "upgrade"))
            return false;
        return true;
    }

    static std::string
    makePrefix(std::uint32_t id);

    std::optional<std::uint32_t>
    networkID() const override
    {
        return networkID_;
    }

    void
    addInboundPeer(
        Application& app,
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr);

    void
    addOutboundPeer(
        Application& app,
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id);

protected:
    std::shared_ptr<Writer>
    makeRedirectResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remote_address);

    std::shared_ptr<Writer>
    makeErrorResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remote_address,
        std::string msg);

    /** Handles non-peer protocol requests.

        @return true if the request was handled.
    */
    bool
    processRequest(http_request_type const& req, Handoff& handoff);

    //--------------------------------------------------------------------------

    void
    remove(Child& child);

    void
    stopChildren();

    void
    autoConnect();

    // Delegation of events handling to the application layer
private:
    virtual bool
    onEvtProcessRequest(http_request_type const& req, Handoff& handoff) = 0;

    virtual std::shared_ptr<P2PeerImp>
    mkInboundPeer(
        Application& app,
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) = 0;

    virtual std::shared_ptr<P2PeerImp>
    mkOutboundPeer(
        Application& app,
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

#endif  // RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED
