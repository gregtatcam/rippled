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

#ifndef RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/app/rdb/RelationalDBInterface_global.h>
#include <ripple/basics/chrono.h>
#include <ripple/core/Job.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/rpc/handlers/GetCounts.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/SimpleWriter.h>

#include <boost/asio/basic_waitable_timer.hpp>

namespace ripple {

template <typename>
class PeerImp;
class BasicConfig;

template <typename P2POverlayImplmnt>
class OverlayImpl : public Overlay,
                    public P2POverlayImplmnt,
                    public reduce_relay::SquelchHandler
{
    static_assert(
        std::is_base_of<P2POverlay, P2POverlayImplmnt>::value,
        "P2POverlayImplmnt must inherit from P2POverlay");

private:
    using P2PeerImp_t = typename P2POverlayImplmnt::P2PeerImp_t;
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;

    struct Timer : std::enable_shared_from_this<Timer>
    {
        OverlayImpl<P2POverlayImplmnt>& overlay_;
        boost::asio::basic_waitable_timer<clock_type> timer_;

        explicit Timer(OverlayImpl<P2POverlayImplmnt>& overlay);

        void
        stop();

        void
        run();

        void
        on_timer(error_code ec);
    };

    std::shared_ptr<Timer> timer_;
    int timer_count_;
    std::atomic<uint64_t> jqTransOverflow_{0};
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    // Last time we crawled peers for shard info. 'cs' = crawl shards
    std::atomic<std::chrono::seconds> csLast_{std::chrono::seconds{0}};
    std::mutex csMutex_;
    std::condition_variable csCV_;
    // Peer IDs expecting to receive a last link notification
    std::set<std::uint32_t> csIDs_;

    reduce_relay::Slots<UptimeClock> slots_;

    // A message with the list of manifests we send to peers
    std::shared_ptr<Message> manifestMessage_;
    // Used to track whether we need to update the cached list of manifests
    std::optional<std::uint32_t> manifestListSeq_;
    // Protects the message and the sequence list of manifests
    std::mutex manifestLock_;
    hash_map<
        std::shared_ptr<PeerFinder::Slot>,
        std::weak_ptr<PeerImp<P2PeerImp_t>>>
        m_peers;
    hash_map<Peer::id_t, std::weak_ptr<PeerImp<P2PeerImp_t>>> ids_;

    //--------------------------------------------------------------------------

public:
    OverlayImpl(
        Application& app,
        P2POverlay::Setup const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    ~OverlayImpl();

    OverlayImpl(OverlayImpl<P2POverlayImplmnt> const&) = delete;
    OverlayImpl&
    operator=(OverlayImpl<P2POverlayImplmnt> const&) = delete;

    std::size_t
    size() const override;

    Json::Value
    json() override;

    Overlay::PeerSequence
    getActivePeers() const override;

    void checkTracking(std::uint32_t) override;

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const override;

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) override;

    void
    broadcast(protocol::TMProposeSet& m) override;

    void
    broadcast(protocol::TMValidation& m) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    P2POverlay&
    p2p() override
    {
        return *this;
    }

    std::shared_ptr<Message>
    getManifestsMessage();

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    // UnaryFunc will be called as
    //  void(std::shared_ptr<PeerImp>&&)
    //
    template <class UnaryFunc>
    void
    for_each(UnaryFunc&& f) const
    {
        std::vector<std::weak_ptr<PeerImp<P2PeerImp_t>>> wp;
        {
            std::lock_guard lock(this->mutex_);

            // Iterate over a copy of the peer list because peer
            // destruction can invalidate iterators.
            wp.reserve(this->ids_.size());

            for (auto& x : this->ids_)
                wp.push_back(x.second);
        }

        for (auto& w : wp)
        {
            if (auto p = w.lock())
                f(std::move(p));
        }
    }

    // Called when TMManifests is received from a peer
    void
    onManifests(
        std::shared_ptr<protocol::TMManifests> const& m,
        std::shared_ptr<PeerImp<P2PeerImp_t>> const& from);

    void
    incJqTransOverflow() override
    {
        ++jqTransOverflow_;
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return jqTransOverflow_;
    }

    void
    incPeerDisconnectCharges() override
    {
        ++peerDisconnectsCharges_;
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return peerDisconnectsCharges_;
    }

    Json::Value
    crawlShards(bool pubKey, std::uint32_t hops) override;

    /** Called when the last link from a peer chain is received.

        @param id peer id that received the shard info.
    */
    void
    lastLink(std::uint32_t id);

    /** Updates message count for validator/peer. Sends TMSquelch if the number
     * of messages for N peers reaches threshold T. A message is counted
     * if a peer receives the message for the first time and if
     * the message has been  relayed.
     * @param key Unique message's key
     * @param validator Validator's public key
     * @param peers Peers' id to update the slots for
     * @param type Received protocol message type
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        std::set<Peer::id_t>&& peers,
        protocol::MessageType type);

    /** Overload to reduce allocation in case of single peer
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t peer,
        protocol::MessageType type);

    /** Called when the peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     */
    void
    deletePeer(Peer::id_t id);

protected:
    // HOOKS
    bool
    processRequest(http_request_type const& req, Handoff& handoff) override;

    std::shared_ptr<typename P2POverlayImplmnt::P2PeerImp_t>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        Resource::Consumer consumer,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr) override;

    std::shared_ptr<typename P2POverlayImplmnt::P2PeerImp_t>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) override;

    void
    onPeerDeactivate(
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot) override;

private:
    void
    squelch(
        PublicKey const& validator,
        Peer::id_t const id,
        std::uint32_t squelchDuration) const override;

    void
    unsquelch(PublicKey const& validator, Peer::id_t id) const override;

    /** Handles crawl requests. Crawl returns information about the
        node and its peers so crawlers can map the network.

        @return true if the request was handled.
    */
    bool
    processCrawl(http_request_type const& req, Handoff& handoff);

    /** Handles validator list requests.
        Using a /vl/<hex-encoded public key> URL, will retrieve the
        latest valdiator list (or UNL) that this node has for that
        public key, if the node trusts that public key.

        @return true if the request was handled.
    */
    bool
    processValidatorList(http_request_type const& req, Handoff& handoff);

    /** Handles health requests. Health returns information about the
        health of the node.

        @return true if the request was handled.
    */
    bool
    processHealth(http_request_type const& req, Handoff& handoff);

    /** Returns information about peers on the overlay network.
        Reported through the /crawl API
        Controlled through the config section [crawl] overlay=[0|1]
    */
    Json::Value
    getOverlayInfo();

    /** Returns information about the local server.
        Reported through the /crawl API
        Controlled through the config section [crawl] server=[0|1]
    */
    Json::Value
    getServerInfo();

    /** Returns information about the local server's performance counters.
        Reported through the /crawl API
        Controlled through the config section [crawl] counts=[0|1]
    */
    Json::Value
    getServerCounts();

    /** Returns information about the local server's UNL.
        Reported through the /crawl API
        Controlled through the config section [crawl] unl=[0|1]
    */
    Json::Value
    getUnlInfo();

    //--------------------------------------------------------------------------

    void
    sendEndpoints();

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator */
    void
    deleteIdlePeers();
};

}  // namespace ripple

#include <ripple/overlay/impl/PeerImp.h>

namespace ripple {

namespace CrawlOptions {
enum {
    Disabled = 0,
    Overlay = (1 << 0),
    ServerInfo = (1 << 1),
    ServerCounts = (1 << 2),
    Unl = (1 << 3)
};
}

//------------------------------------------------------------------------------

template <typename P2POverlayImplmnt>
OverlayImpl<P2POverlayImplmnt>::Timer::Timer(OverlayImpl& overlay)
    : overlay_(overlay), timer_(overlay_.io_service_)
{
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::Timer::stop()
{
    error_code ec;
    timer_.cancel(ec);
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::Timer::run()
{
    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, this->shared_from_this(), std::placeholders::_1)));
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::Timer::on_timer(error_code ec)
{
    if (ec || overlay_.isStopping())
    {
        if (ec && ec != boost::asio::error::operation_aborted)
        {
            JLOG(overlay_.journal_.error()) << "on_timer: " << ec.message();
        }
        return;
    }

    overlay_.m_peerFinder->once_per_second();
    overlay_.sendEndpoints();
    overlay_.autoConnect();

    if ((++overlay_.timer_count_ % Tuning::checkIdlePeers) == 0)
        overlay_.deleteIdlePeers();

    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, this->shared_from_this(), std::placeholders::_1)));
}

//------------------------------------------------------------------------------

template <typename P2POverlayImplmnt>
OverlayImpl<P2POverlayImplmnt>::OverlayImpl(
    Application& app,
    P2POverlay::Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : Overlay(parent)
    , P2POverlayImplmnt(
          app,
          setup,
          parent,
          serverHandler,
          resourceManager,
          resolver,
          io_service,
          config,
          collector)
    , timer_count_(0)
    , slots_(app, *this)
{
    // TODO
    timer_ = std::make_shared<Timer>(*this);
    timer_->run();
}

template <typename P2POverlayImplmnt>
OverlayImpl<P2POverlayImplmnt>::~OverlayImpl()
{
    this->stop();
    timer_->stop();  // TODO

    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock<decltype(this->mutex_)> lock(this->mutex_);
    this->cond_.wait(lock, [this] { return this->list_.empty(); });
}

//------------------------------------------------------------------------------
template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::onPeerDeactivate(
    Peer::id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot)
{
    std::lock_guard lock(this->mutex_);
    ids_.erase(id);
    auto const iter = m_peers.find(slot);
    assert(iter != m_peers.end());
    m_peers.erase(iter);
}

template <typename P2POverlayImplmnt>
std::shared_ptr<typename P2POverlayImplmnt::P2PeerImp_t>
OverlayImpl<P2POverlayImplmnt>::mkInboundPeer(
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    Resource::Consumer consumer,
    ProtocolVersion protocol,
    std::unique_ptr<stream_type>&& stream_ptr)
{
    auto peer = std::make_shared<PeerImp<P2PeerImp_t>>(
        this->app_,
        id,
        slot,
        std::move(request),
        publicKey,
        protocol,
        consumer,
        std::move(stream_ptr),
        *this);

    std::lock_guard lock(this->mutex_);

    {
        auto const result = m_peers.emplace(peer->slot(), peer);
        assert(result.second);
        (void)result.second;
    }

    {
        auto const result = ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->p2p().id()),
            std::make_tuple(peer));
        assert(result.second);
        (void)result.second;
    }

    return peer;
}

template <typename P2POverlayImplmnt>
std::shared_ptr<typename P2POverlayImplmnt::P2PeerImp_t>
OverlayImpl<P2POverlayImplmnt>::mkOutboundPeer(
    std::unique_ptr<stream_type>&& stream_ptr,
    boost::beast::multi_buffer const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id)
{
    auto peer = std::make_shared<PeerImp<P2PeerImp_t>>(
        this->app_,
        std::move(stream_ptr),
        buffers.data(),
        std::move(slot),
        std::move(response),
        usage,
        publicKey,
        protocol,
        id,
        *this);

    std::lock_guard lock(this->mutex_);

    {
        auto const result = m_peers.emplace(peer->slot(), peer);
        assert(result.second);
        (void)result.second;
    }

    {
        auto const result = ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->p2p().id()),
            std::make_tuple(peer));
        assert(result.second);
        (void)result.second;
    }

    return peer;
}

template <typename P2POverlayImplmnt>
bool
OverlayImpl<P2POverlayImplmnt>::processRequest(
    http_request_type const& req,
    Handoff& handoff)
{
    // Take advantage of || short-circuiting
    return processCrawl(req, handoff) || processValidatorList(req, handoff) ||
        processHealth(req, handoff);
}
//------------------------------------------------------------------------------

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::onManifests(
    std::shared_ptr<protocol::TMManifests> const& m,
    std::shared_ptr<PeerImp<P2PeerImp_t>> const& from)
{
    auto const n = m->list_size();
    auto const& journal = from->pjournal();

    protocol::TMManifests relay;

    for (std::size_t i = 0; i < n; ++i)
    {
        auto& s = m->list().Get(i).stobject();

        if (auto mo = deserializeManifest(s))
        {
            auto const serialized = mo->serialized;

            auto const result =
                this->app_.validatorManifests().applyManifest(std::move(*mo));

            if (result == ManifestDisposition::accepted)
            {
                relay.add_list()->set_stobject(s);

                // N.B.: this is important; the applyManifest call above moves
                //       the loaded Manifest out of the optional so we need to
                //       reload it here.
                mo = deserializeManifest(serialized);
                assert(mo);

                this->app_.getOPs().pubManifest(*mo);

                if (this->app_.validators().listed(mo->masterKey))
                {
                    auto db = this->app_.getWalletDB().checkoutDb();
                    addValidatorManifest(*db, serialized);
                }
            }
        }
        else
        {
            JLOG(journal.debug())
                << "Malformed manifest #" << i + 1 << ": " << strHex(s);
            continue;
        }
    }

    if (!relay.list().empty())
        for_each([m2 = std::make_shared<Message>(relay, protocol::mtMANIFESTS)](
                     std::shared_ptr<PeerImp<P2PeerImp_t>>&& p) {
            p->p2p().send(m2);
        });
}

template <typename P2POverlayImplmnt>
Json::Value
OverlayImpl<P2POverlayImplmnt>::crawlShards(bool pubKey, std::uint32_t hops)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    Json::Value jv(Json::objectValue);
    auto const numPeers{size()};
    if (numPeers == 0)
        return jv;

    // If greater than a hop away, we may need to gather or freshen data
    if (hops > 0)
    {
        // Prevent crawl spamming
        clock_type::time_point const last(csLast_.load());
        if ((clock_type::now() - last) > 60s)
        {
            auto const timeout(seconds((hops * hops) * 10));
            std::unique_lock<std::mutex> l{csMutex_};

            // Check if already requested
            if (csIDs_.empty())
            {
                {
                    std::lock_guard lock{this->mutex_};
                    for (auto& id : this->ids_)
                        csIDs_.emplace(id.first);
                }

                // Relay request to active peers
                protocol::TMGetPeerShardInfo tmGPS;
                tmGPS.set_hops(hops);
                this->foreach(send_always(std::make_shared<Message>(
                    tmGPS, protocol::mtGET_PEER_SHARD_INFO)));

                if (csCV_.wait_for(l, timeout) == std::cv_status::timeout)
                {
                    csIDs_.clear();
                    csCV_.notify_all();
                }
                csLast_ = duration_cast<seconds>(
                    clock_type::now().time_since_epoch());
            }
            else
                csCV_.wait_for(l, timeout);
        }
    }

    // Combine the shard info from peers and their sub peers
    hash_map<PublicKey, typename PeerImp<P2PeerImp_t>::ShardInfo>
        peerShardInfo;
    for_each([&](std::shared_ptr<PeerImp<P2PeerImp_t>> const& peer) {
        if (auto psi = peer->getPeerShardInfo())
        {
            // e is non-const so it may be moved from
            for (auto& e : *psi)
            {
                auto it{peerShardInfo.find(e.first)};
                if (it != peerShardInfo.end())
                    // The key exists so join the shard indexes.
                    it->second.shardIndexes += e.second.shardIndexes;
                else
                    peerShardInfo.emplace(std::move(e));
            }
        }
    });

    // Prepare json reply
    auto& av = jv[jss::peers] = Json::Value(Json::arrayValue);
    for (auto const& e : peerShardInfo)
    {
        auto& pv{av.append(Json::Value(Json::objectValue))};
        if (pubKey)
            pv[jss::public_key] = toBase58(TokenType::NodePublic, e.first);

        auto const& address{e.second.endpoint.address()};
        if (!address.is_unspecified())
            pv[jss::ip] = address.to_string();

        pv[jss::complete_shards] = to_string(e.second.shardIndexes);
    }

    return jv;
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::lastLink(std::uint32_t id)
{
    // Notify threads when every peer has received a last link.
    // This doesn't account for every node that might reply but
    // it is adequate.
    std::lock_guard l{csMutex_};
    if (csIDs_.erase(id) && csIDs_.empty())
        csCV_.notify_all();
}

/** The number of active peers on the network
    Active peers are only those peers that have completed the handshake
    and are running the Ripple protocol.
*/
template <typename P2POverlayImplmnt>
std::size_t
OverlayImpl<P2POverlayImplmnt>::size() const
{
    std::lock_guard lock(this->mutex_);
    return this->ids_.size();
}

template <typename P2POverlayImplmnt>
Json::Value
OverlayImpl<P2POverlayImplmnt>::getOverlayInfo()
{
    using namespace std::chrono;
    Json::Value jv;
    auto& av = jv["active"] = Json::Value(Json::arrayValue);

    for_each([&](std::shared_ptr<PeerImp<P2PeerImp_t>>&& sp) {
        auto& pv = av.append(Json::Value(Json::objectValue));
        pv[jss::public_key] = base64_encode(
            sp->p2p().getNodePublic().data(), sp->p2p().getNodePublic().size());
        pv[jss::type] = sp->slot()->inbound() ? "in" : "out";
        pv[jss::uptime] = static_cast<std::uint32_t>(
            duration_cast<seconds>(sp->uptime()).count());
        if (sp->crawl())
        {
            pv[jss::ip] = sp->p2p().getRemoteAddress().address().to_string();
            if (sp->slot()->inbound())
            {
                if (auto port = sp->slot()->listening_port())
                    pv[jss::port] = *port;
            }
            else
            {
                pv[jss::port] =
                    std::to_string(sp->p2p().getRemoteAddress().port());
            }
        }

        {
            auto version{sp->getVersion()};
            if (!version.empty())
                // Could move here if Json::value supported moving from strings
                pv[jss::version] = version;
        }

        std::uint32_t minSeq, maxSeq;
        sp->ledgerRange(minSeq, maxSeq);
        if (minSeq != 0 || maxSeq != 0)
            pv[jss::complete_ledgers] =
                std::to_string(minSeq) + "-" + std::to_string(maxSeq);

        if (auto shardIndexes = sp->getShardIndexes())
            pv[jss::complete_shards] = to_string(*shardIndexes);
    });

    return jv;
}

template <typename P2POverlayImplmnt>
Json::Value
OverlayImpl<P2POverlayImplmnt>::getServerInfo()
{
    bool const humanReadable = false;
    bool const admin = false;
    bool const counters = false;

    Json::Value server_info =
        this->app_.getOPs().getServerInfo(humanReadable, admin, counters);

    // Filter out some information
    server_info.removeMember(jss::hostid);
    server_info.removeMember(jss::load_factor_fee_escalation);
    server_info.removeMember(jss::load_factor_fee_queue);
    server_info.removeMember(jss::validation_quorum);

    if (server_info.isMember(jss::validated_ledger))
    {
        Json::Value& validated_ledger = server_info[jss::validated_ledger];

        validated_ledger.removeMember(jss::base_fee);
        validated_ledger.removeMember(jss::reserve_base_xrp);
        validated_ledger.removeMember(jss::reserve_inc_xrp);
    }

    return server_info;
}

template <typename P2POverlayImplmnt>
Json::Value
OverlayImpl<P2POverlayImplmnt>::getServerCounts()
{
    return getCountsJson(this->app_, 10);
}

template <typename P2POverlayImplmnt>
Json::Value
OverlayImpl<P2POverlayImplmnt>::getUnlInfo()
{
    Json::Value validators = this->app_.validators().getJson();

    if (validators.isMember(jss::publisher_lists))
    {
        Json::Value& publisher_lists = validators[jss::publisher_lists];

        for (auto& publisher : publisher_lists)
        {
            publisher.removeMember(jss::list);
        }
    }

    validators.removeMember(jss::signing_keys);
    validators.removeMember(jss::trusted_validator_keys);
    validators.removeMember(jss::validation_quorum);

    Json::Value validatorSites = this->app_.validatorSites().getJson();

    if (validatorSites.isMember(jss::validator_sites))
    {
        validators[jss::validator_sites] =
            std::move(validatorSites[jss::validator_sites]);
    }

    return validators;
}

// Returns information on verified peers.
template <typename P2POverlayImplmnt>
Json::Value
OverlayImpl<P2POverlayImplmnt>::json()
{
    Json::Value json;
    for (auto const& peer : this->getActivePeers())
    {
        json.append(peer->json());
    }
    return json;
}

template <typename P2POverlayImplmnt>
std::shared_ptr<Peer>
OverlayImpl<P2POverlayImplmnt>::findPeerByShortID(Peer::id_t const& id) const
{
    std::lock_guard lock(this->mutex_);
    auto const iter = ids_.find(id);
    if (iter != ids_.end())
        return iter->second.lock();
    return {};
}

// A public key hash map was not used due to the peer connect/disconnect
// update overhead outweighing the performance of a small set linear search.
template <typename P2POverlayImplmnt>
std::shared_ptr<Peer>
OverlayImpl<P2POverlayImplmnt>::findPeerByPublicKey(PublicKey const& pubKey)
{
    std::lock_guard lock(this->mutex_);
    for (auto const& e : ids_)
    {
        if (auto peer = e.second.lock())
        {
            if (peer->getNodePublic() == pubKey)
                return peer;
        }
    }
    return {};
}

template <typename P2POverlayImplmnt>
bool
OverlayImpl<P2POverlayImplmnt>::processCrawl(
    http_request_type const& req,
    Handoff& handoff)
{
    if (req.target() != "/crawl" ||
        this->setup_.crawlOptions == CrawlOptions::Disabled)
        return false;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.result(boost::beast::http::status::ok);
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");
    msg.body()["version"] = Json::Value(2u);

    if (this->setup_.crawlOptions & CrawlOptions::Overlay)
    {
        msg.body()["overlay"] = getOverlayInfo();
    }
    if (this->setup_.crawlOptions & CrawlOptions::ServerInfo)
    {
        msg.body()["server"] = getServerInfo();
    }
    if (this->setup_.crawlOptions & CrawlOptions::ServerCounts)
    {
        msg.body()["counts"] = getServerCounts();
    }
    if (this->setup_.crawlOptions & CrawlOptions::Unl)
    {
        msg.body()["unl"] = getUnlInfo();
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

template <typename P2POverlayImplmnt>
bool
OverlayImpl<P2POverlayImplmnt>::processValidatorList(
    http_request_type const& req,
    Handoff& handoff)
{
    // If the target is in the form "/vl/<validator_list_public_key>",
    // return the most recent validator list for that key.
    constexpr std::string_view prefix("/vl/");

    if (!req.target().starts_with(prefix.data()) || !this->setup_.vlEnabled)
        return false;

    std::uint32_t version = 1;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto fail = [&msg, &handoff](auto status) {
        msg.result(status);
        msg.insert("Content-Length", "0");

        msg.body() = Json::nullValue;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    };

    auto key = req.target().substr(prefix.size());

    if (auto slash = key.find('/'); slash != boost::string_view::npos)
    {
        auto verString = key.substr(0, slash);
        if (!boost::conversion::try_lexical_convert(verString, version))
            return fail(boost::beast::http::status::bad_request);
        key = key.substr(slash + 1);
    }

    if (key.empty())
        return fail(boost::beast::http::status::bad_request);

    // find the list
    auto vl = this->app_.validators().getAvailable(key, version);

    if (!vl)
    {
        // 404 not found
        return fail(boost::beast::http::status::not_found);
    }
    else if (!*vl)
    {
        return fail(boost::beast::http::status::bad_request);
    }
    else
    {
        msg.result(boost::beast::http::status::ok);

        msg.body() = *vl;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    }
}

template <typename P2POverlayImplmnt>
bool
OverlayImpl<P2POverlayImplmnt>::processHealth(
    http_request_type const& req,
    Handoff& handoff)
{
    if (req.target() != "/health")
        return false;
    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto info = getServerInfo();

    int last_validated_ledger_age = -1;
    if (info.isMember("validated_ledger"))
        last_validated_ledger_age = info["validated_ledger"]["age"].asInt();
    bool amendment_blocked = false;
    if (info.isMember("amendment_blocked"))
        amendment_blocked = true;
    int number_peers = info["peers"].asInt();
    std::string server_state = info["server_state"].asString();
    auto load_factor =
        info["load_factor"].asDouble() / info["load_base"].asDouble();

    enum { healthy, warning, critical };
    int health = healthy;
    auto set_health = [&health](int state) {
        if (health < state)
            health = state;
    };

    msg.body()[jss::info] = Json::objectValue;
    if (last_validated_ledger_age >= 7 || last_validated_ledger_age < 0)
    {
        msg.body()[jss::info]["validated_ledger"] = last_validated_ledger_age;
        if (last_validated_ledger_age < 20)
            set_health(warning);
        else
            set_health(critical);
    }

    if (amendment_blocked)
    {
        msg.body()[jss::info]["amendment_blocked"] = true;
        set_health(critical);
    }

    if (number_peers <= 7)
    {
        msg.body()[jss::info]["peers"] = number_peers;
        if (number_peers != 0)
            set_health(warning);
        else
            set_health(critical);
    }

    if (!(server_state == "full" || server_state == "validating" ||
          server_state == "proposing"))
    {
        msg.body()[jss::info]["server_state"] = server_state;
        if (server_state == "syncing" || server_state == "tracking" ||
            server_state == "connected")
        {
            set_health(warning);
        }
        else
            set_health(critical);
    }

    if (load_factor > 100)
    {
        msg.body()[jss::info]["load_factor"] = load_factor;
        if (load_factor < 1000)
            set_health(warning);
        else
            set_health(critical);
    }

    switch (health)
    {
        case healthy:
            msg.result(boost::beast::http::status::ok);
            break;
        case warning:
            msg.result(boost::beast::http::status::service_unavailable);
            break;
        case critical:
            msg.result(boost::beast::http::status::internal_server_error);
            break;
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

template <typename P2POverlayImplmnt>
Overlay::PeerSequence
OverlayImpl<P2POverlayImplmnt>::getActivePeers() const
{
    Overlay::PeerSequence ret;
    ret.reserve(size());

    for_each([&ret](std::shared_ptr<PeerImp<P2PeerImp_t>>&& sp) {
        ret.emplace_back(std::move(sp));
    });

    return ret;
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::checkTracking(std::uint32_t index)
{
    for_each([index](std::shared_ptr<PeerImp<P2PeerImp_t>>&& sp) {
        sp->checkTracking(index);
    });
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::broadcast(protocol::TMProposeSet& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER);
    for_each([&](std::shared_ptr<PeerImp<P2PeerImp_t>>&& p) {
        p->p2p().send(sm);
    });
}

template <typename P2POverlayImplmnt>
std::set<Peer::id_t>
OverlayImpl<P2POverlayImplmnt>::relay(
    protocol::TMProposeSet& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = this->app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER, validator);
        for_each([&](std::shared_ptr<PeerImp<P2PeerImp_t>>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->p2p().send(sm);
        });
        return *toSkip;
    }
    return {};
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::broadcast(protocol::TMValidation& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtVALIDATION);
    for_each([sm](std::shared_ptr<PeerImp<P2PeerImp_t>>&& p) {
        p->p2p().send(sm);
    });
}

template <typename P2POverlayImplmnt>
std::set<Peer::id_t>
OverlayImpl<P2POverlayImplmnt>::relay(
    protocol::TMValidation& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = this->app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtVALIDATION, validator);
        for_each([&](std::shared_ptr<PeerImp<P2PeerImp_t>>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->p2p().send(sm);
        });
        return *toSkip;
    }
    return {};
}

template <typename P2POverlayImplmnt>
std::shared_ptr<Message>
OverlayImpl<P2POverlayImplmnt>::getManifestsMessage()
{
    std::lock_guard g(manifestLock_);

    if (auto seq = this->app_.validatorManifests().sequence();
        seq != manifestListSeq_)
    {
        protocol::TMManifests tm;

        this->app_.validatorManifests().for_each_manifest(
            [&tm](std::size_t s) { tm.mutable_list()->Reserve(s); },
            [&tm, &hr = this->app_.getHashRouter()](Manifest const& manifest) {
                tm.add_list()->set_stobject(
                    manifest.serialized.data(), manifest.serialized.size());
                hr.addSuppression(manifest.hash());
            });

        manifestMessage_.reset();

        if (tm.list_size() != 0)
            manifestMessage_ =
                std::make_shared<Message>(tm, protocol::mtMANIFESTS);

        manifestListSeq_ = seq;
    }

    return manifestMessage_;
}

//------------------------------------------------------------------------------

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::sendEndpoints()
{
    auto const result = this->m_peerFinder->buildEndpointsForPeers();
    for (auto const& e : result)
    {
        std::shared_ptr<PeerImp<P2PeerImp_t>> peer;
        {
            std::lock_guard lock(this->mutex_);
            auto const iter = this->m_peers.find(e.first);
            if (iter != this->m_peers.end())
                peer = iter->second.lock();
        }
        if (peer)
            peer->sendEndpoints(e.second.begin(), e.second.end());
    }
}

std::shared_ptr<Message>
makeSquelchMessage(
    PublicKey const& validator,
    bool squelch,
    uint32_t squelchDuration)
{
    protocol::TMSquelch m;
    m.set_squelch(squelch);
    m.set_validatorpubkey(validator.data(), validator.size());
    if (squelch)
        m.set_squelchduration(squelchDuration);
    return std::make_shared<Message>(m, protocol::mtSQUELCH);
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::unsquelch(
    PublicKey const& validator,
    Peer::id_t id) const
{
    if (auto peer = findPeerByShortID(id);
        peer && this->app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
        // optimize - multiple message with different
        // validator might be sent to the same peer
        peer->p2p().send(makeSquelchMessage(validator, false, 0));
    }
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::squelch(
    PublicKey const& validator,
    Peer::id_t id,
    uint32_t squelchDuration) const
{
    if (auto peer = findPeerByShortID(id);
        peer && this->app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
        peer->p2p().send(makeSquelchMessage(validator, true, squelchDuration));
    }
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    std::set<Peer::id_t>&& peers,
    protocol::MessageType type)
{
    if (!this->strand_.running_in_this_thread())
        return post(
            this->strand_,
            [this, key, validator, peers = std::move(peers), type]() mutable {
                updateSlotAndSquelch(key, validator, std::move(peers), type);
            });

    for (auto id : peers)
        slots_.updateSlotAndSquelch(key, validator, id, type);
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    Peer::id_t peer,
    protocol::MessageType type)
{
    if (!this->strand_.running_in_this_thread())
        return post(this->strand_, [this, key, validator, peer, type]() {
            updateSlotAndSquelch(key, validator, peer, type);
        });

    slots_.updateSlotAndSquelch(key, validator, peer, type);
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::deletePeer(Peer::id_t id)
{
    if (!this->strand_.running_in_this_thread())
        return post(
            this->strand_, std::bind(&OverlayImpl::deletePeer, this, id));

    slots_.deletePeer(id, true);
}

template <typename P2POverlayImplmnt>
void
OverlayImpl<P2POverlayImplmnt>::deleteIdlePeers()
{
    if (!this->strand_.running_in_this_thread())
        return post(
            this->strand_, std::bind(&OverlayImpl::deleteIdlePeers, this));

    slots_.deleteIdlePeers();
}

}  // namespace ripple

#endif
