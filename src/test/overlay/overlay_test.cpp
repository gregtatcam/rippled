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
#include <test/jtx/Env.h>

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include <memory>

namespace ripple {

namespace test {

class OverlayImplTest;

static std::string
name(std::string const& n, int i)
{
    return n + std::to_string(i);
}

struct PseudoNet
{
    PseudoNet(
        beast::unit_test::suite& suite,
        boost::asio::io_service& service,
        std::string const& ip,
        std::vector<std::string> const& ipsFixed,
        int peerPort)
        : io_service_(service)
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
        , identity_(randomKeyPair(KeyType::ed25519))
        , overlay_(std::make_shared<OverlayImplTest>(
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
        id_++;
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
    static inline int id_ = 0;
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

class PeerImpTest : public P2PeerImp
{
public:
    PeerImpTest(
        PseudoNet& net,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              *net.logs_,
              *net.config_,
              id,
              slot,
              std::move(request),
              publicKey,
              protocol,
              std::move(stream_ptr),
              overlay)
    {
    }

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    PeerImpTest(
        PseudoNet& net,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              *net.logs_,
              *net.config_,
              std::move(stream_ptr),
              buffers,
              std::move(slot),
              std::move(response),
              publicKey,
              protocol,
              id,
              overlay)
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
        std::cout << "protocol started event\n";
    }

    void
    onEvtRun() override
    {
        std::cout << "run event\n";
    }

    void
    onEvtClose() override
    {
        std::cout << "close event\n";
    }

    void
    onEvtGracefulClose() override
    {
        std::cout << "graceful close event\n";
    }

    void
    onEvtShutdown() override
    {
        std::cout << "shutdown event\n";
    }

    std::pair<size_t, boost::system::error_code>
    onEvtProtocolMessage(boost::beast::multi_buffer const&, size_t&) override
    {
        std::cout << "protocol message event\n";
        return {};
    }
};

class OverlayImplTest : public P2POverlayImpl,
                        public std::enable_shared_from_this<OverlayImplTest>
{
private:
    PseudoNet& net_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::string const name_;

public:
    OverlayImplTest(
        PseudoNet& net,
        std::uint16_t overlayPort,
        std::string const& name)
        : P2POverlayImpl(
              [&]() -> HandshakeConfig {
                  HandshakeConfig hconfig{
                      *net.logs_,
                      net.identity_,
                      *net.config_,
                      nullptr,
                      net.timeKeeper_->now()};
                  return hconfig;
              }(),
              *net.cluster_,
              net.reservations_,
              true,
              setup_Overlay(*net.config_),
              overlayPort,
              *net.resourceManager_,
              *net.resolver_,
              net.io_service_,
              *net.config_,
              net.collector_->collector())
        , net_(net)
        , timer_(net.io_service_)
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
    onTimer(boost::system::error_code ec)
    {
        std::cout << "onTimer " << name_ << std::endl << std::flush;
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
            net_,
            id,
            slot,
            std::move(request),
            publicKey,
            protocol,
            std::move(stream_ptr),
            *this);
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
            net_,
            std::move(stream_ptr),
            buffers.data(),
            std::move(slot),
            std::move(response),
            publicKey,
            protocol,
            id,
            *this);
        return peer;
    }

    void
    onPeerDeactivate(
        P2Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot) override
    {
        std::cout << "deactivating peer\n";
    }
};

void
PseudoNet::run()
{
    server_->ports(serverPort_);
    overlay_->run();
}

class overlay_net_test : public beast::unit_test::suite
{
    boost::asio::io_service io_service_;

public:
    overlay_net_test()
    {
    }

    std::vector<std::shared_ptr<PseudoNet>> nets_;

    void
    mkNet(
        std::string const& i,
        std::vector<std::string> const& fixed,
        std::string const& baseIp = "172.0.0.",
        int peerPort = 51235)
    {
        std::string ip = baseIp + i;
        std::cout << "configuring " << i << std::endl << std::flush;
        std::cout << "-------------------\n" << std::endl << std::flush;
        auto net = std::make_shared<PseudoNet>(
            *this, io_service_, ip, fixed, peerPort);
        nets_.emplace_back(net);
        net->run();
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        mkNet("0", {"172.0.0.1"});
        mkNet("1", {"172.0.0.0"});
        boost::thread_group tg;
        for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
            tg.create_thread(
                boost::bind(&boost::asio::io_service::run, &io_service_));
        tg.join_all();
    }

    void
    run() override
    {
        testOverlay();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(overlay_net, ripple_data, ripple);

}  // namespace test

}  // namespace ripple