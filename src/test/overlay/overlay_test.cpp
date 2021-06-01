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
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/Stoppable.h>
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
#include <boost/thread.hpp>

#include <memory>
#include <mutex>

namespace ripple {

namespace test {

class OverlayImplTest;
class VirtualNetwork;
std::mutex logMutex;

static std::string
name(std::string const& n, int i)
{
    return n + std::to_string(i);
}

struct Counts
{
    static inline std::recursive_mutex cntMutex;
    static inline std::condition_variable_any cond;
    static inline std::uint16_t msgSendCnt = 0;
    static inline std::uint16_t msgRecvCnt = 0;
    static inline std::uint16_t inPeersCnt = 0;
    static inline std::uint16_t outPeersCnt = 0;
    static inline std::uint16_t deactivateCnt = 0;
    static inline std::uint16_t expectedMsgCnt = 0;
    static void
    incCnt(std::uint16_t& cnt)
    {
        std::lock_guard l(cntMutex);
        cnt++;
        cond.notify_all();
    }
    static bool
    deactivated()
    {
        return deactivateCnt == inPeersCnt + outPeersCnt;
    }
    static void
    waitDeactivated()
    {
        using namespace std::chrono;
        std::unique_lock l(cntMutex);
        cond.wait_for(l, 5s, [] { return deactivated(); });
    }
    static bool
    expected()
    {
        std::unique_lock l(cntMutex);
        return expectedMsgCnt == (msgSendCnt + msgRecvCnt);
    }
};

// All objects needed to run the overlay
struct VirtualNode
{
    VirtualNode(
        VirtualNetwork& net,
        beast::unit_test::suite& suite,
        Stoppable& parent,
        boost::asio::io_service& service,
        std::string const& ip,
        std::vector<std::string> const& ipsFixed,
        std::uint16_t peerPort)
        : net_(net)
        , ip_(ip)
        , id_(sid_)
        , io_service_(service)
        , config_(mkConfig(ip, std::to_string(peerPort), ipsFixed))
        , logs_(std::make_unique<jtx::SuiteLogs>(suite))
        , cluster_(std::make_unique<Cluster>(logs_->journal("Cluster")))
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
              net,
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
        std::vector<std::string> const& ipsFixed,
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

        if (http)
        {
            (*config)["server"].append("port_rpc");
            (*config)["port_rpc"].set("ip", ip);
            (*config)["port_rpc"].set("port", "6006");
            (*config)["port_rpc"].set("protocol", "http");
        }
        (*config)["ssl_verify"].append("0");
        for (auto f : ipsFixed)
            config->IPS_FIXED.push_back(f + " " + peerPort);
        config->setupControl(true, true, false);
        return config;
    }
    static inline int sid_ = 0;
    VirtualNetwork& net_;
    std::string ip_;
    int id_;
    boost::asio::io_service& io_service_;
    std::unique_ptr<Config> config_;
    std::unique_ptr<jtx::SuiteLogs> logs_;
    std::unique_ptr<Cluster> cluster_;
    std::unique_ptr<ManualTimeKeeper> timeKeeper_;
    std::unique_ptr<CollectorManager> collector_;
    std::unique_ptr<Resource::Manager> resourceManager_;
    std::unique_ptr<ResolverAsio> resolver_;
    std::pair<PublicKey, SecretKey> identity_;
    PeerReservationTable reservations_;
    std::shared_ptr<OverlayImplTest> overlay_;
    std::vector<Port> serverPort_;
    std::unique_ptr<Server> server_;
    std::string name_;
};

// Collection of overlays
class VirtualNetwork
{
    friend class overlay_net_test;

protected:
    boost::asio::io_service io_service_;
    boost::thread_group tg_;
    std::mutex nodesMutex_;
    std::unordered_map<int, std::shared_ptr<VirtualNode>> nodes_;

public:
    virtual ~VirtualNetwork() = default;

    virtual void
    stop() = 0;

    boost::asio::io_service&
    io_service()
    {
        return io_service_;
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
        std::vector<std::string> fixed,
        int peerPort = 51235) = 0;
    void
    startNets(std::vector<std::string> const& nodes)
    {
        for (auto n : nodes)
            mkNode(n, nodes);
        for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
            tg_.create_thread(
                boost::bind(&boost::asio::io_service::run, &io_service_));
        tg_.join_all();
    }
};

class PeerImpTest : public DefaultPeerImp<PeerImpTest>
{
    friend class P2PeerImp<PeerImpTest>;
    VirtualNetwork& net_;
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
        , net_(node.net_)
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
        , net_(node.net_)
        , node_(node)
    {
    }

    ~PeerImpTest();

    // close peer connection
    void
    closeConnection()
    {
        if (!strand_.running_in_this_thread())
            return post(
                strand_,
                std::bind(
                    &PeerImpTest::closeConnection,
                    std::static_pointer_cast<PeerImpTest>(shared_from_this())));
        close();
    }

protected:
    // P2P events/methods
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
                Counts::incCnt(Counts::msgRecvCnt);
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
        {
            std::lock_guard l(logMutex);
            std::cout << node_.name_ << " received from " << remote_address_
                      << std::endl;
        }
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

class OverlayImplTest : public DefaultOverlayImpl,
                        public std::enable_shared_from_this<OverlayImplTest>
{
private:
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::mutex peersMutex_;
    std::unordered_map<
        std::shared_ptr<PeerFinder::Slot>,
        std::weak_ptr<PeerImpTest>>
        peers_;
    VirtualNetwork& net_;
    VirtualNode& node_;
    std::string const name_;
    AppConfigRequestorTest requestor_;

public:
    virtual ~OverlayImplTest()
    {
        timer_.cancel();
    }

    OverlayImplTest(
        VirtualNetwork& net,
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
        , net_(net)
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
    }

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

    void
    checkStopped() override
    {
        if (isStopping() && areChildrenStopped() && list_.empty())
            stopped();
    }

    // close all peers connection
    void
    closeConnections()
    {
        std::lock_guard l(peersMutex_);
        for (auto& peer : peers_)
        {
            auto sp = peer.second.lock();
            if (sp)
                sp->closeConnection();
        }
    }

    void
    onPeerDeactivate(std::shared_ptr<PeerFinder::Slot> const& slot)
    {
        Counts::incCnt(Counts::deactivateCnt);
        std::lock_guard l(peersMutex_);
        peers_.erase(slot);
    }

    std::string
    name()
    {
        return name_;
    }

protected:
    bool
    processRequest(http_request_type const& req, Handoff& handoff) override
    {
        return false;
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
        Counts::incCnt(Counts::inPeersCnt);
        std::lock_guard l(peersMutex_);
        peers_.emplace(peer->slot(), peer);
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
        Counts::incCnt(Counts::outPeersCnt);
        std::lock_guard l(peersMutex_);
        peers_.emplace(peer->slot(), peer);
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
                auto const iter = peers_.find(e.first);
                if (iter != peers_.end())
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
                {
                    std::lock_guard l(logMutex);
                    std::cout << name_ << ": sending to "
                              << peer->getRemoteAddress() << std::endl;
                }
                Counts::incCnt(Counts::msgSendCnt);
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

class overlay_net_test : public beast::unit_test::suite,
                         public VirtualNetwork,
                         public RootStoppable
{
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> overlayTimer_;

public:
    overlay_net_test()
        : RootStoppable("overlay-test"), overlayTimer_(io_service_)
    {
    }

protected:
    void
    mkNode(
        std::string const& ip,
        std::vector<std::string> fixed,
        int peerPort = 51235) override
    {
        fixed.erase(std::find(fixed.begin(), fixed.end(), ip));
        auto net = std::make_shared<VirtualNode>(
            *this, *this, *this, io_service_, ip, fixed, peerPort);
        add(net);
        net->run();
    }

    void
    stop() override
    {
        std::lock_guard l1(nodesMutex_);
        overlayTimer_.cancel();

        // cancel the timer so that
        // the terminated connection is not
        // re-connected by auto-connect
        for (auto& node : nodes_)
            node.second->overlay_->cancelTimer();

        for (auto& node : nodes_)
        {
            node.second->overlay_->closeConnections();
            Counts::waitDeactivated();
            node.second->server_.reset();
        }
        io_service_.stop();
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        // interfaces must be pre-configured
        std::vector<std::string> nets = {
            "172.0.0.0", "172.0.0.1", "172.0.0.2", "172.0.0.3", "172.0.0.4"};
        overlayTimer_.expires_from_now(std::chrono::seconds(15));
        overlayTimer_.async_wait(std::bind(
            &overlay_net_test::onOverlayTimer, this, std::placeholders::_1));
        startNets(nets);
        std::lock_guard l(logMutex);
        std::cout << "peers " << Counts::inPeersCnt << " "
                  << Counts::outPeersCnt << " " << Counts::deactivateCnt
                  << std::endl;
        std::cout << "messages " << Counts::msgRecvCnt << " "
                  << Counts::msgSendCnt << std::endl;
        BEAST_EXPECT(Counts::deactivated());
        BEAST_EXPECT(
            Counts::msgSendCnt > 0 && Counts::msgSendCnt == Counts::msgRecvCnt);
    }

    void
    onOverlayTimer(boost::system::error_code const& ec)
    {
        if (ec)
            return;
        stop();
    }

public:
    void
    run() override
    {
        testOverlay();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(overlay_net, ripple_data, ripple);

}  // namespace test

}  // namespace ripple