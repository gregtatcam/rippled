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

#include <ripple/app/main/CollectorManager.h>
#include <ripple/basics/ResolverAsio.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/make_Overlay.h>
#include <ripple/server/Server.h>
#include <ripple/server/Session.h>
#include <ripple.pb.h>
#include <test/jtx/Env.h>

#include <boost/asio.hpp>
#include <boost/bimap.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/regex.hpp>
#include <boost/sort/sort.hpp>
#include <boost/thread.hpp>
#include <boost/tokenizer.hpp>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unistd.h>

namespace ripple {

namespace test {

/** Unit-tests to test Overlay (peer-2-peer only) network. There is
 * a thin application layer implementation to send/receive the endpoints.
 * There are two tests: 1) overlay_net_test, which creates a small network of
 * five interconnected nodes; 2) overlay_xrpl_test, which attempts to
 * replicate complete XRPL network overlay.
 */

static std::string
mkName(std::string const& n, int i)
{
    return n + std::to_string(i);
}

/** Overlay total counts of endpoint messages, inbound/outbound peers,
 * and deactivated peers.
 */
struct Counts
{
    static inline std::atomic_uint64_t msgSendCnt = 0;
    static inline std::atomic_uint64_t msgRecvCnt = 0;
    static inline std::atomic_uint32_t inPeersCnt = 0;
    static inline std::atomic_uint32_t outPeersCnt = 0;
    static inline std::atomic_uint32_t deactivateCnt = 0;
    static bool
    deactivated()
    {
        return deactivateCnt == inPeersCnt + outPeersCnt;
    }
};

class OverlayImplTest;
class VirtualNetwork;

/** Represents a virtual node in the overlay. It contains all objects
 * required for Overlay and Peer instantiation.
 */
struct VirtualNode
{
    VirtualNode(
        VirtualNetwork& net,
        boost::asio::io_service& service,
        std::string const& ip,
        bool isFixed,
        std::unordered_map<std::string, std::string> const& bootstrap,
        std::uint16_t peerPort,
        std::uint16_t out_max,
        std::uint16_t in_max)
        : ip_(ip)
        , id_(sid_)
        , io_service_(service)
        , config_(mkConfig(
              ip,
              std::to_string(peerPort),
              isFixed,
              bootstrap,
              out_max,
              in_max))
        , logs_(std::make_unique<jtx::SuiteLogs>(net))
        , timeKeeper_(std::make_unique<ManualTimeKeeper>())
        , collector_(make_CollectorManager(
              config_->section(SECTION_INSIGHT),
              logs_->journal("Collector")))
        , resourceManager_(Resource::make_Manager(
              collector_->collector(),
              logs_->journal("Resource")))
        , resolver_(ResolverAsio::New(
              io_service_,
              logs_->journal(mkName("Overlay", id_))))
        , identity_(randomKeyPair(KeyType::secp256k1))
        , overlay_(std::make_shared<OverlayImplTest>(
              *this,
              peerPort,
              mkName("Overlay", id_)))
        , serverPort_(1)
        , server_(make_Server(
              *overlay_,
              io_service_,
              logs_->journal(mkName("Server", id_))))
        , name_(ip)
        , out_max_(out_max)
        , in_max_(in_max)
        , bootstrap_(bootstrap)
        , net_(net)
    {
        serverPort_.back().ip = beast::IP::Address::from_string(ip);
        serverPort_.back().port = peerPort;
        serverPort_.back().protocol.insert("peer");
        serverPort_.back().context = make_SSLContext("");
        sid_++;
    }
    void
    run();
    static std::unique_ptr<Config>
    mkConfig(
        std::string const& ip,
        std::string const& peerPort,
        bool isFixed,  // if true then ips_fixed, otherwise ips
        std::unordered_map<std::string, std::string> const& bootstrap,
        std::uint16_t out_max,
        std::uint16_t in_max,
        std::string const& dbPath = "",
        bool http = false)
    {
        auto config = std::make_unique<Config>();
        config->overwrite(ConfigSection::nodeDatabase(), "type", "memory");
        config->overwrite(ConfigSection::nodeDatabase(), "path", "main");
        config->deprecatedClearSection(ConfigSection::importNodeDatabase());
        if (dbPath != "")
        {
            std::string cmd = "mkdir -p " + dbPath;
            auto ret = system(cmd.c_str());
            (void)ret;
        }
        config->legacy("database_path", dbPath);
        (*config)["server"].append("port_peer");
        (*config)["port_peer"].set("ip", ip);
        (*config)["port_peer"].set("port", peerPort);
        (*config)["port_peer"].set("protocol", "peer");

        config->PEER_PRIVATE = false;
        config->PEERS_OUT_MAX = out_max;
        config->PEERS_IN_MAX = in_max;

        if (http)
        {
            (*config)["server"].append("port_rpc");
            (*config)["port_rpc"].set("ip", ip);
            (*config)["port_rpc"].set("port", "6006");
            (*config)["port_rpc"].set("protocol", "http");
        }
        (*config)["ssl_verify"].append("0");
        for (auto it : bootstrap)
        {
            if (it.first == ip)
                continue;
            if (isFixed)
                config->IPS_FIXED.push_back(it.first + " " + peerPort);
            else
                config->IPS.push_back(it.first + " " + peerPort);
        }
        config->setupControl(true, true, false);
        return config;
    }
    static inline int sid_ = 0;
    std::string ip_;
    int id_;
    boost::asio::io_service& io_service_;
    std::unique_ptr<Config> config_;
    std::unique_ptr<jtx::SuiteLogs> logs_;
    std::unique_ptr<ManualTimeKeeper> timeKeeper_;
    std::unique_ptr<CollectorManager> collector_;
    std::unique_ptr<Resource::Manager> resourceManager_;
    std::unique_ptr<ResolverAsio> resolver_;
    std::pair<PublicKey, SecretKey> identity_;
    std::shared_ptr<OverlayImplTest> overlay_;
    std::vector<Port> serverPort_;
    std::unique_ptr<Server> server_;
    std::string name_;
    std::uint16_t out_max_;
    std::uint16_t in_max_;
    std::unordered_map<std::string, std::string> const& bootstrap_;
    VirtualNetwork& net_;
    std::uint16_t nRedirects_{0};
};

/** Represents the Overlay - collection of VirtualNode. Unit tests inherit
 * from this class. It contains one and only io_service for all async
 * operations in the network.
 */
class VirtualNetwork : public beast::unit_test::suite
{
    friend class overlay_net_test;

protected:
    std::uint16_t tot_out_ = 0;
    std::uint16_t tot_in_ = 0;
    std::optional<std::pair<std::uint16_t, std::uint16_t>> max_default_{};
    bool batch_ = false;
    boost::asio::io_service io_service_;
    boost::thread_group tg_;
    std::mutex nodesMutex_;
    std::unordered_map<int, std::shared_ptr<VirtualNode>> nodes_;
    std::chrono::time_point<std::chrono::steady_clock> start_;
    std::unordered_map<std::string, std::string> bootstrap_;
    // global ip:local ip
    boost::bimap<std::string, std::string> ip2Local_;
    using global_local = boost::bimap<std::string, std::string>::value_type;
    std::string baseIp_ = "172.0";
    std::uint16_t static constexpr maxSubaddr_ = 255;

public:
    virtual ~VirtualNetwork() = default;
    VirtualNetwork()
    {
        start_ = std::chrono::steady_clock::now();
    }

    void
    stop();

    boost::asio::io_service&
    io_service()
    {
        return io_service_;
    }

    /** Represents epoch time in seconds since the start of the test.
     */
    std::size_t
    timeSinceStart()
    {
        using namespace std::chrono;
        return duration_cast<seconds>(steady_clock::now() - start_).count();
    }

    std::string
    getGlobalIp(std::string const& localIp)
    {
        return ip2Local_.right.at(localIp);
    }

    std::string
    getLocalIp(std::string const& globalIp)
    {
        return ip2Local_.left.at(globalIp);
    }

protected:
    void
    add(std::shared_ptr<VirtualNode> const& node)
    {
        std::lock_guard l(nodesMutex_);
        nodes_.emplace(node->id_, node);
    }
    void
    mkNode(
        std::string const& ip,
        bool isFixed,
        std::uint16_t out_max,
        std::uint16_t in_max,
        std::uint16_t peerPort = 51235)
    {
        {
            if (out_max == 0)
            {
                out_max++;
                in_max++;
            }
            // test - reduce out_max+in_max
            auto const t = out_max + in_max;
            if (max_default_ && in_max > 0 && t <= 21 &&
                t > (max_default_->first + max_default_->second))
            {
                out_max = max_default_->first;
                in_max = max_default_->second;
            }
            tot_out_ += out_max;
            tot_in_ += in_max;
            if (!batch_)
            {
                std::cout << nodes_.size() << " " << ip << " "
                          << ip2Local_.right.at(ip) << " " << out_max << " "
                          << in_max << " " << tot_out_ << " " << tot_in_ << " "
                          << (bootstrap_.find(ip) != bootstrap_.end()
                                  ? bootstrap_[ip]
                                  : "")
                          << "                                \r" << std::flush;
            }
            auto node = std::make_shared<VirtualNode>(
                *this,
                io_service_,
                ip,
                isFixed,
                bootstrap_,
                peerPort,
                out_max,
                in_max);
            add(node);
            node->run();
        }
    }
};

class P2PConfigTest : public P2PConfig
{
    VirtualNode const& node_;

public:
    P2PConfigTest(VirtualNode const& node) : node_(node)
    {
    }
    Config const&
    config() const override
    {
        return *node_.config_;
    }
    Logs&
    logs() const override
    {
        return *node_.logs_;
    }
    bool
    isValidator() const override
    {
        return true;
    }
    std::pair<PublicKey, SecretKey> const&
    identity() const override
    {
        return node_.identity_;
    }
    std::optional<std::string>
    clusterMember(PublicKey const&) const override
    {
        return {};
    }
    bool
    reservedPeer(PublicKey const& key) const override
    {
        return false;
    }
    std::optional<std::pair<uint256, uint256>>
    clHashes() const override
    {
        return {};
    }
    NetClock::time_point
    now() const override
    {
        return node_.timeKeeper_->now();
    }
};

/** Thin Application layer peer implementation. Handles
 * send/receive endpoints protocol message.
 */
class PeerImpTest : public P2PeerImp
{
    VirtualNode& node_;

public:
    ~PeerImpTest();

    PeerImpTest(
        VirtualNode& node,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              overlay.p2pConfig(),
              id,
              slot,
              std::move(request),
              publicKey,
              protocol,
              std::move(stream_ptr),
              overlay)
        , node_(node)
    {
    }

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    PeerImpTest(
        VirtualNode& node,
        std::unique_ptr<stream_type>&& stream_ptr,
        const_buffers_type const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              overlay.p2pConfig(),
              std::move(stream_ptr),
              buffers,
              std::move(slot),
              std::move(response),
              publicKey,
              protocol,
              id,
              overlay)
        , node_(node)
    {
    }

    void
    charge(Resource::Charge const&) override
    {
    }

    bool
    cluster() const override
    {
        return false;
    }

    bool
    isHighLatency() const override
    {
        return false;
    }

    int
    getScore(bool) const override
    {
        return 0;
    }

    PublicKey const&
    getNodePublic() const override
    {
        return node_.identity_.first;
    }

    Json::Value
    json() override
    {
        return {};
    }

    bool
    supportsFeature(ProtocolFeature f) const override
    {
        return false;
    }

    std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return {};
    }

    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }

    uint256 const&
    getClosedLedgerHash() const override
    {
        static uint256 h{};
        return h;
    }

    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override
    {
        return false;
    }

    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override
    {
    }

    bool
    hasTxSet(uint256 const& hash) const override
    {
        return false;
    }

    void
    cycleStatus() override
    {
    }

    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override
    {
        return false;
    }

private:
    //--------------------------------------------------------------------------
    // Delegate custom handling of events to the application layer.
    void
    onEvtRun() override
    {
    }

    bool
    onEvtSendFilter(std::shared_ptr<Message> const&) override
    {
        return false;
    }

    void
    onEvtClose() override
    {
    }

    void
    onEvtGracefulClose() override
    {
    }

    void
    onEvtShutdown() override
    {
    }

    void
    onEvtDoProtocolStart() override
    {
    }

    bool
    onEvtProtocolMessage(
        detail::MessageHeader const& header,
        mutable_buffers_type const& buffers) override
    {
        switch (header.message_type)
        {
            case protocol::mtENDPOINTS: {
                auto const m =
                    detail::parseMessageContent<protocol::TMEndpoints>(
                        header, buffers);
                if (!m)
                    return false;
                if (m->version() != 2)
                    break;
                Counts::msgRecvCnt++;
                std::vector<PeerFinder::Endpoint> endpoints;
                endpoints.reserve(m->endpoints_v2().size());

                for (auto const& tm : m->endpoints_v2())
                {
                    auto result =
                        beast::IP::Endpoint::from_string_checked(tm.endpoint());
                    if (!result)
                        continue;

                    endpoints.emplace_back(
                        tm.hops() > 0 ? *result
                                      : remote_address_.at_port(result->port()),
                        tm.hops());
                }

                if (!endpoints.empty())
                    overlay_.peerFinder().on_endpoints(slot_, endpoints);
            }
        }
        return true;
    }
};

/** ConnectAttempt must bind to ip/port so that when it connects
 * to the server endpoint it's not treated as a duplicate ip.
 * If a client doesn't bind to specific ip then it binds to
 * a default ip, which is going to be the same for all clients.
 * Consequently, clients connecting to the same endpoint are
 * treated as the duplicated endpoints and are disconnected.
 */
class ConnectAttemptTest : public ConnectAttempt
{
public:
    ConnectAttemptTest(
        VirtualNode& node,
        P2PConfig const& p2pConfig,
        boost::asio::io_service& io_service,
        endpoint_type const& remote_endpoint,
        Resource::Consumer usage,
        shared_context const& context,
        std::uint32_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        beast::Journal journal,
        P2POverlayImpl& overlay)
        : ConnectAttempt(
              p2pConfig,
              io_service,
              remote_endpoint,
              usage,
              context,
              id,
              slot,
              journal,
              overlay)
    {
        // Bind to this node configured ip
        auto sec = p2pConfig_.config().section("port_peer");
        socket_.open(boost::asio::ip::tcp::v4());
        socket_.bind(boost::asio::ip::tcp::endpoint(
            boost::asio::ip::address::from_string(
                sec.get<std::string>("ip")->c_str()),
            0));
        boost::asio::socket_base::reuse_address reuseAddress(true);
        socket_.set_option(reuseAddress);
    }
};

/** Thin application layer overlay implementation. Keeps a list
 * of the application layer peer implementation.
 */
class OverlayImplTest : public P2POverlayImpl
{
private:
    using clock_type = std::chrono::steady_clock;
    struct Timer : Child, std::enable_shared_from_this<Timer>
    {
        OverlayImplTest& overlay_;
        boost::asio::basic_waitable_timer<clock_type> timer_;
        bool stopping_{false};

        explicit Timer(OverlayImplTest& overlay)
            : Child(overlay), overlay_(overlay), timer_(overlay_.io_service_)
        {
        }

        void
        stop() override
        {
            stopping_ = true;
            timer_.cancel();
        }

        void
        async_wait()
        {
            timer_.expires_after(std::chrono::seconds(1));
            timer_.async_wait(overlay_.strand_.wrap(std::bind(
                &Timer::on_timer, shared_from_this(), std::placeholders::_1)));
        }

        void
        on_timer(error_code ec)
        {
            if (ec || stopping_)
            {
                return;
            }

            overlay_.m_peerFinder->once_per_second();
            overlay_.sendEndpoints();
            overlay_.autoConnect();
            async_wait();
        }
    };

    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImpTest>>
        peers_;
    std::weak_ptr<Timer> timer_;
    VirtualNode& node_;

public:
    OverlayImplTest(
        VirtualNode& node,
        std::uint16_t port,
        std::string const& name)
        : P2POverlayImpl(
              std::move(std::make_unique<P2PConfigTest>(node)),
              setup_Overlay(*node.config_),
              port,
              *node.resourceManager_,
              *node.resolver_,
              node.io_service_,
              *node.config_,
              node.collector_->collector())
        , node_(node)
    {
    }
    ~OverlayImplTest() = default;

    void
    start() override
    {
        P2POverlayImpl::start();

        auto const timer = std::make_shared<Timer>(*this);
        addChild(timer);
        timer_ = timer;
        timer->async_wait();
    }

    void
    stop() override
    {
        P2POverlayImpl::stop();
    }

    std::size_t
    size() const override
    {
        return peers_.size();
    }

    Json::Value
    json() override
    {
        return {};
    }

    PeerSequence
    getActivePeers() const override
    {
        return {};
    }

    void checkTracking(std::uint32_t) override
    {
    }

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const&) const override
    {
        return {};
    }

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const&) override
    {
        return {};
    }

    void
    broadcast(protocol::TMProposeSet&) override
    {
    }

    void
    broadcast(protocol::TMValidation&) override
    {
    }

    std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) override
    {
        return {};
    }

    std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) override
    {
        return {};
    }

    void
    incJqTransOverflow() override
    {
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return 0;
    }

    void
    incPeerDisconnect() override
    {
    }

    std::uint64_t
    getPeerDisconnect() const override
    {
        return 0;
    }

    void
    incPeerDisconnectCharges() override
    {
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return 0;
    }

    Json::Value
    crawlShards(bool includePublicKey, std::uint32_t hops) override
    {
        return {};
    }

    void
    onPeerDeactivate(std::shared_ptr<PeerFinder::Slot> const& slot)
    {
        Counts::deactivateCnt++;
        std::lock_guard l(mutex_);
        peers_.erase(slot);
    }

    // Server handler
    //-----------------------------------------------------------------
    bool
    onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint)
    {
        return true;
    }

    Handoff
    onHandoff(
        Session& session,
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remote_address)
    {
        return P2POverlayImpl::onHandoff(
            std::move(bundle), std::move(request), remote_address);
    }

    Handoff
    onHandoff(
        Session& session,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remote_address)
    {
        return onHandoff(
            session,
            {},
            std::forward<http_request_type>(request),
            remote_address);
    }

    void
    onRequest(Session& session)
    {
        if (beast::rfc2616::is_keep_alive(session.request()))
            session.complete();
        else
            session.close(true);
    }

    void
    onWSMessage(
        std::shared_ptr<WSSession> session,
        std::vector<boost::asio::const_buffer> const&)
    {
    }

    void
    onClose(Session& session, boost::system::error_code const&)
    {
    }

    void
    onStopped(Server& server)
    {
    }
    //-----------------------------------------------------------------

private:
    void
    sendEndpoints()
    {
        auto const result = m_peerFinder->buildEndpointsForPeers();
        for (auto const& e : result)
        {
            std::shared_ptr<PeerImpTest> peer;
            {
                std::lock_guard lock(mutex_);
                if (auto const iter = peers_.find(e.first);
                    iter != peers_.end())
                    peer = iter->second.lock();
            }
            if (peer)
            {
                protocol::TMEndpoints tm;
                for (auto first = e.second.begin(); first != e.second.end();
                     first++)
                {
                    auto& tme2(*tm.add_endpoints_v2());
                    tme2.set_endpoint(first->address.to_string());
                    tme2.set_hops(first->hops);
                }
                tm.set_version(2);
                Counts::msgSendCnt++;
                peer->send(
                    std::make_shared<Message>(tm, protocol::mtENDPOINTS));
            }
        }
    }
    //-----------------------------------------------------------------
    bool
    onEvtProcessRequest(http_request_type const& req, Handoff& handoff) override
    {
        return false;
    }

    std::shared_ptr<P2PeerImp>
    mkInboundPeer(
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) override
    {
        auto const peer = std::make_shared<PeerImpTest>(
            node_,
            id,
            slot,
            std::move(request),
            publicKey,
            protocol,
            std::move(stream_ptr),
            *this);
        Counts::inPeersCnt++;
        std::lock_guard l(mutex_);
        peers_.emplace(slot, peer);
        return peer;
    }

    std::shared_ptr<P2PeerImp>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) override
    {
        auto const peer = std::make_shared<PeerImpTest>(
            node_,
            std::move(stream_ptr),
            buffers.data(),
            std::move(slot),
            std::move(response),
            publicKey,
            protocol,
            id,
            *this);
        Counts::outPeersCnt++;
        std::lock_guard l(mutex_);
        peers_.emplace(slot, peer);
        return peer;
    }

    std::shared_ptr<ConnectAttempt>
    mkConnectAttempt(
        beast::IP::Endpoint const& remote_endpoint,
        Resource::Consumer const& usage,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        std::uint16_t id) override
    {
        return std::make_shared<ConnectAttemptTest>(
            node_,
            p2pConfig(),
            io_service_,
            beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
            usage,
            setup_.context,
            id++,
            slot,
            p2pConfig().logs().journal("Peer"),
            *this);
    }
};

void
VirtualNode::run()
{
    server_->ports(serverPort_);
    overlay_->start();
}

void
VirtualNetwork::stop()
{
    std::lock_guard l(nodesMutex_);

    for (auto& node : nodes_)
    {
        node.second->server_.reset();
        node.second->overlay_->stop();
    }
    io_service_.stop();
}

PeerImpTest::~PeerImpTest()
{
    static_cast<OverlayImplTest&>(overlay_).onPeerDeactivate(slot_);
}

/** Test Overlay network with five nodes with ip in range 172.0.0.0-172.0.0.4.
 * Ip's must be pre-configured (see overlay_xrpl_test below). The test stops
 * after total 20 peers or 15 seconds.
 */
class overlay_net_test : public VirtualNetwork
{
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> overlayTimer_;

public:
    overlay_net_test() : overlayTimer_(io_service_)
    {
    }

    void
    startNodes(std::vector<std::string> const& nodes)
    {
        for (auto n : nodes)
            mkNode(n, true, 20, 20);
        for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
            tg_.create_thread(
                boost::bind(&boost::asio::io_service::run, &io_service_));
        tg_.join_all();
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        auto mkIp = [&](auto str) {
            std::string ip = baseIp_ + str;
            ip2Local_.insert(global_local(ip, ip));
            bootstrap_[ip] = ip;
            return ip;
        };
        std::vector<std::string> nodes = {
            mkIp(".0.0"),
            mkIp(".0.1"),
            mkIp(".0.2"),
            mkIp(".0.3"),
            mkIp(".0.4")};
        setTimer();
        startNodes(nodes);
        std::cout << "peers " << Counts::inPeersCnt << " "
                  << Counts::outPeersCnt << " " << Counts::deactivateCnt
                  << std::endl;
        std::cout << "messages " << Counts::msgRecvCnt << " "
                  << Counts::msgSendCnt << std::endl;
        BEAST_EXPECT(
            Counts::inPeersCnt + Counts::outPeersCnt == 20 &&
            Counts::deactivated());
        BEAST_EXPECT(
            Counts::msgSendCnt > 0 && Counts::msgSendCnt == Counts::msgRecvCnt);
    }

    void
    onOverlayTimer(boost::system::error_code const& ec)
    {
        if (ec || (Counts::outPeersCnt + Counts::inPeersCnt == 20) ||
            timeSinceStart() > 20)
        {
            stop();
        }
        else
            setTimer();
    }

    void
    setTimer()
    {
        overlayTimer_.expires_from_now(std::chrono::seconds(1));
        overlayTimer_.async_wait(std::bind(
            &overlay_net_test::onOverlayTimer, this, std::placeholders::_1));
    }

    void
    run() override
    {
        testOverlay();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(overlay_net, ripple_data, ripple);
// BEAST_DEFINE_TESTSUITE_MANUAL(overlay_xrpl, ripple_data, ripple)

}  // namespace test

}  // namespace ripple