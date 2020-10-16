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

#ifndef RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/Resolver.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/core/Job.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/TrafficCount.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/server/Handoff.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/optional.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ripple {

class PeerImp;
class BasicConfig;

class OverlayImpl : public Overlay, public squelch::SquelchHandler
{
public:
    class Child
    {
    protected:
        OverlayImpl& overlay_;

        explicit Child(OverlayImpl& overlay);

        virtual ~Child();

    public:
        virtual void
        stop() = 0;
    };

private:
    using clock_type = std::chrono::steady_clock;
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    struct Timer : Child, std::enable_shared_from_this<Timer>
    {
        boost::asio::basic_waitable_timer<clock_type> timer_;

        explicit Timer(OverlayImpl& overlay);

        void
        stop() override;

        void
        run();

        void
        on_timer(error_code ec);
    };

    Application& app_;
    boost::asio::io_service& io_service_;
    boost::optional<boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex
    std::condition_variable_any cond_;
    std::weak_ptr<Timer> timer_;
    boost::container::flat_map<Child*, std::weak_ptr<Child>> list_;
    Setup setup_;
    beast::Journal const journal_;
    ServerHandler& serverHandler_;
    Resource::Manager& m_resourceManager;
    std::unique_ptr<PeerFinder::Manager> m_peerFinder;
    TrafficCount m_traffic;
    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImp>> m_peers;
    hash_map<Peer::id_t, std::weak_ptr<PeerImp>> ids_;
    Resolver& m_resolver;
    std::atomic<Peer::id_t> next_id_;
    int timer_count_;
    std::atomic<uint64_t> jqTransOverflow_{0};
    std::atomic<uint64_t> peerDisconnects_{0};
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    // Last time we crawled peers for shard info. 'cs' = crawl shards
    std::atomic<std::chrono::seconds> csLast_{std::chrono::seconds{0}};
    std::mutex csMutex_;
    std::condition_variable csCV_;
    // Peer IDs expecting to receive a last link notification
    std::set<std::uint32_t> csIDs_;

    boost::optional<std::uint32_t> networkID_;

    squelch::Slots<UptimeClock> slots_;

    //--------------------------------------------------------------------------

public:
    OverlayImpl(
        Application& app,
        Setup const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    ~OverlayImpl();

    OverlayImpl(OverlayImpl const&) = delete;
    OverlayImpl&
    operator=(OverlayImpl const&) = delete;

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

    std::size_t
    size() const override;

    Json::Value
    json() override;

    PeerSequence
    getActivePeers() const override;

    void
    check() override;

    void checkSanity(std::uint32_t) override;

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const override;

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) override;

    void
    broadcast(protocol::TMProposeSet& m) override;

    void
    broadcast(protocol::TMValidation& m) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    void
    add_active(std::shared_ptr<PeerImp> const& peer);

    void
    remove(std::shared_ptr<PeerFinder::Slot> const& slot);

    /** Called when a peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void
    activate(std::shared_ptr<PeerImp> const& peer);

    // Called when an active peer is destroyed.
    void
    onPeerDeactivate(Peer::id_t id);

    // UnaryFunc will be called as
    //  void(std::shared_ptr<PeerImp>&&)
    //
    template <class UnaryFunc>
    void
    for_each(UnaryFunc&& f) const
    {
        std::vector<std::weak_ptr<PeerImp>> wp;
        {
            std::lock_guard lock(mutex_);

            // Iterate over a copy of the peer list because peer
            // destruction can invalidate iterators.
            wp.reserve(ids_.size());

            for (auto& x : ids_)
                wp.push_back(x.second);
        }

        for (auto& w : wp)
        {
            if (auto p = w.lock())
                f(std::move(p));
        }
    }

    // Called when TMManifests is received from a peer
    void
    onManifests(
        std::shared_ptr<protocol::TMManifests> const& m,
        std::shared_ptr<PeerImp> const& from);

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
    incJqTransOverflow() override
    {
        ++jqTransOverflow_;
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return jqTransOverflow_;
    }

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

    Json::Value
    crawlShards(bool pubKey, std::uint32_t hops) override;

    /** Called when the last link from a peer chain is received.

        @param id peer id that received the shard info.
    */
    void
    lastLink(std::uint32_t id);

    /** Updates message count for validator/peer. Sends TMSquelch if the number
     * of messages for N peers reaches threshold T. A message is counted
     * if a peer receives the message for the first time and if
     * the message has been  relayed.
     * @param key Unique message's key
     * @param validator Validator's public key
     * @param peers Peers' id to update the slots for
     * @param type Received protocol message type
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        std::set<Peer::id_t>&& peers,
        protocol::MessageType type);

    /** Overload to reduce allocation in case of single peer
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t peer,
        protocol::MessageType type);

    /** Called when the peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     */
    void
    deletePeer(Peer::id_t id);

private:
    void
    squelch(
        PublicKey const& validator,
        Peer::id_t const id,
        std::uint32_t squelchDuration) const override;

    void
    unsquelch(PublicKey const& validator, Peer::id_t id) const override;

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

    /** Handles crawl requests. Crawl returns information about the
        node and its peers so crawlers can map the network.

        @return true if the request was handled.
    */
    bool
    processCrawl(http_request_type const& req, Handoff& handoff);

    /** Handles validator list requests.
        Using a /vl/<hex-encoded public key> URL, will retrieve the
        latest valdiator list (or UNL) that this node has for that
        public key, if the node trusts that public key.

        @return true if the request was handled.
    */
    bool
    processValidatorList(http_request_type const& req, Handoff& handoff);

    /** Handles health requests. Health returns information about the
        health of the node.

        @return true if the request was handled.
    */
    bool
    processHealth(http_request_type const& req, Handoff& handoff);

    /** Handles non-peer protocol requests.

        @return true if the request was handled.
    */
    bool
    processRequest(http_request_type const& req, Handoff& handoff);

    /** Returns information about peers on the overlay network.
        Reported through the /crawl API
        Controlled through the config section [crawl] overlay=[0|1]
    */
    Json::Value
    getOverlayInfo();

    /** Returns information about the local server.
        Reported through the /crawl API
        Controlled through the config section [crawl] server=[0|1]
    */
    Json::Value
    getServerInfo();

    /** Returns information about the local server's performance counters.
        Reported through the /crawl API
        Controlled through the config section [crawl] counts=[0|1]
    */
    Json::Value
    getServerCounts();

    /** Returns information about the local server's UNL.
        Reported through the /crawl API
        Controlled through the config section [crawl] unl=[0|1]
    */
    Json::Value
    getUnlInfo();

    //--------------------------------------------------------------------------

    //
    // Stoppable
    //

    void
    checkStopped();

    void
    onPrepare() override;

    void
    onStart() override;

    void
    onStop() override;

    void
    onChildrenStopped() override;

    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override;

    //--------------------------------------------------------------------------

    void
    remove(Child& child);

    void
    stop();

    void
    autoConnect();

    void
    sendEndpoints();

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator */
    void
    deleteIdlePeers();

private:
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

    // Continous and rolling averages
    struct CompressionStats
    {
        std::mutex mutex;
        std::uint64_t totalCnt{0};
        std::uint64_t totalCntCompr{0};
        std::uint64_t totalSize{0};
        std::uint64_t totalSizeCompr{0};
        std::uint64_t totalSizeUncompr{0};
        std::uint64_t protocolError{0};
        std::uint64_t badMessage{0};
        std::uint64_t messageSize{0};
        std::array<std::uint64_t, 10> typeTotalCnt{0};
        std::array<std::uint64_t, 10> typeTotalCntCompr{0};
        std::array<std::uint64_t, 10> typeTotalSize{0};
        std::array<std::uint64_t, 10> typeTotalSizeCompr{0};
        std::array<std::uint64_t, 10> typeTotalSizeUncompr{0};
        clock_type::time_point intervalStart{clock_type::now()};
        boost::circular_buffer<double> rollingAvgBwSavingsBuff{6, 0ull};
        std::uint64_t accumBytes{0};
        std::uint64_t accumBytesUncompr{0};
        double rollingAvgBwSavings{0.0};

        std::uint16_t
        typeToStatsType(std::uint16_t type)
        {
            switch (type)
            {
                case protocol::mtMANIFESTS:
                    return 0;
                case protocol::mtENDPOINTS:
                    return 1;
                case protocol::mtTRANSACTION:
                    return 2;
                case protocol::mtGET_LEDGER:
                    return 3;
                case protocol::mtLEDGER_DATA:
                    return 4;
                case protocol::mtGET_OBJECTS:
                    return 5;
                case protocol::mtVALIDATORLIST:
                    return 6;
                case protocol::mtPROPOSE_LEDGER:
                    return 7;
                case protocol::mtVALIDATION:
                    return 8;
                default:
                    return 9;
            }
        }

        std::uint16_t
        statsTypeToType(std::uint16_t type)
        {
            switch (type)
            {
                case 0:
                    return protocol::mtMANIFESTS;
                case 1:
                    return protocol::mtENDPOINTS;
                case 2:
                    return protocol::mtTRANSACTION;
                case 3:
                    return protocol::mtGET_LEDGER;
                case 4:
                    return protocol::mtLEDGER_DATA;
                case 5:
                    return protocol::mtGET_OBJECTS;
                case 6:
                    return protocol::mtVALIDATORLIST;
                case 7:
                    return protocol::mtPROPOSE_LEDGER;
                case 8:
                    return protocol::mtVALIDATION;
                default:
                    return 9;
            }
        }

        void
        addCompressionMetrics(
            std::uint16_t type,
            std::size_t uncompr_size,
            std::size_t total_size,
            bool is_compressed)
        {
            type = typeToStatsType(type);
            std::lock_guard l(mutex);

            auto const header = 4;
            totalCnt++;
            typeTotalCnt[type]++;
            totalSize += total_size;
            accumBytes += total_size;
            typeTotalSize[type] += total_size;
            totalSizeUncompr += uncompr_size + header;
            accumBytesUncompr += uncompr_size + header;
            typeTotalSizeUncompr[type] += uncompr_size + header;
            if (is_compressed)
            {
                totalCntCompr++;
                totalSizeCompr += total_size;
                typeTotalCntCompr[type]++;
                typeTotalSizeCompr[type] += total_size;
            }

            using namespace std::chrono;
            auto const timeElapsed = clock_type::now() - intervalStart;
            auto const timeElapsedInSecs =
                std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);
            if (timeElapsedInSecs >= 600s)
            {
                double const avgBwSavings = 100. *
                    (1. - (double)accumBytes / (double)accumBytesUncompr);
                rollingAvgBwSavingsBuff.push_back(avgBwSavings);

                double const total = std::accumulate(
                    rollingAvgBwSavingsBuff.begin(),
                    rollingAvgBwSavingsBuff.end(),
                    0.0);
                rollingAvgBwSavings = total / (double)rollingAvgBwSavingsBuff.size();

                intervalStart = clock_type::now();
                accumBytes = 0;
                accumBytesUncompr = 0;
            }
        }

        void
        addErrorMetrics(boost::system::error_code const& err)
        {
            std::lock_guard l(mutex);
            if (err == boost::system::errc::protocol_error)
                protocolError++;
            else if (err == boost::system::errc::message_size)
                messageSize++;
            else if (err == boost::system::errc::bad_message)
                badMessage++;
        }

        Json::Value
        getCompressionMetrics()
        {
            std::lock_guard l(mutex);

            Json::Value ret(Json::objectValue);

            ret[jss::comp_total_cnt] = std::to_string(totalCnt);
            ret[jss::comp_total_cnt_compr] = std::to_string(totalCntCompr);
            ret[jss::comp_total_size] = std::to_string(totalSize);
            ret[jss::comp_total_size_compr] = std::to_string(totalSizeCompr);
            ret[jss::comp_total_size_uncompr] =
                std::to_string(totalSizeUncompr);
            ret[jss::comp_avg_bw_savings] = std::to_string(rollingAvgBwSavings);
            double const avgBwSavings =
                100. * (1. - (double)totalSize / (double)totalSizeUncompr);
            ret[jss::comp_total_avg_bw_savings] = std::to_string(avgBwSavings);
            ret[jss::comp_err_protocol_error] = std::to_string(protocolError);
            ret[jss::comp_err_bad_message] = std::to_string(badMessage);
            ret[jss::comp_err_message_size] = std::to_string(messageSize);

            auto set = [&](int i,
                           auto cnt,
                           auto cnt_compr,
                           auto size,
                           auto size_compr,
                           auto size_uncompr) {
                ret[cnt] = std::to_string(typeTotalCnt[i]);
                ret[cnt_compr] = std::to_string(typeTotalCntCompr[i]);
                ret[size] = std::to_string(typeTotalSize[i]);
                ret[size_compr] = std::to_string(typeTotalSizeCompr[i]);
                ret[size_uncompr] = std::to_string(typeTotalSizeUncompr[i]);
            };
            using namespace jss;
            set(0,
                comp_total_cnt_man,
                comp_total_cnt_compr_man,
                comp_total_size_man,
                comp_total_size_compr_man,
                comp_total_size_uncompr_man);
            set(1,
                comp_total_cnt_end,
                comp_total_cnt_compr_end,
                comp_total_size_end,
                comp_total_size_compr_end,
                comp_total_size_uncompr_end);
            set(2,
                comp_total_cnt_tx,
                comp_total_cnt_compr_tx,
                comp_total_size_tx,
                comp_total_size_compr_tx,
                comp_total_size_uncompr_tx);
            set(3,
                comp_total_cnt_gl,
                comp_total_cnt_compr_gl,
                comp_total_size_gl,
                comp_total_size_compr_gl,
                comp_total_size_uncompr_gl);
            set(4,
                comp_total_cnt_ld,
                comp_total_cnt_compr_ld,
                comp_total_size_ld,
                comp_total_size_compr_ld,
                comp_total_size_uncompr_gl);
            set(5,
                comp_total_cnt_go,
                comp_total_cnt_compr_go,
                comp_total_size_go,
                comp_total_size_compr_go,
                comp_total_size_uncompr_go);
            set(6,
                comp_total_cnt_vl,
                comp_total_cnt_compr_vl,
                comp_total_size_vl,
                comp_total_size_compr_vl,
                comp_total_size_uncompr_vl);
            set(7,
                comp_total_cnt_prop,
                comp_total_cnt_compr_prop,
                comp_total_size_prop,
                comp_total_size_compr_prop,
                comp_total_size_uncompr_prop);
            set(8,
                comp_total_cnt_val,
                comp_total_cnt_compr_val,
                comp_total_size_val,
                comp_total_size_compr_val,
                comp_total_size_uncompr_val);
            set(9,
                comp_total_cnt_other,
                comp_total_cnt_compr_other,
                comp_total_size_other,
                comp_total_size_compr_other,
                comp_total_size_uncompr_other);
            return ret;
        }
    };

    Stats m_stats;
    std::mutex m_statsMutex;

    CompressionStats m_comprStats;

public:
    void
    addCompressionMetrics(
        std::uint16_t type,
        std::size_t uncompr_size,
        std::size_t total_size,
        bool is_compressed)
    {
        m_comprStats.addCompressionMetrics(
            type, uncompr_size, total_size, is_compressed);
    }
    void
    addErrorMetrics(boost::system::error_code const& err)
    {
        m_comprStats.addErrorMetrics(err);
    }
    Json::Value
    getCompressionMetrics() override
    {
        return m_comprStats.getCompressionMetrics();
    }

private:
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

#endif
