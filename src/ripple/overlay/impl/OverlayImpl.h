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

#include <ripple/basics/chrono.h>
#include <ripple/core/Job.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>

#include <boost/asio/basic_waitable_timer.hpp>

namespace ripple {

template <typename>
class PeerImp;
class BasicConfig;

class OverlayImpl : public Overlay,
                    public P2POverlayImpl<PeerImp<P2PeerImp>>,
                    public reduce_relay::SquelchHandler
{
private:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;

    struct Timer : std::enable_shared_from_this<Timer>
    {
        OverlayImpl& overlay_;
        boost::asio::basic_waitable_timer<clock_type> timer_;

        explicit Timer(OverlayImpl& overlay);

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

    //--------------------------------------------------------------------------

public:
    OverlayImpl(
        Application& app,
        Setup const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    ~OverlayImpl();

    OverlayImpl(OverlayImpl const&) = delete;
    OverlayImpl&
    operator=(OverlayImpl const&) = delete;

    std::size_t
    size() const override;

    Json::Value
    json() override;

    PeerSequence
    getActivePeers() const override;

    void checkTracking(std::uint32_t) override;

    std::shared_ptr<P2Peer>
    findPeerByShortID(Peer::id_t const& id) const override;

    std::shared_ptr<P2Peer>
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
        std::vector<std::weak_ptr<PeerImp<P2PeerImp>>> wp;
        {
            std::lock_guard lock(mutex_);

            // Iterate over a copy of the peer list because peer
            // destruction can invalidate iterators.
            wp.reserve(ids_.size());

            for (auto& x : ids_)
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
        std::shared_ptr<PeerImp<P2PeerImp>> const& from);

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

    std::shared_ptr<PeerImp<P2PeerImp>>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        Resource::Consumer consumer,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr) override;

    std::shared_ptr<PeerImp<P2PeerImp>>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) override;

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

#endif
