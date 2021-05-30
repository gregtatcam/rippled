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

#include <ripple/basics/Resolver.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/P2PConfig.h>
#include <ripple/overlay/impl/TrafficCount.h>
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

        virtual void
        run() = 0;
    };

protected:
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    P2PConfig const p2pConfig_;
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
    TrafficCount m_traffic;
    Resolver& m_resolver;
    std::atomic<Peer::id_t> next_id_;
    int timer_count_;
    std::atomic<uint64_t> peerDisconnects_{0};

    std::optional<std::uint32_t> networkID_;

    //--------------------------------------------------------------------------

public:
    P2POverlayImpl(
        P2PConfig const& p2pConfig,
        Setup const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    virtual ~P2POverlayImpl() = default;

    P2POverlayImpl(P2POverlayImpl const&) = delete;
    P2POverlayImpl&
    operator=(P2POverlayImpl const&) = delete;

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

    ServerHandler&
    serverHandler()
    {
        return serverHandler_;
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

    void
    addOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id);

    void
    addInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr);

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

    void
    reportTraffic(TrafficCount::category cat, bool isInbound, int bytes);

    void
    incPeerDisconnect() override
    {
        ++peerDisconnects_;
    }

    std::uint64_t
    getPeerDisconnect() const override
    {
        return peerDisconnects_;
    }

    std::optional<std::uint32_t>
    networkID() const override
    {
        return networkID_;
    }

    void
    stop() override;

    virtual void
    checkStopped() = 0;

    void
    start() override;

protected:
    virtual std::shared_ptr<Child>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) = 0;

    virtual std::shared_ptr<Child>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) = 0;

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
    virtual bool
    processRequest(http_request_type const& req, Handoff& handoff) = 0;

    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override;

    //--------------------------------------------------------------------------

    void
    remove(Child& child);

    void
    autoConnect();

private:
    void
    stopChildren();

protected:
    struct TrafficGauges
    {
        TrafficGauges(
            char const* name,
            beast::insight::Collector::ptr const& collector)
            : bytesIn(collector->make_gauge(name, "Bytes_In"))
            , bytesOut(collector->make_gauge(name, "Bytes_Out"))
            , messagesIn(collector->make_gauge(name, "Messages_In"))
            , messagesOut(collector->make_gauge(name, "Messages_Out"))
        {
        }
        beast::insight::Gauge bytesIn;
        beast::insight::Gauge bytesOut;
        beast::insight::Gauge messagesIn;
        beast::insight::Gauge messagesOut;
    };

    struct Stats
    {
        template <class Handler>
        Stats(
            Handler const& handler,
            beast::insight::Collector::ptr const& collector,
            std::vector<TrafficGauges>&& trafficGauges_)
            : peerDisconnects(
                  collector->make_gauge("Overlay", "Peer_Disconnects"))
            , trafficGauges(std::move(trafficGauges_))
            , hook(collector->make_hook(handler))
        {
        }

        beast::insight::Gauge peerDisconnects;
        std::vector<TrafficGauges> trafficGauges;
        beast::insight::Hook hook;
    };

    Stats m_stats;
    std::mutex m_statsMutex;

protected:
    void
    collect_metrics()
    {
        auto counts = m_traffic.getCounts();
        std::lock_guard lock(m_statsMutex);
        assert(counts.size() == m_stats.trafficGauges.size());

        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            m_stats.trafficGauges[i].bytesIn = counts[i].bytesIn;
            m_stats.trafficGauges[i].bytesOut = counts[i].bytesOut;
            m_stats.trafficGauges[i].messagesIn = counts[i].messagesIn;
            m_stats.trafficGauges[i].messagesOut = counts[i].messagesOut;
        }
        m_stats.peerDisconnects = getPeerDisconnect();
    }
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED
