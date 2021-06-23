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
#include <ripple/app/main/CollectorManager.h>
#include <ripple/basics/ResolverAsio.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/Stoppable.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/make_Overlay.h>
#include <ripple/server/Server.h>
#include <ripple/server/Session.h>
#include <ripple.pb.h>
#include <test/jtx/Env.h>
#include <test/overlay/DefaultOverlayImpl.h>

#include <boost/asio.hpp>
#include <boost/bimap.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/regex.hpp>
#include <boost/thread.hpp>
#include <boost/tokenizer.hpp>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <unistd.h>

namespace ripple {

namespace test {

/** Unit-tests for testing Overlay (peer-2-peer only) network. There is
 * a think application layer implementation to send/receive the endpoints.
 * There are two test overlay_net_test, which creates a small network of
 * five interconnected nodes. And overlay_xrpl_test, which attempts to
 * replicate complete XRPL network overlay. Each test has more detailed
 * description.
 */

class OverlayImplTest;
class VirtualNetwork;

static std::string
name(std::string const& n, int i)
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

/** Represents a virtual node in the overlay. It contains all objects
 * required for Overlay and Peer instantiation.
 */
struct VirtualNode
{
    VirtualNode(
        beast::unit_test::suite& suite,
        VirtualNetwork& net,
        Stoppable& parent,
        boost::asio::io_service& service,
        std::string const& ip,
        bool isFixed,
        std::unordered_map<std::string, std::string> const& bootstrap,
        std::uint16_t peerPort,
        std::uint16_t out_max,
        std::uint16_t in_max,
        bool testNode = false)
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
        , logs_(std::make_unique<jtx::SuiteLogs>(suite))
        , timeKeeper_(std::make_unique<ManualTimeKeeper>())
        , collector_(CollectorManager::New(
              config_->section(SECTION_INSIGHT),
              logs_->journal("Collector")))
        , resourceManager_(Resource::make_Manager(
              collector_->collector(),
              logs_->journal("Resource")))
        , resolver_(ResolverAsio::New(
              io_service_,
              logs_->journal(name("Overlay", id_))))
        , identity_(randomKeyPair(KeyType::secp256k1))
        , overlay_(std::make_shared<OverlayImplTest>(
              *this,
              parent,
              peerPort,
              name("Overlay", id_)))
        , serverPort_(1)
        , server_(make_Server(
              *overlay_,
              io_service_,
              logs_->journal(name("Server", id_))))
        , name_(ip)
        , out_max_(out_max)
        , in_max_(in_max)
        , bootstrap_(bootstrap)
        , net_(net)
        , testNode_(testNode)
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
            system(cmd.c_str());
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
    bool testNode_;
    std::uint16_t nRedirects_{0};
};

/** Represents the Overlay - collection of VirtualNode. Unit tests inherit
 * from this class. It contains one and only io_service for all async
 * operations in the network.
 */
class VirtualNetwork
{
    friend class overlay_net_test;

protected:
    boost::asio::io_service io_service_;
    boost::thread_group tg_;
    std::mutex nodesMutex_;
    std::unordered_map<int, std::shared_ptr<VirtualNode>> nodes_;
    std::chrono::time_point<std::chrono::steady_clock> start_;
    std::unordered_map<std::string, std::string> bootstrap_;
    // features
    // limit connections to bootstrap nodes from the same node
    bool limitBootstrapConnections_ = false;
    // handle max out of inbound connections
    bool handleInboundPrunning_ = false;

public:
    virtual ~VirtualNetwork() = default;
    VirtualNetwork()
    {
        start_ = std::chrono::steady_clock::now();
    }

    virtual void
    stop() = 0;

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

    bool
    limitConnections()
    {
        return limitBootstrapConnections_;
    }

    bool
    inboundPrunning()
    {
        return handleInboundPrunning_;
    }

    std::tuple<std::uint16_t, std::uint32_t, std::vector<std::uint16_t>>
    getRedirects()
    {
        std::lock_guard l(nodesMutex_);
        std::tuple<std::uint16_t, std::uint32_t, std::vector<std::uint16_t>>
            res;
        std::get<2>(res).reserve(nodes_.size());
        for (auto it : nodes_)
        {
            if (!it.second->testNode_)
            {
                std::get<1>(res) += it.second->nRedirects_;
                std::get<2>(res).push_back(it.second->nRedirects_);
            }
            else
                std::get<0>(res) = it.second->nRedirects_;
        }
        return res;
    }

protected:
    void
    add(std::shared_ptr<VirtualNode> const& node)
    {
        std::lock_guard l(nodesMutex_);
        nodes_.emplace(node->id_, node);
    }
    virtual void
    mkNode(
        std::string const& ip,
        bool isFixed,
        std::uint16_t out_max,
        std::uint16_t in_max,
        bool testNode = false,
        std::uint16_t peerPort = 51235) = 0;
};

/** Thin Application layer peer implementation. Handles
 * send/receive endpoints protocol message.
 */
class PeerImpTest : public DefaultPeerImp<PeerImpTest>
{
    friend class P2PeerImp<PeerImpTest>;
    VirtualNode& node_;

public:
    PeerImpTest(
        VirtualNode& node,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl& overlay)
        : DefaultPeerImp(
              *node.logs_,
              id,
              slot,
              std::move(request),
              publicKey,
              protocol,
              std::move(stream_ptr),
              false,
              overlay)
        , node_(node)
    {
    }

    PeerImpTest(
        VirtualNode& node,
        std::unique_ptr<stream_type>&& stream_ptr,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay)
        : DefaultPeerImp(
              *node.logs_,
              std::move(stream_ptr),
              std::move(slot),
              std::move(response),
              publicKey,
              protocol,
              id,
              false,
              overlay)
        , node_(node)
    {
    }

    ~PeerImpTest();

private:
    /** P2P hook. Enables p2p layer to hand over protocol message
     * implementation to the application layer peer.
     */
    std::pair<size_t, boost::system::error_code>
    onEvtProtocolMessage(
        boost::beast::multi_buffer const& mbuffers,
        size_t& hint)
    {
        std::pair<std::size_t, boost::system::error_code> result = {0, {}};

        auto header = ripple::detail::getMessageHeader(
            compressionEnabled(), result.second, mbuffers.data(), hint);
        if (!header)
            return result;

        bool success = false;

        switch (header->message_type)
        {
            case protocol::mtENDPOINTS:
                Counts::msgRecvCnt++;
                if (auto m = ripple::detail::parseMessageContent<
                        protocol::TMEndpoints>(*header, mbuffers.data()))
                {
                    onMessage(m);
                    success = true;
                }
                break;
            default:
                break;
        }

        result.first = header->total_wire_size;

        if (!success)
            result.second = make_error_code(boost::system::errc::bad_message);

        return result;
    }

    void
    onMessage(std::shared_ptr<protocol::TMEndpoints> const& m)
    {
        std::vector<PeerFinder::Endpoint> endpoints;
        endpoints.reserve(m->endpoints_v2().size());

        for (auto const& tm : m->endpoints_v2())
        {
            if (auto result =
                    beast::IP::Endpoint::from_string_checked(tm.endpoint()))
                endpoints.emplace_back(
                    tm.hops() > 0 ? *result
                                  : remote_address_.at_port(result->port()),
                    tm.hops());
        }

        if (!endpoints.empty())
            overlay_.peerFinder().on_endpoints(slot_, endpoints);
    }
};

/** Enables access to dynamic properties managed by the Application
 * layer.
 */
class AppConfigRequestorTest : public AppConfigRequestor
{
public:
    std::optional<std::string>
    clusterMember(PublicKey const&) override
    {
        return std::nullopt;
    }
    bool
    reservedPeer(PublicKey const&) override
    {
        return false;
    }
    std::optional<std::pair<uint256, uint256>>
    clHashes() override
    {
        return std::make_pair(uint256{1}, uint256{2});
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
    VirtualNode& node_;

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
        , node_(node)
    {
        // Bind to this node configured ip
        auto sec = p2pConfig_.config.section("port_peer");
        socket_.open(boost::asio::ip::tcp::v4());
        socket_.bind(boost::asio::ip::tcp::endpoint(
            boost::asio::ip::address::from_string(
                sec.get<std::string>("ip")->c_str()),
            0));
        boost::asio::socket_base::reuse_address reuseAddress(true);
        socket_.set_option(reuseAddress);
    }

protected:
    void
    processResponse() override
    {
        if (response_.result() ==
            boost::beast::http::status::service_unavailable)
            node_.nRedirects_++;
        ConnectAttempt::processResponse();
    }
};

/** Thin application layer overlay implementation. Keeps a list
 * of the application layer peer implementation.
 */
class OverlayImplTest : public DefaultOverlayImpl,
                        public std::enable_shared_from_this<OverlayImplTest>
{
private:
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::mutex peersMutex_;
    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImpTest>>
        peers_;
    VirtualNode& node_;
    std::string const name_;
    AppConfigRequestorTest requestor_;
    // Model a feature limiting a number of connections to
    // the bootstrap servers. Allow only one connection to
    // each of the ripple, alloy, or isrdc nodes.
    std::unordered_map<std::string, bool> bootstrapConnected_;
    // handle inbound slots max out
    std::size_t inboundPrunningTimer_{0};
    // removed peers statistics
    boost::circular_buffer<float> rollingAvg_{30, 0u};
    std::size_t intervalStart_{0};
    float totalRemoved_{0};
    std::atomic<float> rollingAvgRemoved_{0};

public:
    virtual ~OverlayImplTest()
    {
        timer_.cancel();
    }

    OverlayImplTest(
        VirtualNode& node,
        Stoppable& parent,
        std::uint16_t overlayPort,
        std::string const& name)
        : DefaultOverlayImpl(
              P2PConfig{
                  *node.config_,
                  *node.logs_,
                  true,
                  node.identity_,
                  node.timeKeeper_->now(),
                  requestor_},
              setup_Overlay(*node.config_),
              parent,
              overlayPort,
              *node.resourceManager_,
              *node.resolver_,
              node.io_service_,
              *node.config_,
              node.collector_->collector())
        , timer_(node.io_service_)
        , node_(node)
        , name_(name)
    {
    }

    void
    setTimer()
    {
        timer_.expires_from_now(std::chrono::seconds(1));
        timer_.async_wait(strand_.wrap(std::bind(
            &OverlayImplTest::onTimer,
            this->shared_from_this(),
            std::placeholders::_1)));
    }

    void
    cancelTimer()
    {
        timer_.cancel();
    }

    // Overlay
    void
    run()
    {
        start();
        setTimer();
    }

    void
    onTimer(boost::system::error_code const& ec)
    {
        if (ec)
            return;
        peerFinder().once_per_second();
        sendEndpoints();
        autoConnect();
        setTimer();
        if (node_.net_.inboundPrunning() && checkInboundPrunning())
            handleInboundPrunning();
    }

    bool
    checkInboundPrunning()
    {
        // applicable only to high inbound max configuration
        if (node_.in_max_ < 100)
            return false;
        if (inboundPrunningTimer_ > 0 &&
            node_.net_.timeSinceStart() > inboundPrunningTimer_)
            return true;
        else if (inboundPrunningTimer_ == 0)
        {
            auto [nout, nin] = getPeersCounts();
            // this is a prototype of the feature. the way this test
            // runs, the inbound slots are going to fill up on start up
            // therefore don't disconnect right away, but wait 90 seconds
            // so that the peer has enough time to connect to other endpoints
            // besides this one.
            if (nin >= node_.in_max_)
                inboundPrunningTimer_ = node_.net_.timeSinceStart() + 90;
        }
        return false;
    }

    /** Handle inbound peer max out
     */
    void
    handleInboundPrunning()
    {
        auto [nout, nin] = getPeersCounts();
        std::vector<std::shared_ptr<PeerImpTest>> active;
        {
            std::lock_guard l(peersMutex_);
            active.reserve(nin);
            for (auto [slot, peer] : peers_)
            {
                auto p = peer.lock();
                if (p && p->inbound())
                    active.push_back(p);
            }
        }
        // use a simple strategy of removing a sample of peers
        auto ns = 3 * nin / 100;
        std::random_shuffle(active.begin(), active.end());
        for (auto i = 0; i < ns; ++i)
            active[i]->stop();
        inboundPrunningTimer_ = 0;
        totalRemoved_ += ns;
        float const timeElapsed = node_.net_.timeSinceStart() - intervalStart_;
        if (timeElapsed > 60)
        {
            auto const avgRemoved = totalRemoved_ / (timeElapsed / 60.);
            rollingAvg_.push_back(avgRemoved);
            auto const totalRemoved =
                std::accumulate(rollingAvg_.begin(), rollingAvg_.end(), 0.);
            rollingAvgRemoved_ = totalRemoved / rollingAvg_.size();
            intervalStart_ = node_.net_.timeSinceStart();
            totalRemoved_ = 0;
        }
    }

    std::optional<float>
    getRemoved()
    {
        if (node_.in_max_ > 100)
            return {rollingAvgRemoved_};
        else
            return std::nullopt;
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

    void
    checkStopped() override
    {
        if (isStopping() && areChildrenStopped() && list_.empty())
            stopped();
    }

    void
    onPeerDeactivate(std::shared_ptr<PeerFinder::Slot> const& slot)
    {
        Counts::deactivateCnt++;
        std::lock_guard l(peersMutex_);
        // erase if it's one of the bootstrap nodes so can connect again
        if (auto it = node_.bootstrap_.find(
                slot->remote_endpoint().address().to_string());
            it != node_.bootstrap_.end() &&
            bootstrapConnected_.find(it->second) != bootstrapConnected_.end())
            bootstrapConnected_.erase(it->second);
        peers_.erase(slot);
    }

    std::string
    name()
    {
        return name_;
    }

    std::pair<std::uint16_t, std::uint16_t>
    getPeersCounts()
    {
        std::lock_guard l(peersMutex_);
        std::uint16_t nin = 0;
        std::uint16_t nout = 0;
        for (auto [slot, peer] : peers_)
        {
            (void)slot;
            if (auto p = peer.lock())
            {
                if (p->inbound())
                    nin++;
                else
                    nout++;
            }
        }
        return {nout, nin};
    }

    void
    outputPeers(
        std::ofstream& of,
        boost::bimap<std::string, std::string> const& ip2Local)
    {
        std::lock_guard l(peersMutex_);
        for (auto [slot, peer] : peers_)
        {
            (void)slot;
            auto p = peer.lock();
            if (p)
                of << ip2Local.right.at(node_.ip_) << ","
                   << ip2Local.right.at(
                          p->getRemoteAddress().address().to_string())
                   << "," << (p->inbound() ? "in" : "out") << std::endl;
        }
    }

    std::shared_ptr<ConnectAttempt>
    mkConnectAttempt(
        beast::IP::Endpoint const& remote_endpoint,
        Resource::Consumer const& usage,
        std::shared_ptr<PeerFinder::Slot> const& slot) override
    {
        return std::make_shared<ConnectAttemptTest>(
            node_,
            p2pConfig_,
            io_service_,
            beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
            usage,
            setup_.context,
            next_id_++,
            slot,
            p2pConfig_.logs.journal("Peer"),
            *this);
    }

    /** Model a feature allowing only one connection to one of
     * the bootstrap nodes.
     */
    void
    connect(beast::IP::Endpoint const& address) override
    {
        if (node_.net_.limitConnections())
        {
            std::lock_guard l(peersMutex_);
            if (auto it = node_.bootstrap_.find(address.address().to_string());
                it != node_.bootstrap_.end() &&
                bootstrapConnected_.find(it->second) !=
                    bootstrapConnected_.end())
                return;
        }
        P2POverlayImpl::connect(address);
    }

protected:
    bool
    processRequest(http_request_type const& req, Handoff& handoff) override
    {
        return false;
    }

    void
    addPeer(std::shared_ptr<PeerImpTest> const& peer)
    {
        std::lock_guard l(peersMutex_);
        peers_.emplace(peer->slot(), peer);
        if (node_.net_.limitConnections())
        {
            if (auto it = node_.bootstrap_.find(
                    peer->getRemoteAddress().address().to_string());
                it != node_.bootstrap_.end())
                bootstrapConnected_[it->second] = true;
        }
    }

    std::shared_ptr<P2POverlayImpl::Child>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) override
    {
        auto peer = std::make_shared<PeerImpTest>(
            node_,
            id,
            slot,
            std::move(request),
            publicKey,
            protocol,
            std::move(stream_ptr),
            *this);
        Counts::inPeersCnt++;
        addPeer(peer);
        return peer;
    }

    std::shared_ptr<P2POverlayImpl::Child>
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
        auto peer = std::make_shared<PeerImpTest>(
            node_,
            std::move(stream_ptr),
            std::move(slot),
            std::move(response),
            publicKey,
            protocol,
            id,
            *this);
        Counts::outPeersCnt++;
        addPeer(peer);
        return peer;
    }

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
};

PeerImpTest::~PeerImpTest()
{
    static_cast<OverlayImplTest&>(overlay_).onPeerDeactivate(slot_);
}

void
VirtualNode::run()
{
    server_->ports(serverPort_);
    overlay_->run();
}

/** Test Overlay network with five nodes with ip in range 172.0.0.0-172.0.0.4.
 * Ip's must be pre-configured (see overlay_xrpl_test below). The test stops
 * after total 20 peers or 15 seconds.
 */
class overlay_net_test : public beast::unit_test::suite,
                         public VirtualNetwork,
                         public RootStoppable
{
protected:
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> overlayTimer_;
    // global ip:local ip
    boost::bimap<std::string, std::string> ip2Local_;
    using global_local = boost::bimap<std::string, std::string>::value_type;
    std::string baseIp_ = "172.0";
    std::uint16_t static constexpr maxSubaddr_ = 255;
    std::uint16_t tot_out_ = 0;
    std::uint16_t tot_in_ = 0;
    std::optional<std::pair<std::uint16_t, std::uint16_t>> max_default_{};
    bool batch_ = false;
    std::uint16_t duration_ = 3600;
    bool resolve_ = false;

public:
    overlay_net_test()
        : RootStoppable("overlay-test"), overlayTimer_(io_service_)
    {
    }

protected:
    void
    mkNode(
        std::string const& ip,
        bool isFixed,
        std::uint16_t out_max,
        std::uint16_t in_max,
        bool testNode = false,
        std::uint16_t peerPort = 51235) override
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
            std::cout << nodes_.size() << " " << ip << " "
                      << ip2Local_.right.at(ip) << " " << out_max << " "
                      << in_max << " " << tot_out_ << " " << tot_in_ << " "
                      << (bootstrap_.find(ip) != bootstrap_.end()
                              ? bootstrap_[ip]
                              : "")
                      << "                                \r" << std::flush;
        auto node = std::make_shared<VirtualNode>(
            *this,
            *this,
            *this,
            io_service_,
            ip,
            isFixed,
            bootstrap_,
            peerPort,
            out_max,
            in_max,
            testNode);
        add(node);
        node->run();
    }

    void
    stop() override
    {
        std::lock_guard l1(nodesMutex_);

        // cancel the timer so that
        // the terminated connection is not
        // re-connected by auto-connect
        for (auto& node : nodes_)
            node.second->overlay_->cancelTimer();

        for (auto& node : nodes_)
        {
            node.second->overlay_->stop();
            node.second->server_.reset();
        }
        io_service_.stop();
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

public:
    void
    run() override
    {
        testOverlay();
    }
};

/** Test of the Overlay network. Network configuration - adjacency matrix
 * with the type of connection (outbound/inbound) is passed in as
 * the unit test argument. The matrix can be generated by crawling XRPL
 * network. The global ip's are mapped to local 172.x.x.x ip's, which
 * must pre-configured in the system. On Ubuntu 20.20 (tested system)
 * ip's can be configured as:
 *    ip link add dummy1 type dummy
 *    ip address add 172.0.0.1/255.255.255.0 dev dummy1
 * In additions, the number of open files must be increased to 65536
 * (On Ubuntu 20.20: ulimit -n 65536, also may need to update
 * /etc/security/limits.conf, /etc/sysctl.conf, /etc/pam.d/common-session,
 * /etc/systemd/system.conf). The test runs until no changes are detected in the
 * network - the number of in/out peers remains the same after four minutes.
 */
class overlay_xrpl_test : public overlay_net_test
{
    // Network configuration of outbound/inbound max peer for each node.
    std::map<std::string, std::map<std::string, std::uint16_t>> netConfig_;
    // Total outbound/inbound peers in the network at each logged time point.
    // We stop when the number of peers doesn't change after a few iterations.
    std::vector<std::uint16_t> tot_peers_out_;
    std::vector<std::uint16_t> tot_peers_in_;
    // options
    std::string adjMatrixPath_ = "";
    // add a node to test how well it can get connected
    bool nodeAdded_ = false;
    // set to true when avg % of filled out/in slot is >= 96%
    bool networkStable_ = false;

public:
    /** Set bootstrap_, netConfig_, and ip2Local_. Use the adjacency matrix.
     * Map global ip to local ip.
     */
    void
    getNetConfig()
    {
        std::
            map<std::string, std::map<std::string, std::map<std::string, bool>>>
                all;
        std::uint16_t cnt = 1;
        std::fstream file(adjMatrixPath_, std::ios::in);
        assert(file.is_open());
        std::string line;
        // map to local ip
        auto map2Local = [&](std::string const& ip) {
            if (ip2Local_.left.find(ip) == ip2Local_.left.end())
            {
                std::stringstream str;
                str << baseIp_ << "." << (cnt / (maxSubaddr_ + 1)) << "."
                    << (cnt % (maxSubaddr_ + 1));
                ip2Local_.insert(global_local(ip, str.str()));
                cnt++;
            }
            return ip2Local_.left.at(ip);
        };
        // for each ip figure out out_max and in_max.
        // for an entry ip,ip1,'in|out'
        // increment ip:max_in|max_out.
        // for each ip,ip1,'in' and ip,ip1,'out' entry,
        // if corresponding ip1,ip,'out' or ip1,ip,'in'
        // are not present then increment ip1:max_out|max_in
        while (getline(file, line))
        {
            boost::smatch match;
            boost::regex rx("^([^,]+),([^,]+),(in|out)");
            assert(boost::regex_search(line, match, rx));
            auto const ip = map2Local(match[1]);
            auto const ip1 = map2Local(match[2]);
            auto const ctype = match[3];
            if (all.find(ip) == all.end() || all[ip].find(ip1) == all[ip].end())
                netConfig_[ip][ctype]++;
            all[ip][ip1][ctype] = true;
            if (all.find(ip1) == all.end() ||
                all[ip1].find(ip) == all[ip1].end())
            {
                std::string const t = (ctype == "in") ? "out" : "in";
                all[ip1][ip][t] = true;
                netConfig_[ip1][t] += 1;
            }
        }
        // Figure out which ips in the adjacency matrix represent
        // the bootstrap servers of ripple, alloy, and isrdc. Those
        // servers are added to each node's ips configuration as "local"
        // ip's.
        auto mapLocal = [&](auto ip, auto host) {
            if (auto it1 = ip2Local_.left.find(ip); it1 != ip2Local_.left.end())
                bootstrap_[it1->second] = host;
        };
        // manual resolve for offline testing
        if (resolve_)
        {
            auto resolveManual = [&](auto hosts, auto host) {
                std::for_each(hosts.begin(), hosts.end(), [&](auto& it) {
                    mapLocal(it, host);
                });
            };
            std::vector<std::string> ripple = {
                "34.205.233.231",
                "169.55.164.29",
                "198.11.206.6",
                "169.55.164.21",
                "198.11.206.26",
                "52.25.71.90",
                "3.216.68.48",
                "54.190.253.12"};
            resolveManual(ripple, "r.ripple.ee");
            std::vector<std::string> alloy = {
                "46.4.218.119",
                "88.99.137.170",
                "116.202.148.26",
                "136.243.24.38",
                "95.216.102.188",
                "46.4.138.103",
                "46.4.218.120",
                "116.202.163.130",
                "95.216.102.182",
                "94.130.221.2",
                "95.216.5.218"};
            resolveManual(alloy, "zaphod.alloy.ee");
            std::vector<std::string> isrdc = {"59.185.224.109"};
            resolveManual(isrdc, "sahyadri.isrdc.in");
        }
        else
        {
            auto resolve = [&](auto host) {
                boost::asio::ip::tcp::resolver resolver(io_service_);
                boost::asio::ip::tcp::resolver::query query(host, "80");
                boost::asio::ip::tcp::resolver::iterator iter =
                    resolver.resolve(query);
                std::for_each(iter, {}, [&](auto& it) {
                    auto ip = it.endpoint().address().to_string();
                    mapLocal(ip, host);
                });
            };
            resolve("r.ripple.com");
            resolve("zaphod.alloy.ee");
            resolve("sahyadri.isrdc.in");
        }
    }

    bool
    parseArg()
    {
        if (arg() == "")
            return false;
        std::cout << arg() << std::endl;
        boost::char_separator<char> sep(",");
        boost::char_separator<char> sep1(":");
        typedef boost::tokenizer<boost::char_separator<char>> t_tokenizer;
        t_tokenizer tok(arg(), sep);
        for (auto it = tok.begin(); it != tok.end(); ++it)
        {
            if (adjMatrixPath_ == "")
                adjMatrixPath_ = *it;
            else if (*it == "limit")
                limitBootstrapConnections_ = true;
            else if (*it == "prune")
                handleInboundPrunning_ = true;
            else if ((*it).substr(0, 3) == "ip:")
                baseIp_ = (*it).substr(3);
            else if (*it == "batch")
                batch_ = true;
            else if ((*it).substr(0, 9) == "duration:")
                duration_ = std::stoi((*it).substr(9));
            else if (*it == "resolve")
                resolve_ = true;
            else if ((*it).substr(0, 4) == "max:")
            {
                std::string const val = (*it).substr(4);
                t_tokenizer tok1(val, sep1);
                auto it1 = tok1.begin();
                std::uint16_t max_out = std::stoi(*it1);
                std::uint16_t max_in = std::stoi(*(++it1));
                max_default_ = {std::make_pair(max_out, max_in)};
            }
            else
            {
                std::cout << "invalid argument " << *it << std::endl;
                return false;
            }
        }
        return (adjMatrixPath_ != "");
    }

    void
    testXRPLOverlay()
    {
        testcase("XRPLOverlay");
        if (!parseArg())
        {
            fail("adjacency matrix must be provided");
            return;
        }

        std::remove("stop");
        std::remove("add");

        getNetConfig();
        startNodes();
        BEAST_EXPECT(Counts::deactivated());
        BEAST_EXPECT(
            Counts::msgSendCnt > 0 && Counts::msgSendCnt == Counts::msgRecvCnt);
    }

    void
    startNodes()
    {
        std::vector<std::string> ips;
        ips.reserve(netConfig_.size());
        std::for_each(netConfig_.begin(), netConfig_.end(), [&](auto it) {
            ips.push_back(it.first);
        });
        std::random_shuffle(ips.begin(), ips.end());
        for (auto ip : ips)
            mkNode(ip, false, netConfig_[ip]["out"], netConfig_[ip]["in"]);
        std::cout << "total out: " << tot_out_ << ", total in: " << tot_in_
                  << "                             \n";
        setTimer();
        for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
            tg_.create_thread(
                boost::bind(&boost::asio::io_service::run, &io_service_));
        tg_.join_all();
    }

    void
    outputNetwork()
    {
        std::ofstream of("network.out");
        for (auto [id, node] : nodes_)
            node->overlay_->outputPeers(of, ip2Local_);
    }

    void
    onOverlayTimer(boost::system::error_code const& ec)
    {
        if (ec)
        {
            stop();
            return;
        }

        std::ifstream inf("stop");
        if (timeSinceStart() > duration_ || inf.good() || !doLog())
        {
            outputNetwork();
            stop();
        }
        else
        {
            // addNode();
            setTimer();
        }
    }

    /** Add a node to test network connectivity
     */
    void
    addNode()
    {
        std::ifstream inf("add");
        if (!nodeAdded_ && (inf.good() || networkStable_))
        {
            std::string const node("172.0.2.237");
            std::cout << "added node " << node << std::endl;
            nodeAdded_ = true;
            ip2Local_.insert(global_local(node, node));
            mkNode(node, false, 10, 10, true);
        }
    }

    void
    setTimer()
    {
        overlayTimer_.expires_from_now(std::chrono::seconds(40));
        overlayTimer_.async_wait(std::bind(
            &overlay_xrpl_test::onOverlayTimer, this, std::placeholders::_1));
    }

    bool
    doLog()
    {
        using namespace std::chrono;
        std::vector<float> pct_out;
        std::vector<float> pct_in;
        std::vector<float> pct_def_out;
        std::vector<float> pct_def_in;
        std::vector<std::uint16_t> peers_out;
        std::vector<std::uint16_t> peers_in;
        std::uint16_t Nin = 0;
        std::uint16_t Nout = 0;
        float avg_pct_out = 0.;
        float avg_pct_in = 0.;
        float avg_pct_def_out = 0.;
        float avg_pct_def_in = 0.;
        float avg_peers_out = 0.;
        float avg_peers_in = 0.;
        std::uint16_t out_max = 0;
        std::uint16_t in_max = 0;
        std::uint16_t tot_out = 0;
        std::uint16_t tot_in = 0;
        std::uint16_t no_peers = 0;
        std::vector<float> removed;
        float avg_removed = 0;
        for (auto [id, node] : nodes_)
        {
            (void)id;
            auto [nout, nin] = node->overlay_->getPeersCounts();
            if ((nout + nin) == 0)
                no_peers++;
            if (node->out_max_ > 0)
            {
                tot_out += nout;
                Nout++;
                avg_peers_out += nout;
                if (nout > out_max)
                    out_max = nout;
                peers_out.push_back(nout);
                avg_pct_out += 100. * nout / node->out_max_;
                pct_out.push_back(100. * nout / node->out_max_);
            }
            if (node->in_max_ > 0)
            {
                tot_in += nin;
                Nin++;
                avg_peers_in += nin;
                if (nin > in_max)
                    in_max = nin;
                peers_in.push_back(nin);
                avg_pct_in += 100. * nin / node->in_max_;
                pct_in.push_back(100. * nin / node->in_max_);
            }
            std::uint16_t deflt = max_default_
                ? (max_default_->first + max_default_->second)
                : 21;
            if (node->in_max_ > 0 && (node->out_max_ + node->in_max_ <= deflt))
            {
                avg_pct_def_in += 100. * nin / node->in_max_;
                avg_pct_def_out += 100. * nout / node->out_max_;
                pct_def_out.push_back(100. * nout / node->out_max_);
                pct_def_in.push_back(100. * nin / node->in_max_);
            }
            if (auto r = node->overlay_->getRemoved(); r)
            {
                avg_removed += *r;
                removed.push_back(*r);
            }
        }
        auto stats = [](auto& avg, auto N, auto sample) {
            avg = avg / N;
            float sd = 0.;
            for (auto d : sample)
                sd += (d - avg) * (d - avg);
            if (N > 1)
                sd = sqrt(sd) / (N - 1);
            return sd;
        };
        auto sd_peers_out = stats(avg_peers_out, Nout, peers_out);
        auto sd_peers_in = stats(avg_peers_in, Nin, peers_in);
        auto sd_pct_out = stats(avg_pct_out, Nout, pct_out);
        auto sd_pct_in = stats(avg_pct_in, Nin, pct_in);
        auto sd_pct_def_out =
            stats(avg_pct_def_out, pct_def_out.size(), pct_def_out);
        auto sd_pct_def_in =
            stats(avg_pct_def_in, pct_def_in.size(), pct_def_in);
        auto [an_redirect, sum, redirects] = getRedirects();
        auto sd_redirects = stats(sum, redirects.size(), redirects);
        std::uint16_t anout = 0;
        std::uint16_t anin = 0;
        std::uint16_t max_anout = 0;
        std::uint16_t max_anin = 0;
        if (nodes_[VirtualNode::sid_ - 1]->testNode_)
        {
            auto res =
                nodes_[VirtualNode::sid_ - 1]->overlay_->getPeersCounts();
            anout = res.first;
            anin = res.second;
            max_anout = nodes_[VirtualNode::sid_ - 1]->out_max_;
            max_anin = nodes_[VirtualNode::sid_ - 1]->in_max_;
        }
        auto sd_removed = stats(avg_removed, removed.size(), removed);
        {
            std::cout.precision(2);
            std::cout << timeSinceStart() << ", out: " << tot_out
                      << ", in: " << tot_in << ", snd: " << Counts::msgSendCnt
                      << ", rcv: " << Counts::msgRecvCnt
                      << ", deact: " << Counts::deactivateCnt
                      << ", max out/in: " << out_max << "/" << in_max
                      << ", avg out/in: " << avg_peers_out << "/"
                      << sd_peers_out << ", " << avg_peers_in << "/"
                      << sd_peers_in << ", avg pct out/in: " << avg_pct_out
                      << "/" << sd_pct_out << ", " << avg_pct_in << "/"
                      << sd_pct_in
                      << ", avg pct default out/in: " << avg_pct_def_out << "/"
                      << sd_pct_def_out << ", " << avg_pct_def_in << "/"
                      << sd_pct_def_in << ", no peers: " << no_peers
                      << ", removed: " << avg_removed << "/" << sd_removed
                      << ", redirects: " << sum << "/" << sd_redirects
                      << ", add node out/in/redirect " << max_anout << "/"
                      << anout << ", " << max_anin << "/" << anin << ", "
                      << an_redirect << std::endl
                      << std::flush;
        }
        if (tot_peers_in_.size() > 0 &&
            tot_peers_in_[tot_peers_in_.size() - 1] != tot_in)
            tot_peers_in_.clear();
        if (tot_peers_out_.size() > 0 &&
            tot_peers_out_[tot_peers_out_.size() - 1] != tot_out)
            tot_peers_out_.clear();
        tot_peers_in_.push_back(tot_in);
        tot_peers_out_.push_back(tot_out);
        if (avg_pct_out >= 96 && avg_pct_in >= 96)
            networkStable_ = true;
        // stop if the network doesn't change
        if (tot_peers_in_.size() >= 6 and tot_peers_out_.size() >= 6)
        {
            return false;
        }
        return true;
    }

    void
    run() override
    {
        testXRPLOverlay();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(overlay_net, ripple_data, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(overlay_xrpl, ripple_data, ripple);

}  // namespace test

}  // namespace ripple