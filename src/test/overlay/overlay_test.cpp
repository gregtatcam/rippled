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
#include <ripple/basics/ResolverAsio.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/make_Overlay.h>
#include <test/jtx/Env.h>

namespace ripple {

namespace test {

class PeerImpTest : public P2PeerImp
{
public:
    PeerImpTest(
        Application& app,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              app,
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
        Application& app,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              app,
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
public:
    OverlayImplTest(
        Application& app,
        Setup const& setup,
        std::uint16_t overlayPort,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector)
        : P2POverlayImpl(
              app,
              setup,
              overlayPort,
              resourceManager,
              resolver,
              io_service,
              config,
              collector)
    {
    }

    void
    run()
    {
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
            this->app(),
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
            this->app(),
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
    std::unique_ptr<OverlayImplTest> overlay1_;
    std::unique_ptr<OverlayImplTest> overlay2_;

public:
    overlay_net_test()
    {
    }

    std::unique_ptr<jtx::Env>
    mkEnv(std::string const& ip, std::vector<std::string> const& fixed)
    {
        auto c = std::make_unique<Config>();
        std::stringstream str;
        str << "[server]\n"
            << "port_peer\n"
            << "port_rpc_admin_local\n"
            << "[port_peer]\n"
            << "port = 5005\n"
            << "ip = " << ip << "\n"
            << "protocol = peer\n"
            << "[port_rpc_admin_local]\n"
            << "port = 6006\n"
            << "ip = " << ip << "\n"
            << "admin=[0.0.0.0]\n"
            << "protocol = http\n"
            << "[ips_fixed]\n";
        for (auto i : fixed)
            str << i << " 51235\n";
        c->loadFromString(str.str());
        c->overwrite(ConfigSection::nodeDatabase(), "type", "memory");
        c->overwrite(ConfigSection::nodeDatabase(), "path", "main");
        c->deprecatedClearSection(ConfigSection::importNodeDatabase());
        c->legacy("database_path", "");
        c->setupControl(true, true, true);
        return std::make_unique<jtx::Env>(*this, std::move(c));
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        std::cout << "running test\n";
        auto env1 = mkEnv("172.0.0.0", {"172.0.0.1"});
        auto env2 = mkEnv("172.0.0.1", {"172.0.0.0"});
        auto& app1 = env1->app();
        auto& app2 = env2->app();
        auto resolver1 = ResolverAsio::New(
            app1.getIOService(), app1.journal("OverlayTest1"));
        overlay1_ = std::make_unique<OverlayImplTest>(
            app1,
            setup_Overlay(app1.config()),
            5005,
            app1.getResourceManager(),
            *resolver1,
            app1.getIOService(),
            app1.config(),
            app1.getCollectorManager().collector());
        BEAST_EXPECT(overlay1_);
        overlay2_ = std::make_unique<OverlayImplTest>(
            app2,
            setup_Overlay(app2.config()),
            5005,
            app2.getResourceManager(),
            *resolver1,
            app1.getIOService(),
            app2.config(),
            app2.getCollectorManager().collector());
        BEAST_EXPECT(overlay2_);
        TestHandler handler1(*overlay1_);
        TestHandler handler2(*overlay2_);
        auto server1 =
            make_Server(handler1, app1.getIOService(), app1.journal("server1"));
        auto server2 =
            make_Server(handler2, app1.getIOService(), app1.journal("server2"));
        std::vector<Port> serverPort1(1);
        serverPort1.back().ip = beast::IP::Address::from_string("172.0.0.0");
        serverPort1.back().port = 5005;
        serverPort1.back().protocol.insert("peer");
        std::vector<Port> serverPort2(1);
        serverPort2.back().ip = beast::IP::Address::from_string("172.0.0.1");
        serverPort2.back().port = 5005;
        serverPort2.back().protocol.insert("peer");
        server1->ports(serverPort1);
        server2->ports(serverPort2);
        overlay1_->run();
        overlay2_->run();
        app1.getIOService().run();
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