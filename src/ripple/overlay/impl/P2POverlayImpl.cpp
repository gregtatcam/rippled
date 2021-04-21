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

#include <ripple/app/misc/HashRouter.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/InboundConnection.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/predicates.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {

P2POverlayImpl::P2POverlayImpl(
    Application& app,
    Setup const& setup,
    Stoppable& parent,
    std::uint16_t overlayPort,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : P2POverlayInternal(parent)
    , app_(app)
    , io_service_(io_service)
    , work_(std::in_place, std::ref(io_service_))
    , strand_(io_service_)
    , setup_(setup)
    , journal_(app_.journal("Overlay"))
    , overlayPort_(overlayPort)
    , m_resourceManager(resourceManager)
    , m_peerFinder(PeerFinder::make_Manager(
          *this,
          io_service,
          stopwatch(),
          app_.journal("PeerFinder"),
          config,
          collector))
    , m_resolver(resolver)
    , next_id_(1)
    , m_stats(
          std::bind(&P2POverlayImpl::collect_metrics, this),
          collector,
          [counts = m_traffic.getCounts(), collector]() {
              std::vector<TrafficGauges> ret;
              ret.reserve(counts.size());

              for (size_t i = 0; i < counts.size(); ++i)
              {
                  ret.push_back(TrafficGauges(counts[i].name, collector));
              }

              return ret;
          }())
{
    beast::PropertyStream::Source::add(m_peerFinder.get());
}

P2POverlayImpl::~P2POverlayImpl()
{
    stop();

    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock<decltype(mutex_)> lock(mutex_);
    cond_.wait(lock, [this] { return list_.empty(); });
}

P2POverlayImpl::Child::Child(P2POverlayImpl& overlay) : overlay_(overlay)
{
}

P2POverlayImpl::Child::~Child()
{
    overlay_.remove(*this);
}

//------------------------------------------------------------------------------

Handoff
P2POverlayImpl::onHandoff(
    std::unique_ptr<stream_type>&& stream_ptr,
    http_request_type&& request,
    endpoint_type remote_endpoint)
{
    auto const id = next_id_++;
    beast::WrappedSink sink(app_.logs()["Peer"], P2Peer::makePrefix(id));
    beast::Journal journal(sink);

    Handoff handoff;
    if (processRequest(request, handoff))
        return handoff;
    if (!isPeerUpgrade(request))
        return handoff;

    handoff.moved = true;

    JLOG(journal.debug()) << "Peer connection upgrade from " << remote_endpoint;

    boost::system::error_code ec;
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
            app_);

        {
            // The node gets a reserved slot if it is in our cluster
            // or if it has a reservation.
            bool const reserved =
                static_cast<bool>(app_.cluster().member(publicKey)) ||
                app_.peerReservations().contains(publicKey);
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

        auto const inboundConnection = std::make_shared<InboundConnection>(
            app_,
            id,
            slot,
            std::move(request),
            publicKey,
            *negotiatedVersion,
            consumer,
            std::move(stream_ptr),
            *this);
        std::lock_guard lock(mutex_);
        list_.emplace(inboundConnection.get(), inboundConnection);
        inboundConnection->run();
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

void
P2POverlayImpl::reportTraffic(
    TrafficCount::category cat,
    bool isInbound,
    int number)
{
    m_traffic.addCount(cat, isInbound, number);
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
        app_,
        io_service_,
        beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
        usage,
        setup_.context,
        next_id_++,
        slot,
        app_.journal("Peer"),
        *this);

    std::lock_guard lock(mutex_);
    list_.emplace(p.get(), p);
    p->run();
}

//------------------------------------------------------------------------------

// Adds a peer that is already handshaked and active
void
P2POverlayImpl::add_active(std::shared_ptr<P2PeerImp> const& peer)
{
    std::lock_guard lock(mutex_);

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

//------------------------------------------------------------------------------
//
// Stoppable
//
//------------------------------------------------------------------------------

// Caller must hold the mutex
void
P2POverlayImpl::checkStopped()
{
    if (isStopping() && areChildrenStopped() && list_.empty())
        stopped();
}

void
P2POverlayImpl::onPrepare()
{
    PeerFinder::Config config = PeerFinder::Config::makeConfig(
        app_.config(),
        overlayPort_,
        !app_.getValidationPublicKey().empty(),
        setup_.ipLimit);

    m_peerFinder->setConfig(config);

    // Populate our boot cache: if there are no entries in [ips] then we use
    // the entries in [ips_fixed].
    auto bootstrapIps =
        app_.config().IPS.empty() ? app_.config().IPS_FIXED : app_.config().IPS;

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
    if (!app_.config().standalone() && !app_.config().IPS_FIXED.empty())
    {
        m_resolver.resolve(
            app_.config().IPS_FIXED,
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
P2POverlayImpl::onStart()
{
}

void
P2POverlayImpl::onStop()
{
    strand_.dispatch(std::bind(&P2POverlayImpl::stop, this));
}

void
P2POverlayImpl::onChildrenStopped()
{
    std::lock_guard lock(mutex_);
    checkStopped();
}

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

void
P2POverlayImpl::onWrite(beast::PropertyStream::Map& stream)
{
    beast::PropertyStream::Set set("traffic", stream);
    auto const stats = m_traffic.getCounts();
    for (auto const& i : stats)
    {
        if (i)
        {
            beast::PropertyStream::Map item(set);
            item["category"] = i.name;
            item["bytes_in"] = std::to_string(i.bytesIn.load());
            item["messages_in"] = std::to_string(i.messagesIn.load());
            item["bytes_out"] = std::to_string(i.bytesOut.load());
            item["messages_out"] = std::to_string(i.messagesOut.load());
        }
    }
}

//------------------------------------------------------------------------------

int
P2POverlayImpl::limit()
{
    return m_peerFinder->config().maxPeers;
}

//------------------------------------------------------------------------------

void
P2POverlayImpl::remove(P2POverlayImpl::Child& child)
{
    std::lock_guard lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        checkStopped();
}

void
P2POverlayImpl::onPeerDistruct(
    P2Peer::id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot)
{
    onPeerDeactivate(id, slot);
    peerFinder().on_closed(slot);
}

void
P2POverlayImpl::stop()
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
    auto const peer = mkOutboundPeer(
        std::move(stream_ptr),
        buffers,
        std::move(slot),
        std::move(response),
        usage,
        publicKey,
        protocol,
        id);

    add_active(peer);
}

void
P2POverlayImpl::addInboundPeer(
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr)
{
    auto const peer = mkInboundPeer(
        id,
        slot,
        std::move(request),
        publicKey,
        consumer,
        protocol,
        std::move(stream_ptr));

    add_active(peer);
}

}  // namespace ripple