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

namespace ripple {

namespace test {

class OverlayImplTest;
class TestHandler;

struct PseudoNet
{
    PseudoNet(boost::asio::io_service& service) : io_service_(service)
    {
        identity_ = randomKeyPair(KeyType::ed25519);
    }
    boost::asio::io_service& io_service_;
    PeerReservationTable reservations_;
    std::unique_ptr<OverlayImplTest> overlay_;
    std::unique_ptr<ResolverAsio> resolver_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<TestHandler> handler_;
    std::unique_ptr<CollectorManager> collector_;
    std::unique_ptr<jtx::SuiteLogs> logs_;
    std::unique_ptr<ManualTimeKeeper> timeKeeper_;
    std::unique_ptr<Resource::Manager> resourceManager_;
    std::unique_ptr<Config> config_;
    std::unique_ptr<Cluster> cluster_;
    std::pair<PublicKey, SecretKey> identity_;
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

class OverlayImplTest : public P2POverlayImpl
{
private:
    PseudoNet& net_;

public:
    OverlayImplTest(PseudoNet& net, std::uint16_t overlayPort)
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
    {
    }

    void
    run()
    {
        doStart();
        autoConnect();
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

struct TestHandler
{
    OverlayImplTest& overlay_;

    TestHandler(OverlayImplTest& overlay) : overlay_(overlay)
    {
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
        return overlay_.onHandoff(
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
};

class overlay_net_test : public beast::unit_test::suite
{
    boost::asio::io_service io_service_;

public:
    overlay_net_test()
    {
    }

    std::vector<std::unique_ptr<PseudoNet>> nets_;

    void
    mkNet(std::string const& i, std::vector<std::string> const& fixed)
    {
        auto net = std::make_unique<PseudoNet>(io_service_);
        net->logs_ = std::make_unique<jtx::SuiteLogs>(*this);
        net->cluster_ =
            std::make_unique<Cluster>(net->logs_->journal("Cluster"));
        net->timeKeeper_ = std::make_unique<ManualTimeKeeper>();
        net->config_ = std::make_unique<Config>();
        std::string ip = "172.0.0." + i;
        (*net->config_)["server"].append("port_peer");
        (*net->config_)["port_peer"].set("ip", ip);
        (*net->config_)["port_peer"].set("port", "5005");
        (*net->config_)["port_peer"].set("protocol", "peer");
        (*net->config_)["server"].append("port_rpc_admin_local");
        (*net->config_)["port_rpc_admin_local"].set("ip", ip);
        (*net->config_)["port_rpc_admin_local"].set("port", "6006");
        (*net->config_)["port_rpc_admin_local"].set("protocol", "http");
        (*net->config_)["port_rpc_admin_local"].set("admin", "0.0.0.0");
        std::string dbpath =
            "/home/gregt/Documents/Projects/private-test-net/Nodes/Node" + i +
            "/db";
        net->config_->legacy("database_path", dbpath);
        for (auto i : fixed)
            (*net->config_)["ips_fixed"].append(i + " 51235");
        net->config_->overwrite(
            ConfigSection::nodeDatabase(), "type", "memory");
        net->config_->overwrite(ConfigSection::nodeDatabase(), "path", "main");
        net->config_->deprecatedClearSection(
            ConfigSection::importNodeDatabase());
        // application runs server handler, which opens ports, set standalone to
        // true to disable the peer protocol
        net->config_->setupControl(true, true, false);
        net->collector_ = CollectorManager::New(
            net->config_->section(SECTION_INSIGHT),
            net->logs_->journal("Collector"));
        net->resourceManager_ = Resource::make_Manager(
            net->collector_->collector(), net->logs_->journal("Resource"));
        std::string name = "OverlayTest" + i;
        net->resolver_ =
            ResolverAsio::New(net->io_service_, net->logs_->journal(name));
        net->overlay_ = std::make_unique<OverlayImplTest>(*net, 5005);
        net->handler_ = std::make_unique<TestHandler>(*net->overlay_);
        std::vector<Port> serverPort(1);
        serverPort.back().ip = beast::IP::Address::from_string(ip);
        serverPort.back().port = 5005;
        serverPort.back().protocol.insert("peer");
        name = "server" + i;
        net->server_ = make_Server(
            *net->handler_, net->io_service_, net->logs_->journal(name));
        net->server_->ports(serverPort);
        net->overlay_->run();
        nets_.push_back(std::move(net));
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        mkNet("0", {"172.0.0.1"});
        mkNet("1", {"172.0.0.0"});
        io_service_.run();
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