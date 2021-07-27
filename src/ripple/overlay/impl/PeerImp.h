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

#ifndef RIPPLE_OVERLAY_PEERIMP_H_INCLUDED
#define RIPPLE_OVERLAY_PEERIMP_H_INCLUDED

#include <ripple/app/consensus/RCLCxPeerPos.h>
#include <ripple/app/ledger/impl/LedgerReplayMsgHandler.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/nodestore/ShardInfo.h>
#include <ripple/overlay/Squelch.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/resource/Fees.h>

namespace ripple {

struct ValidatorBlobInfo;
class SHAMap;

class PeerImp : public P2PeerImp
{
public:
    /** Whether the peer's view of the ledger converges or diverges from ours */
    enum class Tracking { diverged, unknown, converged };

private:
    using waitable_timer =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

    beast::WrappedSink p_sink_;
    beast::Journal const p_journal_;
    waitable_timer timer_;

    std::atomic<Tracking> tracking_;
    clock_type::time_point trackingTime_;

    // The indices of the smallest and largest ledgers this peer has available
    //
    LedgerIndex minLedger_ = 0;
    LedgerIndex maxLedger_ = 0;
    uint256 closedLedgerHash_;
    uint256 previousLedgerHash_;

    boost::circular_buffer<uint256> recentLedgers_{128};
    boost::circular_buffer<uint256> recentTxSets_{128};

    std::optional<std::chrono::milliseconds> latency_;
    std::optional<std::uint32_t> lastPingSeq_;
    clock_type::time_point lastPingTime_;
    clock_type::time_point const creationTime_;

    reduce_relay::Squelch<UptimeClock> squelch_;
    inline static std::atomic_bool reduceRelayReady_{false};

    // Notes on thread locking:
    //
    // During an audit it was noted that some member variables that looked
    // like they need thread protection were not receiving it.  And, indeed,
    // that was correct.  But the multi-phase initialization of PeerImp
    // makes such an audit difficult.  A further audit suggests that the
    // locking is now protecting variables that don't need it.  We're
    // leaving that locking in place (for now) as a form of future proofing.
    //
    // Here are the variables that appear to need locking currently:
    //
    // o closedLedgerHash_
    // o previousLedgerHash_
    // o minLedger_
    // o maxLedger_
    // o recentLedgers_
    // o recentTxSets_
    // o trackingTime_
    // o latency_
    //
    // The following variables are being protected preemptively:
    //
    // o name_
    // o last_status_
    //
    // June 2019

    std::mutex mutable recentLock_;
    protocol::TMStatusChange last_status_;
    Resource::Consumer usage_;
    Resource::Charge fee_;
    std::unique_ptr<LoadEvent> load_event_;
    // The highest sequence of each PublisherList that has
    // been sent to or received from this peer.
    hash_map<PublicKey, std::size_t> publisherListSequences_;

    // Any known shard info from this peer and its sub peers
    hash_map<PublicKey, NodeStore::ShardInfo> shardInfos_;
    std::mutex mutable shardInfoMutex_;

    // true if validation/proposal reduce-relay feature is enabled
    // on the peer.
    bool vpReduceRelayEnabled_ = false;
    bool ledgerReplayEnabled_ = false;
    LedgerReplayMsgHandler ledgerReplayMsgHandler_;

    friend class OverlayImpl;

public:
    PeerImp(PeerImp const&) = delete;
    PeerImp&
    operator=(PeerImp const&) = delete;

    /** Create an active incoming peer from an established ssl connection. */
    PeerImp(
        Application& app,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr,
        OverlayImpl& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    PeerImp(
        Application& app,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        OverlayImpl& overlay);

    virtual ~PeerImp();

    beast::Journal const&
    pjournal() const
    {
        return p_journal_;
    }

    /** Send a set of PeerFinder endpoints as a protocol message. */
    template <
        class FwdIt,
        class = typename std::enable_if_t<std::is_same<
            typename std::iterator_traits<FwdIt>::value_type,
            PeerFinder::Endpoint>::value>>
    void
    sendEndpoints(FwdIt first, FwdIt last);

    void
    charge(Resource::Charge const& fee) override;

    /** Returns `true` if this connection will publicly share its IP address. */
    bool
    crawl() const;

    bool
    cluster() const override;

    /** Check if the peer is tracking
        @param validationSeq The ledger sequence of a recently-validated ledger
    */
    void
    checkTracking(std::uint32_t validationSeq);

    void
    checkTracking(std::uint32_t seq1, std::uint32_t seq2);

    // Return the connection elapsed time.
    clock_type::duration
    uptime() const
    {
        return clock_type::now() - creationTime_;
    }

    Json::Value
    json() override;

    bool
    supportsFeature(ProtocolFeature f) const override;

    std::optional<std::size_t>
    publisherListSequence(PublicKey const& pubKey) const override
    {
        std::lock_guard<std::mutex> sl(recentLock_);

        auto iter = publisherListSequences_.find(pubKey);
        if (iter != publisherListSequences_.end())
            return iter->second;
        return {};
    }

    void
    setPublisherListSequence(PublicKey const& pubKey, std::size_t const seq)
        override
    {
        std::lock_guard<std::mutex> sl(recentLock_);

        publisherListSequences_[pubKey] = seq;
    }

    //
    // Ledger
    //

    uint256 const&
    getClosedLedgerHash() const override
    {
        return closedLedgerHash_;
    }

    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override;

    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override;

    bool
    hasTxSet(uint256 const& hash) const override;

    void
    cycleStatus() override;

    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override;

    // Called to determine our priority for querying
    int
    getScore(bool haveItem) const override;

    bool
    isHighLatency() const override;

    // Return any known shard info from this peer and its sub peers
    [[nodiscard]] hash_map<PublicKey, NodeStore::ShardInfo> const
    getPeerShardInfos() const;

private:
    void
    setTimer();

    void
    cancelTimer();

    // Called when the timer wait completes
    void
    onTimer(boost::system::error_code const& ec);

    // Check if reduce-relay feature is enabled and
    // reduce_relay::WAIT_ON_BOOTUP time passed since the start
    bool
    reduceRelayReady();

public:
    //--------------------------------------------------------------------------
    //
    // ProtocolStream
    //
    //--------------------------------------------------------------------------

    void
    onMessageUnknown(std::uint16_t type);

    void
    onMessageBegin(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m,
        std::size_t size,
        std::size_t uncompressed_size,
        bool isCompressed);

    void
    onMessageEnd(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m);

    void
    onMessage(std::shared_ptr<protocol::TMManifests> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMPing> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMCluster> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetPeerShardInfo> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMPeerShardInfo> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetPeerShardInfoV2> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMPeerShardInfoV2> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMEndpoints> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMTransaction> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetLedger> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMLedgerData> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProposeSet> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMStatusChange> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMHaveTransactionSet> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidatorList> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidatorListCollection> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidation> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetObjectByHash> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMSquelch> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProofPathRequest> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProofPathResponse> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMReplayDeltaRequest> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMReplayDeltaResponse> const& m);

private:
    //--------------------------------------------------------------------------
    // lockedRecentLock is passed as a reminder to callers that recentLock_
    // must be locked.
    void
    addLedger(
        uint256 const& hash,
        std::lock_guard<std::mutex> const& lockedRecentLock);

    void
    doFetchPack(const std::shared_ptr<protocol::TMGetObjectByHash>& packet);

    void
    onValidatorListMessage(
        std::string const& messageType,
        std::string const& manifest,
        std::uint32_t version,
        std::vector<ValidatorBlobInfo> const& blobs);

    void
    checkTransaction(
        int flags,
        bool checkSignature,
        std::shared_ptr<STTx const> const& stx);

    void
    checkPropose(
        Job& job,
        std::shared_ptr<protocol::TMProposeSet> const& packet,
        RCLCxPeerPos peerPos);

    void
    checkValidation(
        std::shared_ptr<STValidation> const& val,
        std::shared_ptr<protocol::TMValidation> const& packet);

    void
    sendLedgerBase(
        std::shared_ptr<Ledger const> const& ledger,
        protocol::TMLedgerData& ledgerData);

    std::shared_ptr<Ledger const>
    getLedger(std::shared_ptr<protocol::TMGetLedger> const& m);

    std::shared_ptr<SHAMap const>
    getTxSet(std::shared_ptr<protocol::TMGetLedger> const& m) const;

    void
    processLedgerRequest(std::shared_ptr<protocol::TMGetLedger> const& m);

private:
    void
    onEvtRun() override;

    void
    onEvtClose() override;

    void
    onEvtGracefulClose() override;

    void
    onEvtShutdown() override;

    void
    onEvtDoProtocolStart() override;

    bool
    onEvtProtocolMessage(
        detail::MessageHeader const& header,
        mutable_buffers_type const& buffers) override;

    virtual bool
    onEvtSendFilter(std::shared_ptr<Message> const&) override;

    std::shared_ptr<PeerImp>
    shared()
    {
        return std::static_pointer_cast<PeerImp>(shared_from_this());
    }
};

//------------------------------------------------------------------------------

template <class Buffers>
PeerImp::PeerImp(
    Application& app,
    std::unique_ptr<stream_type>&& stream_ptr,
    Buffers const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    OverlayImpl& overlay)
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
    , p_sink_(app_.journal("Protocol"), makePrefix(id))
    , p_journal_(p_sink_)
    , timer_(waitable_timer{socket_.get_executor()})
    , tracking_(Tracking::unknown)
    , trackingTime_(clock_type::now())
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , squelch_(app_.journal("Squelch"))
    , usage_(usage)
    , fee_(Resource::feeLightPeer)
    , vpReduceRelayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_VPRR,
          app_.config().VP_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(peerFeatureEnabled(
          headers_,
          FEATURE_LEDGER_REPLAY,
          app_.config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    JLOG(journal_.debug()) << "compression enabled "
                           << (compressionEnabled_ == Compressed::On)
                           << " vp reduce-relay enabled "
                           << vpReduceRelayEnabled_ << " on " << remote_address_
                           << " " << id_;
}

template <class FwdIt, class>
void
PeerImp::sendEndpoints(FwdIt first, FwdIt last)
{
    protocol::TMEndpoints tm;

    while (first != last)
    {
        auto& tme2(*tm.add_endpoints_v2());
        tme2.set_endpoint(first->address.to_string());
        tme2.set_hops(first->hops);
        first++;
    }
    tm.set_version(2);

    send(std::make_shared<Message>(tm, protocol::mtENDPOINTS));
}

}  // namespace ripple

#endif
