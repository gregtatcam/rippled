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
#include <ripple/basics/ResolverAsio.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/make_Overlay.h>
#include <ripple_test.pb.h>
#include <test/jtx/Env.h>

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
    static inline std::uint16_t msgSendCnt = 0;
    static inline std::uint16_t msgRecvCnt = 0;
    static inline std::uint16_t inPeersCnt = 0;
    static inline std::uint16_t outPeersCnt = 0;
};

// All objects needed to run the overlay
struct VirtualNode
{
    VirtualNode(
            VirtualNetwork& net,
            beast::unit_test::suite& suite,
            boost::asio::io_service& service,
            std::string const& ip,
            std::vector<std::string> const& ipsFixed,
            int peerPort)
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
                    peerPort,
                    name("Overlay", id_)))
        , serverPort_(1)
        , server_(make_Server(
              *overlay_,
              io_service_,
              logs_->journal(name("Server", id_))))
    {
        serverPort_.back().ip = beast::IP::Address::from_string(ip);
        serverPort_.back().port = peerPort;
        serverPort_.back().protocol.insert("peer");
        serverPort_.back().context = make_SSLContext("");
        sid_++;
    }
    void
    run();
    std::unique_ptr<Config>
    mkConfig(
        std::string const& ip,
        std::string const& peerPort,
        std::vector<std::string> const& ipsFixed)
    {
        auto config = std::make_unique<Config>();
        config->overwrite(ConfigSection::nodeDatabase(), "type", "memory");
        config->overwrite(ConfigSection::nodeDatabase(), "path", "main");
        config->deprecatedClearSection(ConfigSection::importNodeDatabase());
        (*config)["server"].append("port_peer");
        (*config)["port_peer"].set("ip", ip);
        (*config)["port_peer"].set("port", peerPort);
        (*config)["port_peer"].set("protocol", "peer");
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

class PeerImpTest : public P2PeerImp
{
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
        : P2PeerImp(
              *node.logs_,
              *node.config_,
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
    template <class Buffers>
    PeerImpTest(
        VirtualNode& node,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              *node.logs_,
              *node.config_,
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

protected:
    std::string
    name() const override
    {
        return "";
    }
    bool
    squelched(std::shared_ptr<Message> const&) override
    {
        return false;
    }

    void
    onEvtProtocolStart() override
    {
        protocol::TMSendTest tms;
        tms.set_version(101);
        Counts::msgSendCnt++;
        this->send(std::make_shared<Message>(tms, protocol::mtSEND_TEST));
    }

    void
    onEvtRun() override
    {
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

    std::pair<size_t, boost::system::error_code>
    onEvtProtocolMessage(
        boost::beast::multi_buffer const& mbuffers,
        size_t& hint) override
    {
        std::pair<std::size_t, boost::system::error_code> result = {0, {}};
        bool status = false;

        auto buffers = mbuffers.data();

        auto header = getHeader(buffers, *this, hint);

        if (!header.first)
        {
            result.second = header.second;
            return result;
        }

        if (header.second == boost::system::errc::no_message)
        {
            result.second = {};
            return result;
        }

        switch (header.first->message_type)
        {
            case protocol::mtSEND_TEST: {
                if (auto const m =
                        detail::parseMessageContent<protocol::TMSendTest>(
                            *header.first, buffers))
                {
                    std::lock_guard l(logMutex);
                    Counts::msgRecvCnt++;
                    if (Counts::msgRecvCnt == 40 && Counts::msgSendCnt == 40)
                        node_.net_.stop();
                    status = true;
                }
            }
            break;
        }

        result.first = header.first->total_wire_size;
        if (!status)
            result.second = make_error_code(boost::system::errc::bad_message);

        return result;
    }
};

class OverlayImplTest : public P2POverlayImpl,
                        public std::enable_shared_from_this<OverlayImplTest>
{
private:
    VirtualNetwork& net_;
    VirtualNode& node_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::string const name_;

public:
    ~OverlayImplTest()
    {
        timer_.cancel();
    }

    OverlayImplTest(
            VirtualNetwork& net,
            VirtualNode& node,
            std::uint16_t overlayPort,
            std::string const& name)
        : P2POverlayImpl(
              [&]() -> HandshakeConfig {
                  HandshakeConfig hconfig{
                      *node.logs_,
                      node.identity_,
                      *node.config_,
                      nullptr,
                      node.timeKeeper_->now()};
                  return hconfig;
              }(),
              *node.cluster_,
              node.reservations_,
              true,
              setup_Overlay(*node.config_),
              overlayPort,
              *node.resourceManager_,
              *node.resolver_,
              node.io_service_,
              *node.config_,
              node.collector_->collector())
        , net_(net)
        , node_(node)
        , timer_(node.io_service_)
        , name_(name)
    {
    }

    void
    setTimer()
    {
        timer_.expires_from_now(std::chrono::seconds(1));
        timer_.async_wait(strand().wrap(std::bind(
            &OverlayImplTest::onTimer,
            shared_from_this(),
            std::placeholders::_1)));
    }

    void
    run()
    {
        doStart();
        setTimer();
    }

    void
    onTimer(boost::system::error_code const& ec)
    {
        if (ec)
            return;
        // peerFinder().once_per_second();
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

protected:
    bool
    processRequest(http_request_type const& req, Handoff& handoff) override
    {
        return false;
    }

    std::shared_ptr<P2PeerImp>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        Resource::Consumer consumer,
        ProtocolVersion protocol,
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
        auto peer = std::make_shared<PeerImpTest>(
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
        return peer;
    }

    void
    onPeerDeactivate(
        P2Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot) override
    {
    }
};

void
VirtualNode::run()
{
    server_->ports(serverPort_);
    overlay_->run();
}

class overlay_net_test : public beast::unit_test::suite, public VirtualNetwork
{
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> overlayTimer_;
public:
    overlay_net_test() : overlayTimer_(io_service_)
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
            *this, *this, io_service_, ip, fixed, peerPort);
        add(net);
        net->run();
    }

    void
    stop() override
    {
        std::lock_guard l1(nodesMutex_);
        overlayTimer_.cancel();
        // TODO nodes cleanup
        io_service_.stop();
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        // interfaces must be pre-configured
        std::vector<std::string> nets = {
            "172.0.0.0", "172.0.0.1", "172.0.0.2", "172.0.0.3", "172.0.0.4"};
        overlayTimer_.expires_from_now(std::chrono::seconds(10));
        overlayTimer_.async_wait(std::bind(
                &overlay_net_test::onOverlayTimer,
                this,
                std::placeholders::_1));
        startNets(nets);
    }

    void
    onOverlayTimer(boost::system::error_code const& ec)
    {
        if (ec)
            return;
        std::lock_guard l(logMutex);
        std::cout << "peers " << Counts::inPeersCnt << " " << Counts::outPeersCnt << std::endl;
        std::cout << "messages " << Counts::msgRecvCnt << " " << Counts::msgSendCnt << std::endl;
        BEAST_EXPECT(Counts::inPeersCnt == 20);
        BEAST_EXPECT(Counts::outPeersCnt == 20);
        BEAST_EXPECT(Counts::msgSendCnt == 40);
        BEAST_EXPECT(Counts::msgRecvCnt == 40);
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