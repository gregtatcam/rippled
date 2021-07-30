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

#include <ripple/basics/base64.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/predicates.h>
#include <ripple/peerfinder/make_Manager.h>
#include <ripple/rpc/handlers/GetCounts.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/SimpleWriter.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {

//------------------------------------------------------------------------------

P2POverlayImpl::Child::Child(P2POverlayImpl& overlay) : overlay_(overlay)
{
}

P2POverlayImpl::Child::~Child()
{
    overlay_.remove(*this);
}

//------------------------------------------------------------------------------

P2POverlayImpl::P2POverlayImpl(
    P2PConfigImpl const& p2pConfig,
    Setup const& setup,
    std::uint16_t overlayPort,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : p2pConfig_(p2pConfig)
    , work_(std::in_place, std::ref(io_service))
    , overlayPort_(overlayPort)
    , m_resourceManager(resourceManager)
    , m_resolver(resolver)
    , next_id_(1)
    , io_service_(io_service)
    , strand_(io_service_)
    , setup_(setup)
    , journal_(p2pConfig_.logs().journal("Overlay"))
    , m_peerFinder(PeerFinder::make_Manager(
          io_service,
          stopwatch(),
          p2pConfig_.logs().journal("PeerFinder"),
          config,
          collector))
{
}

Handoff
P2POverlayImpl::onHandoff(
    std::unique_ptr<stream_type>&& stream_ptr,
    http_request_type&& request,
    endpoint_type remote_endpoint)
{
    auto const id = next_id_++;
    beast::WrappedSink sink(p2pConfig_.logs()["Peer"], makePrefix(id));
    beast::Journal journal(sink);

    Handoff handoff;
    if (processRequest(request, handoff))
        return handoff;
    if (!isPeerUpgrade(request))
        return handoff;

    handoff.moved = true;

    JLOG(journal.debug()) << "Peer connection upgrade from " << remote_endpoint;

    error_code ec;
    auto const local_endpoint(
        stream_ptr->next_layer().socket().local_endpoint(ec));
    if (ec)
    {
        JLOG(journal.debug()) << remote_endpoint << " failed: " << ec.message();
        return handoff;
    }

    auto consumer = m_resourceManager.newInboundEndpoint(
        beast::IPAddressConversion::from_asio(remote_endpoint));
    if (consumer.disconnect())
        return handoff;

    auto const slot = m_peerFinder->new_inbound_slot(
        beast::IPAddressConversion::from_asio(local_endpoint),
        beast::IPAddressConversion::from_asio(remote_endpoint));

    if (slot == nullptr)
    {
        // self-connect, close
        handoff.moved = false;
        return handoff;
    }

    // Validate HTTP request

    {
        auto const types = beast::rfc2616::split_commas(request["Connect-As"]);
        if (std::find_if(types.begin(), types.end(), [](std::string const& s) {
                return boost::iequals(s, "peer");
            }) == types.end())
        {
            handoff.moved = false;
            handoff.response =
                makeRedirectResponse(slot, request, remote_endpoint.address());
            handoff.keep_alive = beast::rfc2616::is_keep_alive(request);
            return handoff;
        }
    }

    auto const negotiatedVersion = negotiateProtocolVersion(request["Upgrade"]);
    if (!negotiatedVersion)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Unable to agree on a protocol version");
        handoff.keep_alive = false;
        return handoff;
    }

    auto const sharedValue = makeSharedValue(*stream_ptr, journal);
    if (!sharedValue)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Incorrect security cookie");
        handoff.keep_alive = false;
        return handoff;
    }

    try
    {
        auto publicKey = verifyHandshake(
            request,
            *sharedValue,
            setup_.networkID,
            setup_.public_ip,
            remote_endpoint.address(),
            p2pConfig_);

        {
            // The node gets a reserved slot if it is in our cluster
            // or if it has a reservation.
            bool const reserved =
                static_cast<bool>(p2pConfig_.clusterMember(publicKey)) ||
                p2pConfig_.reservedPeer(publicKey);
            auto const result =
                m_peerFinder->activate(slot, publicKey, reserved);
            if (result != PeerFinder::Result::success)
            {
                m_peerFinder->on_closed(slot);
                JLOG(journal.debug())
                    << "Peer " << remote_endpoint << " redirected, slots full";
                handoff.moved = false;
                handoff.response = makeRedirectResponse(
                    slot, request, remote_endpoint.address());
                handoff.keep_alive = false;
                return handoff;
            }
        }

        addInboundPeer(
            id,
            slot,
            std::move(request),
            publicKey,
            *negotiatedVersion,
            consumer,
            std::move(stream_ptr));

        handoff.moved = true;
        return handoff;
    }
    catch (std::exception const& e)
    {
        JLOG(journal.debug()) << "Peer " << remote_endpoint
                              << " fails handshake (" << e.what() << ")";

        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot, request, remote_endpoint.address(), e.what());
        handoff.keep_alive = false;
        return handoff;
    }
}

//------------------------------------------------------------------------------

bool
P2POverlayImpl::isPeerUpgrade(http_request_type const& request)
{
    if (!is_upgrade(request))
        return false;
    auto const versions = parseProtocolVersions(request["Upgrade"]);
    return !versions.empty();
}

std::string
P2POverlayImpl::makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

std::shared_ptr<Writer>
P2POverlayImpl::makeRedirectResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address)
{
    boost::beast::http::response<json_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::service_unavailable);
    msg.insert("Server", BuildInfo::getFullVersionString());
    {
        std::ostringstream ostr;
        ostr << remote_address;
        msg.insert("Remote-Address", ostr.str());
    }
    msg.insert("Content-Type", "application/json");
    msg.insert(boost::beast::http::field::connection, "close");
    msg.body() = Json::objectValue;
    {
        Json::Value& ips = (msg.body()["peer-ips"] = Json::arrayValue);
        for (auto const& _ : m_peerFinder->redirect(slot))
            ips.append(_.address.to_string());
    }
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

std::shared_ptr<Writer>
P2POverlayImpl::makeErrorResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address,
    std::string text)
{
    boost::beast::http::response<boost::beast::http::empty_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::bad_request);
    msg.reason("Bad Request (" + text + ")");
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Remote-Address", remote_address.to_string());
    msg.insert(boost::beast::http::field::connection, "close");
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

//------------------------------------------------------------------------------

void
P2POverlayImpl::connect(beast::IP::Endpoint const& remote_endpoint)
{
    assert(work_);

    auto usage = resourceManager().newOutboundEndpoint(remote_endpoint);
    if (usage.disconnect())
    {
        JLOG(journal_.info()) << "Over resource limit: " << remote_endpoint;
        return;
    }

    auto const slot = peerFinder().new_outbound_slot(remote_endpoint);
    if (slot == nullptr)
    {
        JLOG(journal_.debug()) << "Connect: No slot for " << remote_endpoint;
        return;
    }

    auto const p = std::make_shared<ConnectAttempt>(
        p2pConfig_,
        io_service_,
        beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
        usage,
        setup_.context,
        next_id_++,
        slot,
        p2pConfig_.logs().journal("Peer"),
        *this);

    std::lock_guard lock(mutex_);
    list_.emplace(p.get(), p);
    p->run();
}

//------------------------------------------------------------------------------

void
P2POverlayImpl::start()
{
    PeerFinder::Config config = PeerFinder::Config::makeConfig(
        p2pConfig_.config(),
        overlayPort_,
        !p2pConfig_.isValidator(),
        setup_.ipLimit);

    m_peerFinder->setConfig(config);
    m_peerFinder->start();

    // Populate our boot cache: if there are no entries in [ips] then we use
    // the entries in [ips_fixed].
    auto bootstrapIps = p2pConfig_.config().IPS.empty()
        ? p2pConfig_.config().IPS_FIXED
        : p2pConfig_.config().IPS;

    // If nothing is specified, default to several well-known high-capacity
    // servers to serve as bootstrap:
    if (bootstrapIps.empty())
    {
        // Pool of servers operated by Ripple Labs Inc. - https://ripple.com
        bootstrapIps.push_back("r.ripple.com 51235");

        // Pool of servers operated by Alloy Networks - https://www.alloy.ee
        bootstrapIps.push_back("zaphod.alloy.ee 51235");

        // Pool of servers operated by ISRDC - https://isrdc.in
        bootstrapIps.push_back("sahyadri.isrdc.in 51235");
    }

    m_resolver.resolve(
        bootstrapIps,
        [this](
            std::string const& name,
            std::vector<beast::IP::Endpoint> const& addresses) {
            std::vector<std::string> ips;
            ips.reserve(addresses.size());
            for (auto const& addr : addresses)
            {
                if (addr.port() == 0)
                    ips.push_back(to_string(addr.at_port(DEFAULT_PEER_PORT)));
                else
                    ips.push_back(to_string(addr));
            }

            std::string const base("config: ");
            if (!ips.empty())
                m_peerFinder->addFallbackStrings(base + name, ips);
        });

    // Add the ips_fixed from the rippled.cfg file
    if (!p2pConfig_.config().standalone() &&
        !p2pConfig_.config().IPS_FIXED.empty())
    {
        m_resolver.resolve(
            p2pConfig_.config().IPS_FIXED,
            [this](
                std::string const& name,
                std::vector<beast::IP::Endpoint> const& addresses) {
                std::vector<beast::IP::Endpoint> ips;
                ips.reserve(addresses.size());

                for (auto& addr : addresses)
                {
                    if (addr.port() == 0)
                        ips.emplace_back(addr.address(), DEFAULT_PEER_PORT);
                    else
                        ips.emplace_back(addr);
                }

                if (!ips.empty())
                    m_peerFinder->addFixedPeer(name, ips);
            });
    }
}

void
P2POverlayImpl::stop()
{
    strand_.dispatch(std::bind(&P2POverlayImpl::stopChildren, this));
    {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        cond_.wait(lock, [this] { return list_.empty(); });
    }
    m_peerFinder->stop();
}

//------------------------------------------------------------------------------

int
P2POverlayImpl::limit()
{
    return m_peerFinder->config().maxPeers;
}

bool
P2POverlayImpl::processRequest(const http_request_type& req, Handoff& handoff)
{
    return onEvtProcessRequest(req, handoff);
}

//------------------------------------------------------------------------------

void
P2POverlayImpl::remove(Child& child)
{
    std::lock_guard lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        cond_.notify_all();
}

void
P2POverlayImpl::stopChildren()
{
    // Calling list_[].second->stop() may cause list_ to be modified
    // (OverlayImpl::remove() may be called on this same thread).  So
    // iterating directly over list_ to call child->stop() could lead to
    // undefined behavior.
    //
    // Therefore we copy all of the weak/shared ptrs out of list_ before we
    // start calling stop() on them.  That guarantees OverlayImpl::remove()
    // won't be called until vector<> children leaves scope.
    std::vector<std::shared_ptr<Child>> children;
    {
        std::lock_guard lock(mutex_);
        if (!work_)
            return;
        work_ = std::nullopt;

        children.reserve(list_.size());
        for (auto const& element : list_)
        {
            children.emplace_back(element.second.lock());
        }
    }  // lock released

    for (auto const& child : children)
    {
        if (child != nullptr)
            child->stop();
    }
}

void
P2POverlayImpl::autoConnect()
{
    auto const result = m_peerFinder->autoconnect();
    for (auto addr : result)
        connect(addr);
}

void
P2POverlayImpl::addInboundPeer(
    Peer::id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr)
{
    std::lock_guard lock(mutex_);

    auto peer = mkInboundPeer(
        id,
        slot,
        std::move(request),
        publicKey,
        protocol,
        consumer,
        std::move(stream_ptr));
    list_.emplace(peer.get(), peer);

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // As we are not on the strand, run() must be called
    // while holding the lock, otherwise new I/O can be
    // queued after a call to stop().
    peer->run();
}

void
P2POverlayImpl::addOutboundPeer(
    std::unique_ptr<stream_type>&& stream_ptr,
    boost::beast::multi_buffer const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id)
{
    std::lock_guard lock(mutex_);

    auto peer = mkOutboundPeer(
        std::move(stream_ptr),
        buffers,
        std::move(slot),
        std::move(response),
        usage,
        publicKey,
        protocol,
        id);
    list_.emplace(peer.get(), peer);

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // As we are not on the strand, run() must be called
    // while holding the lock, otherwise new I/O can be
    // queued after a call to stop().
    peer->run();
}

}  // namespace ripple
