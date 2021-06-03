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
#include <boost/regex.hpp>
#include <boost/thread.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <signal.h>
#include <unistd.h>

namespace ripple {

namespace test {

class OverlayImplTest;
class VirtualNetwork;
std::mutex logMutex;
std::map<std::string, std::map<std::string, std::uint16_t>> netConfig;

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
    static void
    decCnt(std::uint16_t& cnt)
    {
        std::lock_guard l(cntMutex);
        cnt--;
        cond.notify_all();
    }
    static bool
    deactivated()
    {
        return deactivateCnt == inPeersCnt + outPeersCnt;
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
        bool isFixed,
        std::vector<std::string> const& bootstrap,
        std::uint16_t peerPort,
        std::uint16_t out_max,
        std::uint16_t in_max)
        : net_(net)
        , ip_(ip)
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
        , out_max_(out_max)
        , in_max_(in_max)
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
        std::vector<std::string> const& bootstrap,
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
        for (auto f : bootstrap)
            if (isFixed)
                config->IPS_FIXED.push_back(f + " " + peerPort);
            else
                config->IPS.push_back(f + " " + peerPort);
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
    std::chrono::time_point<std::chrono::steady_clock> start_;

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

    std::size_t
    now()
    {
        using namespace std::chrono;
        return duration_cast<seconds>(steady_clock::now() - start_).count();
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
        std::vector<std::string> bootstrap,
        std::uint16_t out_max,
        std::uint16_t in_max,
        std::uint16_t peerPort = 51235) = 0;
    void
    startNodes(std::vector<std::string> const& nodes)
    {
        for (auto n : nodes)
            mkNode(n, true, nodes, 20, 20);
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

private:
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

    void
    onPeerDeactivate(std::shared_ptr<PeerFinder::Slot> const& slot)
    {
        Counts::incCnt(Counts::deactivateCnt);
        std::lock_guard l(peersMutex_);
        if (auto it = peers_.find(slot); it != peers_.end())
        {
            peers_.erase(it);
            auto p = it->second.lock();
            if (p)
            {
                if (p->inbound())
                    Counts::decCnt(Counts::inPeersCnt);
                else
                    Counts::decCnt(Counts::outPeersCnt);
            }
        }
    }

    std::string
    name()
    {
        return name_;
    }

    std::pair<std::uint16_t, std::uint16_t>
    getCounts()
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
    outputNetwork(std::ofstream& of)
    {
        std::lock_guard l(peersMutex_);
        for (auto [slot, peer] : peers_)
        {
            (void)slot;
            auto p = peer.lock();
            if (p)
                of << node_.ip_ << ","
                   << p->getRemoteAddress().address().to_string() << ","
                   << (p->inbound() ? "in" : "out") << std::endl;
        }
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
            std::lock_guard lock(mutex_);
            if (auto const iter = peers_.find(e.first); iter != peers_.end())
            {
                if (auto peer = iter->second.lock())
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
                    Counts::incCnt(Counts::msgSendCnt);
                    peer->send(
                        std::make_shared<Message>(tm, protocol::mtENDPOINTS));
                }
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
protected:
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
        bool isFixed,
        std::vector<std::string> bootstrap,
        std::uint16_t out_max,
        std::uint16_t in_max,
        std::uint16_t peerPort = 51235) override
    {
        static std::uint16_t tot_out = 0;
        static std::uint16_t tot_in = 0;
        tot_out += out_max;
        tot_in += in_max;
        std::cout << ip << " " << out_max << " " << in_max << " " << tot_out
                  << " " << tot_in << std::endl
                  << std::flush;
        bootstrap.erase(std::find(bootstrap.begin(), bootstrap.end(), ip));
        auto node = std::make_shared<VirtualNode>(
            *this,
            *this,
            *this,
            io_service_,
            ip,
            isFixed,
            bootstrap,
            peerPort,
            out_max,
            in_max);
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
    testOverlay()
    {
        testcase("Overlay");
        // interfaces must be pre-configured
        std::vector<std::string> nodes = {
            "172.0.0.0", "172.0.0.1", "172.0.0.2", "172.0.0.3", "172.0.0.4"};
        overlayTimer_.expires_from_now(std::chrono::seconds(15));
        overlayTimer_.async_wait(std::bind(
            &overlay_net_test::onOverlayTimer, this, std::placeholders::_1));
        startNodes(nodes);
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

class xrpl_overlay_test : public overlay_net_test
{
    std::vector<std::uint16_t> tot_peers_out_;
    std::vector<std::uint16_t> tot_peers_in_;

public:
    /**
     * @param adjMatrix - adjacency matrix. Format:
     * ip1,ip2,[in|out]
     * [in|out] - in: inbound connection, ip2 is an inbound peer;
     *            out: outbound connection, ip2 is an outbound peer
     * @return vector of bootstrap ip
     */
    std::vector<std::string>
    getNetConfig(std::string const& adjMatrixPath)
    {
        std::
            map<std::string, std::map<std::string, std::map<std::string, bool>>>
                all;
        std::map<std::string, std::string> ip2Local;
        std::string base = "172.0";
        std::uint16_t cnt = 0;
        std::fstream file(adjMatrixPath, std::ios::in);
        assert(file.is_open());
        std::string line;
        // convert to local "172.x.x.x" ip
        auto convert = [&](auto ip) {
            if (ip2Local.find(ip) == ip2Local.end())
            {
                std::stringstream str;
                str << base << "." << (cnt / 256) << "." << (cnt % 256);
                ip2Local[ip] = str.str();
                cnt++;
            }
            return ip2Local[ip];
        };
        // for each ip figure out out_max and in_max
        // for an entry ip,ip1,'in|out' can
        // increment for ip max_in|max_out
        // for each ip,ip1,'in' and ip,ip1,'out'
        // if corresponding ip1,ip,'out' and/or ip1,ip,'in'
        // are not present then should assume they exist
        while (getline(file, line))
        {
            boost::smatch match;
            boost::regex rx("^([^,]+),([^,]+),(in|out)");
            assert(boost::regex_search(line, match, rx));
            auto const ip = convert(match[1]);
            auto const ip1 = convert(match[2]);
            auto const ctype = match[3];
            if (all.find(ip) == all.end() || all[ip].find(ip1) == all[ip].end())
                netConfig[ip][ctype]++;
            all[ip][ip1][ctype] = true;
            if (all.find(ip1) == all.end() ||
                all[ip1].find(ip) == all[ip1].end())
            {
                std::string const t = (ctype == "in") ? "out" : "in";
                all[ip1][ip][t] = true;
                netConfig[ip1][t] += 1;
            }
        }
        std::vector<std::string> bootstrap;
        auto resolve = [&](auto host) {
            boost::asio::ip::tcp::resolver resolver(io_service_);
            boost::asio::ip::tcp::resolver::query query(host, "80");
            boost::asio::ip::tcp::resolver::iterator iter =
                resolver.resolve(query);
            std::for_each(iter, {}, [&](auto& it) {
                auto ip = it.endpoint().address().to_string();
                if (auto it1 = ip2Local.find(ip); it1 != ip2Local.end())
                    bootstrap.push_back(it1->second);
            });
        };
        resolve("r.ripple.com");
        resolve("zaphod.alloy.ee");
        resolve("sahyadri.isrdc.in");
        return bootstrap;
    }

    void
    testXRPLOverlay()
    {
        testcase("XRPLOverlay");
        // dummy interfaces must be pre-configured
        if (arg() == "")
        {
            fail("adjacency matrix must be provided");
            return;
        }

        auto bootstrap = getNetConfig(arg());
        startNodes(bootstrap);
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
    startNodes(std::vector<std::string> const& bootstrap)
    {
        for (auto [node, types] : netConfig)
            mkNode(node, false, bootstrap, types["out"], types["in"]);
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
            node->overlay_->outputNetwork(of);
    }

    void
    onOverlayTimer(boost::system::error_code const& ec)
    {
        if (ec)
            return;
        std::ifstream inf("stop");
        if (now() > 3600 || inf.good())
        {
            outputNetwork();
            stop();
        }
        else
        {
            doLog();
            setTimer();
        }
    }

    void
    setTimer()
    {
        overlayTimer_.expires_from_now(std::chrono::seconds(40));
        overlayTimer_.async_wait(std::bind(
            &xrpl_overlay_test::onOverlayTimer, this, std::placeholders::_1));
    }

    void
    doLog()
    {
        using namespace std::chrono;
        std::string sep = "";
        std::vector<double> pct_out;
        std::vector<double> pct_in;
        std::vector<std::uint16_t> peers_out;
        std::vector<std::uint16_t> peers_in;
        std::uint16_t Nin = 0;
        std::uint16_t Nout = 0;
        double avg_pct_out = 0.;
        double avg_pct_in = 0.;
        double avg_peers_out = 0.;
        double avg_peers_in = 0.;
        std::uint16_t out_max = 0;
        std::uint16_t in_max = 0;
        std::uint16_t tot_out = 0;
        std::uint16_t tot_in = 0;
        for (auto [id, node] : nodes_)
        {
            (void)id;
            auto [nout, nin] = node->overlay_->getCounts();
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
        }
        auto stats = [](auto& avg, auto N, auto sample) {
            avg = avg / N;
            double sd = 0.;
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
        {
            std::lock_guard l(logMutex);
            std::cout.precision(2);
            std::cout << now() << ", out: " << tot_out << ", in: " << tot_in
                      << ", snd: " << Counts::msgSendCnt
                      << ", rcv: " << Counts::msgRecvCnt
                      << ", deact: " << Counts::deactivateCnt
                      << ", max out/in: " << out_max << "/" << in_max
                      << ", avg out/in: " << avg_peers_out << "/"
                      << sd_peers_out << ", " << avg_peers_in << "/"
                      << sd_peers_in << ", "
                      << "avg pct out/in: " << avg_pct_out << "/" << sd_pct_out
                      << ", " << avg_pct_in << "/" << sd_pct_in << ", "
                      << tot_peers_in_.size() << " " << tot_peers_out_.size()
                      << std::endl
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
        // stop if the network doesn't change
        if (tot_peers_in_.size() >= 4 and tot_peers_out_.size() >= 4)
        {
            overlayTimer_.cancel();
            outputNetwork();
            stop();
        }
    }

    void
    run() override
    {
        testXRPLOverlay();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(overlay_net, ripple_data, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(xrpl_overlay, ripple_data, ripple);

}  // namespace test

}  // namespace ripple