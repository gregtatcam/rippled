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
class Nets;
std::mutex logMutex;

static std::string
name(std::string const& n, int i)
{
    return n + std::to_string(i);
}

// All objects needed to run the overlay
struct PseudoNet
{
    PseudoNet(
        Nets& nets,
        beast::unit_test::suite& suite,
        boost::asio::io_service& service,
        std::string const& ip,
        std::vector<std::string> const& ipsFixed,
        int peerPort)
        : id_(sid_)
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
              nets,
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
class Nets
{
    friend class overlay_net_test;

protected:
    boost::asio::io_service io_service_;
    boost::thread_group tg_;
    std::mutex netMutex_;
    std::unordered_map<int, std::shared_ptr<PseudoNet>> pseudoNets_;
    std::uint32_t nNets_ = 0;

public:
    virtual ~Nets() = default;

    void
    erase(int id)
    {
        std::lock_guard l(netMutex_);
        //pseudoNets_.erase(id);
        nNets_--;
        {
            std::lock_guard l1(logMutex);
            std::cout << "---------------> number of overlays "
                      << nNets_ << std::endl
                      << std::flush;
        }

        if (nNets_ == 0)
        {
            io_service_.stop();
        }
    }

protected:
    void
    add(std::shared_ptr<PseudoNet> const& net)
    {
        std::lock_guard l(netMutex_);
        pseudoNets_.emplace(net->id_, net);
        nNets_++;
    }
    virtual void
    mkNet(
        std::string const& ip,
        std::vector<std::string> fixed,
        int peerPort = 51235) = 0;
    void
    startNets(std::vector<std::string> const& nets)
    {
        for (auto n : nets)
            mkNet(n, nets);
        for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
            tg_.create_thread(
                boost::bind(&boost::asio::io_service::run, &io_service_));
        tg_.join_all();
        std::lock_guard l(logMutex);
        std::cout << "-----------------> finished\n" << std::flush;
    }
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
        {
            std::lock_guard l(logMutex);
            std::cout << "protocol started event, peer id " << this->id()
                      << " for remote " << this->getRemoteAddress() << std::endl
                      << std::flush;
        }
        protocol::TMSendTest tms;
        tms.set_version(101);
        this->send(std::make_shared<Message>(tms, protocol::mtSEND_TEST));
    }

    void
    onEvtRun() override
    {
        std::lock_guard l(logMutex);
        std::cout << "run event\n" << std::flush;
    }

    void
    onEvtClose() override
    {
        std::lock_guard l(logMutex);
        std::cout << "close event\n" << std::flush;
    }

    void
    onEvtGracefulClose() override
    {
        std::lock_guard l(logMutex);
        std::cout << "graceful close event\n" << std::flush;
    }

    void
    onEvtShutdown() override
    {
        std::lock_guard l(logMutex);
        std::cout << "shutdown event\n" << std::flush;
    }

    std::pair<size_t, boost::system::error_code>
    onEvtProtocolMessage(
        boost::beast::multi_buffer const& mbuffers,
        size_t& hint) override
    {
        {
            std::lock_guard l(logMutex);
            std::cout << "protocol message event\n" << std::flush;
        }
        std::pair<std::size_t, boost::system::error_code> result = {0, {}};

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
                    std::cout << "got message from " << this->getRemoteAddress()
                              << ": " << m->version() << std::endl
                              << std::flush;
                }
            }
            break;
            default: {
                std::lock_guard l(logMutex);
                std::cout << "invalid message " << header.first->message_type
                          << std::endl
                          << std::flush;
            }
        }

        result.first = header.first->total_wire_size;
        // intentional to close the connection and terminate
        result.second = make_error_code(boost::system::errc::bad_message);

        return result;
    }
};

class OverlayImplTest : public P2POverlayImpl,
                        public std::enable_shared_from_this<OverlayImplTest>
{
private:
    Nets& nets_;
    PseudoNet& net_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    std::string const name_;
    std::mutex peersMutex_;
    std::unordered_map<P2Peer::id_t, std::weak_ptr<PeerImpTest>> peers_;
    std::uint16_t nPeers_ = 0;

public:
    ~OverlayImplTest()
    {
        timer_.cancel();
    }

    OverlayImplTest(
        Nets& nets,
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
        , nets_(nets)
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
            net_,
            id,
            slot,
            std::move(request),
            publicKey,
            protocol,
            std::move(stream_ptr),
            *this);
        std::lock_guard l(peersMutex_);
        peers_.emplace(peer->id(), peer);
        nPeers_++;
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
        std::lock_guard l(peersMutex_);
        peers_.emplace(peer->id(), peer);
        nPeers_++;
        return peer;
    }

    void
    onPeerDeactivate(
        P2Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot) override
    {
        std::lock_guard l1(peersMutex_);
        auto peer = peers_[id].lock();
        if (peer)
        {
            std::lock_guard l(logMutex);
            std::cout << "deactivating peer " << peer->getRemoteAddress()
                      << std::flush;
        }
        //peers_.erase(id);
        nPeers_--;
        if (nPeers_ == 0) // TODO
            net_.io_service_.post([&]() { nets_.erase(net_.id_); });
    }
};

void
PseudoNet::run()
{
    server_->ports(serverPort_);
    overlay_->run();
}

class overlay_net_test : public beast::unit_test::suite, public Nets
{
public:
    overlay_net_test()
    {
    }

protected:
    void
    mkNet(
        std::string const& ip,
        std::vector<std::string> fixed,
        int peerPort = 51235) override
    {
        {
            std::lock_guard l(logMutex);
            std::cout << "configuring " << ip << std::endl << std::flush;
            std::cout << "-------------------\n" << std::endl << std::flush;
        }
        fixed.erase(std::find(fixed.begin(), fixed.end(), ip));
        auto net = std::make_shared<PseudoNet>(
            *this, *this, io_service_, ip, fixed, peerPort);
        add(net);
        net->run();
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        // interfaces must be pre-configured
        std::vector<std::string> nets = {
            "172.0.0.0", "172.0.0.1", "172.0.0.2", "172.0.0.3", "172.0.0.4"};
        startNets(nets);
    }

public:
    void
    run() override
    {
        testOverlay();
        exit(0); // TODO, doesn't exit after returning from testOverlay()
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(overlay_net, ripple_data, ripple);

}  // namespace test

}  // namespace ripple