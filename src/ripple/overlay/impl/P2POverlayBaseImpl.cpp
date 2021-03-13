

//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/P2POverlayBaseImpl.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {
//------------------------------------------------------------------------------

P2POverlayBaseImpl::P2POverlayBaseImpl(
    Application& app,
    Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : P2POverlay(parent)
    , app_(app)
    , io_service_(io_service)
    , work_(boost::in_place(std::ref(io_service_)))
    , strand_(io_service_)
    , setup_(setup)
    , journal_(app_.journal("Overlay"))
    , serverHandler_(serverHandler)
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
          std::bind(&P2POverlayBaseImpl::collect_metrics, this),
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

P2POverlayBaseImpl::~P2POverlayBaseImpl()
{
}

//------------------------------------------------------------------------------

Handoff
P2POverlayBaseImpl::onHandoff(
    std::unique_ptr<stream_type>&& stream_ptr,
    http_request_type&& request,
    endpoint_type remote_endpoint)
{
    auto const id = next_id_++;
    beast::WrappedSink sink(app_.logs()["Peer"], makePrefix(id));
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
P2POverlayBaseImpl::isPeerUpgrade(http_request_type const& request)
{
    if (!is_upgrade(request))
        return false;
    auto const versions = parseProtocolVersions(request["Upgrade"]);
    return !versions.empty();
}

std::string
P2POverlayBaseImpl::makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

std::shared_ptr<Writer>
P2POverlayBaseImpl::makeRedirectResponse(
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
P2POverlayBaseImpl::makeErrorResponse(
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
P2POverlayBaseImpl::connect(beast::IP::Endpoint const& remote_endpoint)
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

    addChild(p);

    p->run();
}

//------------------------------------------------------------------------------
//
// Stoppable
//
//------------------------------------------------------------------------------

// Caller must hold the mutex

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

void
P2POverlayBaseImpl::onWrite(beast::PropertyStream::Map& stream)
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

void
P2POverlayBaseImpl::reportTraffic(
    TrafficCount::category cat,
    bool isInbound,
    int number)
{
    m_traffic.addCount(cat, isInbound, number);
}

int
P2POverlayBaseImpl::limit()
{
    return m_peerFinder->config().maxPeers;
}

void
P2POverlayBaseImpl::autoConnect()
{
    auto const result = m_peerFinder->autoconnect();
    for (auto addr : result)
        connect(addr);
}

}  // namespace ripple