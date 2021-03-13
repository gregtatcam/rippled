//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_P2POVERLAYBASEIMPL_H_INCLUDED
#define RIPPLE_P2POVERLAYBASEIMPL_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/Resolver.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/core/Job.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/P2POverlay.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/OverlayImplTraits.h>
#include <ripple/overlay/impl/TrafficCount.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/peerfinder/make_Manager.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/Handoff.h>
#include <ripple/server/SimpleWriter.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/optional.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace ripple {

class P2POverlayBaseImpl : virtual public P2POverlay
{
protected:
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    Application& app_;
    boost::asio::io_service& io_service_;
    boost::optional<boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex
    std::condition_variable_any cond_;
    Setup setup_;
    beast::Journal const journal_;
    ServerHandler& serverHandler_;
    Resource::Manager& m_resourceManager;
    std::unique_ptr<PeerFinder::Manager> m_peerFinder;
    TrafficCount m_traffic;
    Resolver& m_resolver;
    std::atomic<P2Peer::id_t> next_id_;
    std::atomic<uint64_t> peerDisconnects_{0};
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    boost::optional<std::uint32_t> networkID_;

public:
    class Child
    {
    protected:
        P2POverlayBaseImpl& overlay_;

        explicit Child(P2POverlayBaseImpl& overlay) : overlay_(overlay)
        {
        }

        virtual ~Child()
        {
            overlay_.remove(*this);
        }

    public:
        virtual void
        stop() = 0;
    };

    P2POverlayBaseImpl(
        Application& app,
        Setup const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    virtual ~P2POverlayBaseImpl();

    P2POverlayBaseImpl(P2POverlayBaseImpl const&) = delete;
    P2POverlayBaseImpl&
    operator=(P2POverlayBaseImpl const&) = delete;

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

    virtual void
    remove(std::shared_ptr<PeerFinder::Slot> const& slot) = 0;

    // Called when an active peer is destroyed.
    virtual void
    onPeerDeactivate(P2Peer::id_t id) = 0;

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

    void
    incPeerDisconnectCharges() override
    {
        ++peerDisconnectsCharges_;
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return peerDisconnectsCharges_;
    }

    boost::optional<std::uint32_t>
    networkID() const override
    {
        return networkID_;
    }

    virtual void
    addOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) = 0;

    virtual void
    addInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) = 0;

    /** Called when a peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    virtual void
    activate(std::shared_ptr<PeerFinder::Slot> const& slot) = 0;

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

    virtual bool
    processRequest(http_request_type const& req, Handoff& handoff) = 0;

    /** Returns information about the local server's UNL.
        Reported through the /crawl API
        Controlled through the config section [crawl] unl=[0|1]
    */
    Json::Value
    getUnlInfo();

    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override;

    //--------------------------------------------------------------------------

public:
    virtual void
    remove(P2POverlayBaseImpl::Child& child) = 0;

protected:
    virtual void
    stop() = 0;

    virtual void
    addChild(std::shared_ptr<P2POverlayBaseImpl::Child> child) = 0;

    void
    autoConnect();

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

#endif  // RIPPLE_P2POVERLAYBASEIMPL_H_INCLUDED
