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
#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/impl/LedgerReplayMsgHandler.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/UptimeClock.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/random.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/Squelch.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/digest.h>

#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <sstream>

namespace ripple {

struct ValidatorBlobInfo;

template <typename>
class OverlayImpl;

namespace {
/** The threshold above which we treat a peer connection as high latency */
std::chrono::milliseconds constexpr peerHighLatency{300};
/** How often we PING the peer to check for latency and sendq probe */
std::chrono::seconds constexpr peerTimerInterval{60};

using namespace std::chrono_literals;
}  // namespace

template <typename P2PeerImplmnt>
class PeerImp : public Peer, public P2PeerImplmnt
{
    using P2POverlayImpl_t = typename P2PeerImplmnt::P2POverlayImpl_t;
    static_assert(
        std::is_base_of<P2Peer, P2PeerImplmnt>::value,
        "P2PeerImplmnt must inherit from P2Peer");

    std::shared_ptr<PeerImp<P2PeerImplmnt>>
    shared()
    {
        return std::static_pointer_cast<PeerImp<P2PeerImplmnt>>(
            this->shared_from_this());
    }

    using id_t = P2Peer::id_t;

public:
    /** Whether the peer's view of the ledger converges or diverges from ours */
    enum class Tracking { diverged, unknown, converged };

    struct ShardInfo
    {
        beast::IP::Endpoint endpoint;
        RangeSet<std::uint32_t> shardIndexes;
    };

private:
    using error_code = boost::system::error_code;
    using clock_type = std::chrono::steady_clock;
    using waitable_timer =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

    P2Peer& p2p_;
    OverlayImpl<P2POverlayImpl_t>& overlay_;
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

    std::optional<std::uint32_t> lastPingSeq_;
    clock_type::time_point lastPingTime_;
    clock_type::time_point const creationTime_;
    Resource::Consumer usage_;
    Resource::Charge fee_;
    std::optional<std::chrono::milliseconds> latency_;
    std::mutex mutable recentLock_;

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

    protocol::TMStatusChange last_status_;
    std::unique_ptr<LoadEvent> load_event_;
    // The highest sequence of each PublisherList that has
    // been sent to or received from this peer.
    hash_map<PublicKey, std::size_t> publisherListSequences_;

    std::mutex mutable shardInfoMutex_;
    hash_map<PublicKey, ShardInfo> shardInfo_;

    // true if validation/proposal reduce-relay feature is enabled
    // on the peer.
    bool vpReduceRelayEnabled_ = false;
    bool ledgerReplayEnabled_ = false;
    LedgerReplayMsgHandler ledgerReplayMsgHandler_;

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
        OverlayImpl<P2POverlayImpl_t>& overlay);

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
        OverlayImpl<P2POverlayImpl_t>& overlay);

    virtual ~PeerImp();

    P2Peer&
    p2p() override
    {
        return p2p_;
    }

    beast::Journal const&
    pjournal() const
    {
        return p_journal_;
    }

    // Work-around for calling shared_from_this in constructors
    // void
    // run();

    /** Send a set of PeerFinder endpoints as a protocol message. */
    template <
        class FwdIt,
        class = typename std::enable_if_t<std::is_same<
            typename std::iterator_traits<FwdIt>::value_type,
            PeerFinder::Endpoint>::value>>
    void
    sendEndpoints(FwdIt first, FwdIt last);

    /** Returns `true` if this connection will publicly share its IP address. */
    bool
    crawl() const;

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
        std::lock_guard<std::mutex> sl(this->recentLock());

        auto iter = publisherListSequences_.find(pubKey);
        if (iter != publisherListSequences_.end())
            return iter->second;
        return {};
    }

    void
    setPublisherListSequence(PublicKey const& pubKey, std::size_t const seq)
        override
    {
        std::lock_guard<std::mutex> sl(this->recentLock());

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
    hasShard(std::uint32_t shardIndex) const override;

    bool
    hasTxSet(uint256 const& hash) const override;

    void
    cycleStatus() override;

    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override;

    int
    getScore(bool haveItem) const override;

    bool
    isHighLatency() const override;

    /** Return a range set of known shard indexes from this peer. */
    std::optional<RangeSet<std::uint32_t>>
    getShardIndexes() const;

    /** Return any known shard info from this peer and its sub peers. */
    std::optional<hash_map<PublicKey, ShardInfo>>
    getPeerShardInfo() const;

    void
    charge(Resource::Charge const& fee) override;

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

    std::mutex&
    recentLock() const override
    {
        return recentLock_;
    }

private:
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
    onMessage(std::shared_ptr<protocol::TMGetShardInfo> const& m);

    void
    onMessage(std::shared_ptr<protocol::TMShardInfo> const& m);

    void
    onMessage(std::shared_ptr<protocol::TMGetPeerShardInfo> const& m);

    void
    onMessage(std::shared_ptr<protocol::TMPeerShardInfo> const& m);

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

    void
    onMessage(std::shared_ptr<protocol::TMProtocolStarted> const& m);

    std::string
    hexDump(std::uint8_t const* data, std::uint16_t size) const
    {
        std::stringstream sstr;

        for (int i = 0; i < size; i++)
            sstr << std::setfill('0') << std::setw(2) << std::hex
                 << (static_cast<std::uint32_t>(data[i]) & 0xFF) << " ";

        return sstr.str();
    }

    template <
        class T,
        class Buffers,
        class = std::enable_if_t<
            std::is_base_of<::google::protobuf::Message, T>::value>>
    bool
    invoke(detail::MessageHeader const& header, Buffers const& buffers)
    {
        auto const m = detail::parseMessageContent<T>(header, buffers);
        if (!m)
            return false;

        using namespace ripple::compression;
        onMessageBegin(
            header.message_type,
            m,
            header.payload_wire_size,
            header.uncompressed_size,
            header.algorithm != Algorithm::None);
        onMessage(m);
        onMessageEnd(header.message_type, m);

        return true;
    }

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
    getLedger(std::shared_ptr<protocol::TMGetLedger> const& packet);

    void
    sendOnProtocolStart(bool send);

protected:
    bool
    squelched(std::shared_ptr<Message> const&) override;

    void
    onEvtProtocolStart() override;

    void
    onEvtRun() override;

    void
    onEvtClose() override;

    void
    onEvtGracefulClose() override;

    void
    onEvtShutdown() override;

    std::pair<size_t, boost::system::error_code>
    onEvtProtocolMessage(boost::beast::multi_buffer const&, size_t&) override;
};

}  // namespace ripple

#include <ripple/overlay/impl/OverlayImpl.h>

//------------------------------------------------------------------------------

namespace ripple {

template <typename P2PeerImplmnt>
template <class Buffers>
PeerImp<P2PeerImplmnt>::PeerImp(
    Application& app,
    std::unique_ptr<stream_type>&& stream_ptr,
    Buffers const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer consumer,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    OverlayImpl<P2POverlayImpl_t>& overlay)
    : P2PeerImplmnt(
          app,
          std::move(stream_ptr),
          buffers,
          std::move(slot),
          std::move(response),
          publicKey,
          protocol,
          id,
          overlay)
    , p2p_(static_cast<P2Peer&>(*this))
    , overlay_(overlay)
    , p_sink_(this->app().journal("Protocol"), P2Peer::makePrefix(id))
    , p_journal_(p_sink_)
    , timer_(waitable_timer{this->get_executor()})
    , tracking_(Tracking::unknown)
    , trackingTime_(clock_type::now())
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , usage_(consumer)
    , fee_(Resource::feeLightPeer)
    , squelch_(this->app().journal("Squelch"))
    , vpReduceRelayEnabled_(peerFeatureEnabled(
          this->headers(),
          FEATURE_VPRR,
          this->app().config().VP_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(peerFeatureEnabled(
          this->headers(),
          FEATURE_LEDGER_REPLAY,
          this->app().config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(this->app(), this->app().getLedgerReplayer())
{
    JLOG(this->journal().debug())
        << " vp reduce-relay enabled " << vpReduceRelayEnabled_;
}

template <typename P2PeerImplmnt>
PeerImp<P2PeerImplmnt>::PeerImp(
    Application& app,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr,
    OverlayImpl<P2POverlayImpl_t>& overlay)
    : P2PeerImplmnt(
          app,
          id,
          slot,
          std::move(request),
          publicKey,
          protocol,
          std::move(stream_ptr),
          overlay)
    , p2p_(static_cast<P2Peer&>(*this))
    , overlay_(overlay)
    , p_sink_(this->app().journal("Protocol"), P2Peer::makePrefix(id))
    , p_journal_(p_sink_)
    , timer_(waitable_timer{this->get_executor()})
    , tracking_(Tracking::unknown)
    , trackingTime_(clock_type::now())
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , usage_(consumer)
    , fee_(Resource::feeLightPeer)
    , squelch_(this->app().journal("Squelch"))
    , vpReduceRelayEnabled_(peerFeatureEnabled(
          this->headers(),
          FEATURE_VPRR,
          this->app().config().VP_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(peerFeatureEnabled(
          this->headers(),
          FEATURE_LEDGER_REPLAY,
          this->app().config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    JLOG(this->journal().debug())
        << " vp reduce-relay enabled " << vpReduceRelayEnabled_;
}

template <typename P2PeerImplmnt>
PeerImp<P2PeerImplmnt>::~PeerImp()
{
    JLOG(this->journal().debug()) << "~PeerImp " << this->id();
    overlay_.deletePeer(this->id());
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::charge(Resource::Charge const& fee)
{
    if ((usage_.charge(fee) == Resource::drop) && usage_.disconnect() &&
        this->strand().running_in_this_thread())
    {
        // Sever the connection
        overlay_.incPeerDisconnectCharges();
        this->fail("charge: Resources");
    }
}

template <typename P2PeerImplmnt>
template <class FwdIt, class>
void
PeerImp<P2PeerImplmnt>::sendEndpoints(FwdIt first, FwdIt last)
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

    this->send(std::make_shared<Message>(tm, protocol::mtENDPOINTS));
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::sendOnProtocolStart(bool bSend)
{
    if (!bSend)
        return;

    // Send all the validator lists that have been loaded
    if (this->isInbound() &&
        supportsFeature(ProtocolFeature::ValidatorListPropagation))
    {
        this->app().validators().for_each_available(
            [&](std::string const& manifest,
                std::uint32_t version,
                std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
                PublicKey const& pubKey,
                std::size_t maxSequence,
                uint256 const& hash) {
                ValidatorList::sendValidatorList(
                    *this,
                    0,
                    pubKey,
                    maxSequence,
                    version,
                    manifest,
                    blobInfos,
                    this->app().getHashRouter(),
                    p_journal_);

                // Don't send it next time.
                this->app().getHashRouter().addSuppressionPeer(
                    hash, this->id());
            });
    }
    else
    {
        // Instruct the connected inbound peer to start sending
        protocol::TMProtocolStarted tmPS;
        tmPS.set_time(0);
        this->send(
            std::make_shared<Message>(tmPS, protocol::mtPROTOCOL_STARTED));
    }

    if (auto m = overlay_.getManifestsMessage())
        this->send(m);

    // Request shard info from peer
    protocol::TMGetPeerShardInfo tmGPS;
    tmGPS.set_hops(0);
    this->send(
        std::make_shared<Message>(tmGPS, protocol::mtGET_PEER_SHARD_INFO));
}

// Helper function to check for valid uint256 values in protobuf buffers
static bool
stringIsUint256Sized(std::string const& pBuffStr)
{
    return pBuffStr.size() == uint256::size();
}

/*template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::run()
{
    this->doRun();
}*/

//------------------------------------------------------------------------------

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::crawl() const
{
    auto const iter = this->headers().find("Crawl");
    if (iter == this->headers().end())
        return false;
    return boost::iequals(iter->value(), "public");
}

template <typename P2PeerImplmnt>
Json::Value
PeerImp<P2PeerImplmnt>::json()
{
    Json::Value ret(Json::objectValue);

    ret[jss::public_key] =
        toBase58(TokenType::NodePublic, this->getNodePublic());
    ret[jss::address] = this->getRemoteAddress().to_string();

    if (this->isInbound())
        ret[jss::inbound] = true;

    if (this->cluster())
    {
        ret[jss::cluster] = true;

        if (auto const n = this->name(); !n.empty())
            // Could move here if Json::Value supported moving from a string
            ret[jss::name] = n;
    }

    if (auto const d = this->domain(); !d.empty())
        ret[jss::server_domain] = this->domain();

    if (auto const nid = this->headers()["Network-ID"].to_string();
        !nid.empty())
        ret[jss::network_id] = nid;

    ret[jss::load] = usage_.balance();

    if (auto const version = this->getVersion(); !version.empty())
        ret[jss::version] = version;

    ret[jss::protocol] = to_string(this->protocol());

    {
        std::lock_guard sl(this->recentLock());
        if (latency_)
            ret[jss::latency] = static_cast<Json::UInt>(latency_->count());
    }

    ret[jss::uptime] = static_cast<Json::UInt>(
        std::chrono::duration_cast<std::chrono::seconds>(uptime()).count());

    std::uint32_t minSeq, maxSeq;
    ledgerRange(minSeq, maxSeq);

    if ((minSeq != 0) || (maxSeq != 0))
        ret[jss::complete_ledgers] =
            std::to_string(minSeq) + " - " + std::to_string(maxSeq);

    switch (tracking_.load())
    {
        case Tracking::diverged:
            ret[jss::track] = "diverged";
            break;

        case Tracking::unknown:
            ret[jss::track] = "unknown";
            break;

        case Tracking::converged:
            // Nothing to do here
            break;
    }

    uint256 closedLedgerHash;
    protocol::TMStatusChange last_status;
    {
        std::lock_guard sl(this->recentLock());
        closedLedgerHash = closedLedgerHash_;
        last_status = last_status_;
    }

    if (closedLedgerHash != beast::zero)
        ret[jss::ledger] = to_string(closedLedgerHash);

    if (last_status.has_newstatus())
    {
        switch (last_status.newstatus())
        {
            case protocol::nsCONNECTING:
                ret[jss::status] = "connecting";
                break;

            case protocol::nsCONNECTED:
                ret[jss::status] = "connected";
                break;

            case protocol::nsMONITORING:
                ret[jss::status] = "monitoring";
                break;

            case protocol::nsVALIDATING:
                ret[jss::status] = "validating";
                break;

            case protocol::nsSHUTTING:
                ret[jss::status] = "shutting";
                break;

            default:
                JLOG(p_journal_.warn())
                    << "Unknown status: " << last_status.newstatus();
        }
    }

    auto p2pJson = this->json();

    ret[jss::metrics] = Json::Value(Json::objectValue);
    ret[jss::metrics][jss::total_bytes_recv] =
        p2pJson[jss::metrics][jss::total_bytes_recv];
    ret[jss::metrics][jss::total_bytes_sent] =
        p2pJson[jss::metrics][jss::total_bytes_sent];
    ret[jss::metrics][jss::avg_bps_recv] =
        p2pJson[jss::metrics][jss::avg_bps_recv];
    ret[jss::metrics][jss::avg_bps_sent] =
        p2pJson[jss::metrics][jss::avg_bps_sent];

    return ret;
}

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::supportsFeature(ProtocolFeature f) const
{
    switch (f)
    {
        case ProtocolFeature::ValidatorListPropagation:
            return this->protocol() >= make_protocol(2, 1);
        case ProtocolFeature::ValidatorList2Propagation:
            return this->protocol() >= make_protocol(2, 2);
        case ProtocolFeature::LedgerReplay:
            return ledgerReplayEnabled_;
    }
    return false;
}

//------------------------------------------------------------------------------

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::hasLedger(uint256 const& hash, std::uint32_t seq) const
{
    {
        std::lock_guard sl(this->recentLock());
        if ((seq != 0) && (seq >= minLedger_) && (seq <= maxLedger_) &&
            (tracking_.load() == Tracking::converged))
            return true;
        if (std::find(recentLedgers_.begin(), recentLedgers_.end(), hash) !=
            recentLedgers_.end())
            return true;
    }

    return seq >= this->app().getNodeStore().earliestLedgerSeq() &&
        hasShard(NodeStore::seqToShardIndex(seq));
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::ledgerRange(
    std::uint32_t& minSeq,
    std::uint32_t& maxSeq) const
{
    std::lock_guard sl(this->recentLock());

    minSeq = minLedger_;
    maxSeq = maxLedger_;
}

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::hasShard(std::uint32_t shardIndex) const
{
    std::lock_guard l{shardInfoMutex_};
    auto const it{shardInfo_.find(this->getNodePublic())};
    if (it != shardInfo_.end())
        return boost::icl::contains(it->second.shardIndexes, shardIndex);
    return false;
}

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::hasTxSet(uint256 const& hash) const
{
    std::lock_guard sl(this->recentLock());
    return std::find(recentTxSets_.begin(), recentTxSets_.end(), hash) !=
        recentTxSets_.end();
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::cycleStatus()
{
    // Operations on closedLedgerHash_ and previousLedgerHash_ must be
    // guarded by recentLock_.
    std::lock_guard sl(this->recentLock());
    previousLedgerHash_ = closedLedgerHash_;
    closedLedgerHash_.zero();
}

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::hasRange(std::uint32_t uMin, std::uint32_t uMax)
{
    std::lock_guard sl(this->recentLock());
    return (tracking_ != Tracking::diverged) && (uMin >= minLedger_) &&
        (uMax <= maxLedger_);
}

//------------------------------------------------------------------------------

template <typename P2PeerImplmnt>
std::optional<RangeSet<std::uint32_t>>
PeerImp<P2PeerImplmnt>::getShardIndexes() const
{
    std::lock_guard l{shardInfoMutex_};
    auto it{shardInfo_.find(this->getNodePublic())};
    if (it != shardInfo_.end())
        return it->second.shardIndexes;
    return std::nullopt;
}

template <typename P2PeerImplmnt>
std::optional<hash_map<PublicKey, typename PeerImp<P2PeerImplmnt>::ShardInfo>>
PeerImp<P2PeerImplmnt>::getPeerShardInfo() const
{
    std::lock_guard l{shardInfoMutex_};
    if (!shardInfo_.empty())
        return shardInfo_;
    return std::nullopt;
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::setTimer()
{
    error_code ec;
    timer_.expires_from_now(peerTimerInterval, ec);

    if (ec)
    {
        JLOG(this->journal().error()) << "setTimer: " << ec.message();
        return;
    }
    timer_.async_wait(bind_executor(
        this->strand(),
        std::bind(
            &PeerImp<P2PeerImplmnt>::onTimer,
            shared(),
            std::placeholders::_1)));
}

// convenience for ignoring the error code
template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::cancelTimer()
{
    error_code ec;
    timer_.cancel(ec);
}

//------------------------------------------------------------------------------

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onTimer(error_code const& ec)
{
    if (!this->isSocketOpen())
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        // This should never happen
        JLOG(this->journal().error()) << "onTimer: " << ec.message();
        return this->close();
    }

    if (this->incLargeSendq() >= Tuning::sendqIntervals)
    {
        this->fail("Large send queue");
        return;
    }

    if (auto const t = tracking_.load();
        !this->isInbound() && t != Tracking::converged)
    {
        clock_type::duration duration;

        {
            std::lock_guard sl(this->recentLock());
            duration = clock_type::now() - trackingTime_;
        }

        if ((t == Tracking::diverged &&
             (duration > this->app().config().MAX_DIVERGED_TIME)) ||
            (t == Tracking::unknown &&
             (duration > this->app().config().MAX_UNKNOWN_TIME)))
        {
            overlay_.p2p().peerFinder().on_failure(this->slot());
            this->fail("Not useful");
            return;
        }
    }

    // Already waiting for PONG
    if (lastPingSeq_)
    {
        this->fail("Ping Timeout");
        return;
    }

    lastPingTime_ = clock_type::now();
    lastPingSeq_ = rand_int<std::uint32_t>();

    protocol::TMPing message;
    message.set_type(protocol::TMPing::ptPING);
    message.set_seq(*lastPingSeq_);

    this->send(std::make_shared<Message>(message, protocol::mtPING));

    setTimer();
}

//------------------------------------------------------------------------------
//
// ProtocolHandler
//
//------------------------------------------------------------------------------

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessageUnknown(std::uint16_t type)
{
    // TODO
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessageBegin(
    std::uint16_t type,
    std::shared_ptr<::google::protobuf::Message> const& m,
    std::size_t size,
    std::size_t uncompressed_size,
    bool isCompressed)
{
    load_event_ = this->app().getJobQueue().makeLoadEvent(
        jtPEER, protocolMessageName(type));
    fee_ = Resource::feeLightPeer;
    overlay_.p2p().reportTraffic(
        TrafficCount::categorize(*m, type, true), true, static_cast<int>(size));
    JLOG(this->journal().trace())
        << "onMessageBegin: " << type << " " << size << " " << uncompressed_size
        << " " << isCompressed;
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessageEnd(
    std::uint16_t,
    std::shared_ptr<::google::protobuf::Message> const&)
{
    load_event_.reset();
    charge(fee_);
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMManifests> const& m)
{
    auto const s = m->list_size();

    if (s == 0)
    {
        fee_ = Resource::feeUnwantedData;
        return;
    }

    if (s > 100)
        fee_ = Resource::feeMediumBurdenPeer;

    // VFALCO What's the right job type?
    auto that = shared();
    this->app().getJobQueue().addJob(
        jtVALIDATION_ut, "receiveManifests", [this, that, m](Job&) {
            overlay_.onManifests(m, that);
        });
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(std::shared_ptr<protocol::TMPing> const& m)
{
    if (m->type() == protocol::TMPing::ptPING)
    {
        // We have received a ping request, reply with a pong
        fee_ = Resource::feeMediumBurdenPeer;
        m->set_type(protocol::TMPing::ptPONG);
        this->send(std::make_shared<Message>(*m, protocol::mtPING));
        return;
    }

    if (m->type() == protocol::TMPing::ptPONG && m->has_seq())
    {
        // Only reset the ping sequence if we actually received a
        // PONG with the correct cookie. That way, any peers which
        // respond with incorrect cookies will eventually time out.
        if (m->seq() == lastPingSeq_)
        {
            lastPingSeq_.reset();

            // Update latency estimate
            auto const rtt = std::chrono::round<std::chrono::milliseconds>(
                clock_type::now() - lastPingTime_);

            std::lock_guard sl(this->recentLock());

            if (latency_)
                latency_ = (*latency_ * 7 + rtt) / 8;
            else
                latency_ = rtt;
        }

        return;
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(std::shared_ptr<protocol::TMCluster> const& m)
{
    // VFALCO NOTE I think we should drop the peer immediately
    if (!this->cluster())
    {
        fee_ = Resource::feeUnwantedData;
        return;
    }

    for (int i = 0; i < m->clusternodes().size(); ++i)
    {
        protocol::TMClusterNode const& node = m->clusternodes(i);

        std::string name;
        if (node.has_nodename())
            name = node.nodename();

        auto const publicKey =
            parseBase58<PublicKey>(TokenType::NodePublic, node.publickey());

        // NIKB NOTE We should drop the peer immediately if
        // they send us a public key we can't parse
        if (publicKey)
        {
            auto const reportTime =
                NetClock::time_point{NetClock::duration{node.reporttime()}};

            this->app().cluster().update(
                *publicKey, name, node.nodeload(), reportTime);
        }
    }

    int loadSources = m->loadsources().size();
    if (loadSources != 0)
    {
        Resource::Gossip gossip;
        gossip.items.reserve(loadSources);
        for (int i = 0; i < m->loadsources().size(); ++i)
        {
            protocol::TMLoadSource const& node = m->loadsources(i);
            Resource::Gossip::Item item;
            item.address = beast::IP::Endpoint::from_string(node.name());
            item.balance = node.cost();
            if (item.address != beast::IP::Endpoint())
                gossip.items.push_back(item);
        }
        overlay_.p2p().resourceManager().importConsumers(this->name(), gossip);
    }

    // Calculate the cluster fee:
    auto const thresh = this->app().timeKeeper().now() - 90s;
    std::uint32_t clusterFee = 0;

    std::vector<std::uint32_t> fees;
    fees.reserve(this->app().cluster().size());

    this->app().cluster().for_each([&fees, thresh](ClusterNode const& status) {
        if (status.getReportTime() >= thresh)
            fees.push_back(status.getLoadFee());
    });

    if (!fees.empty())
    {
        auto const index = fees.size() / 2;
        std::nth_element(fees.begin(), fees.begin() + index, fees.end());
        clusterFee = fees[index];
    }

    this->app().getFeeTrack().setClusterFee(clusterFee);
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMGetShardInfo> const& m)
{
    // DEPRECATED
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMShardInfo> const& m)
{
    // DEPRECATED
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMGetPeerShardInfo> const& m)
{
    auto badData = [&](std::string msg) {
        fee_ = Resource::feeBadData;
        JLOG(p_journal_.warn()) << msg;
    };

    if (m->hops() > csHopLimit)
        return badData("Invalid hops: " + std::to_string(m->hops()));
    if (m->peerchain_size() > csHopLimit)
        return badData("Invalid peer chain");

    // Reply with shard info we may have
    if (auto shardStore = this->app().getShardStore())
    {
        fee_ = Resource::feeLightPeer;
        auto shards{shardStore->getCompleteShards()};
        if (!shards.empty())
        {
            protocol::TMPeerShardInfo reply;
            reply.set_shardindexes(shards);

            if (m->has_lastlink())
                reply.set_lastlink(true);

            if (m->peerchain_size() > 0)
            {
                for (int i = 0; i < m->peerchain_size(); ++i)
                {
                    if (!publicKeyType(makeSlice(m->peerchain(i).nodepubkey())))
                        return badData("Invalid peer chain public key");
                }

                *reply.mutable_peerchain() = m->peerchain();
            }

            this->send(
                std::make_shared<Message>(reply, protocol::mtPEER_SHARD_INFO));

            JLOG(p_journal_.trace()) << "Sent shard indexes " << shards;
        }
    }

    // Relay request to peers
    if (m->hops() > 0)
    {
        fee_ = Resource::feeMediumBurdenPeer;

        m->set_hops(m->hops() - 1);
        if (m->hops() == 0)
            m->set_lastlink(true);

        m->add_peerchain()->set_nodepubkey(
            this->getNodePublic().data(), this->getNodePublic().size());

        overlay_.foreach(send_if_not(
            std::make_shared<Message>(*m, protocol::mtGET_PEER_SHARD_INFO),
            match_peer(this)));
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMPeerShardInfo> const& m)
{
    auto badData = [&](std::string msg) {
        fee_ = Resource::feeBadData;
        JLOG(p_journal_.warn()) << msg;
    };

    if (m->shardindexes().empty())
        return badData("Missing shard indexes");
    if (m->peerchain_size() > csHopLimit)
        return badData("Invalid peer chain");
    if (m->has_nodepubkey() && !publicKeyType(makeSlice(m->nodepubkey())))
        return badData("Invalid public key");

    // Check if the message should be forwarded to another peer
    if (m->peerchain_size() > 0)
    {
        // Get the Public key of the last link in the peer chain
        auto const s{
            makeSlice(m->peerchain(m->peerchain_size() - 1).nodepubkey())};
        if (!publicKeyType(s))
            return badData("Invalid pubKey");
        PublicKey peerPubKey(s);

        if (auto peer = overlay_.findPeerByPublicKey(peerPubKey))
        {
            if (!m->has_nodepubkey())
                m->set_nodepubkey(
                    this->getNodePublic().data(), this->getNodePublic().size());

            if (!m->has_endpoint())
            {
                // Check if peer will share IP publicly
                if (crawl())
                    m->set_endpoint(
                        this->getRemoteAddress().address().to_string());
                else
                    m->set_endpoint("0");
            }

            m->mutable_peerchain()->RemoveLast();
            peer->p2p().send(
                std::make_shared<Message>(*m, protocol::mtPEER_SHARD_INFO));

            JLOG(p_journal_.trace())
                << "Relayed TMPeerShardInfo to peer with IP "
                << this->getRemoteAddress().address().to_string();
        }
        else
        {
            // Peer is no longer available so the relay ends
            fee_ = Resource::feeUnwantedData;
            JLOG(p_journal_.info()) << "Unable to route shard info";
        }
        return;
    }

    // Parse the shard indexes received in the shard info
    RangeSet<std::uint32_t> shardIndexes;
    {
        if (!from_string(shardIndexes, m->shardindexes()))
            return badData("Invalid shard indexes");

        std::uint32_t earliestShard;
        std::optional<std::uint32_t> latestShard;
        {
            auto const curLedgerSeq{
                this->app().getLedgerMaster().getCurrentLedgerIndex()};
            if (auto shardStore = this->app().getShardStore())
            {
                earliestShard = shardStore->earliestShardIndex();
                if (curLedgerSeq >= shardStore->earliestLedgerSeq())
                    latestShard = shardStore->seqToShardIndex(curLedgerSeq);
            }
            else
            {
                auto const earliestLedgerSeq{
                    this->app().getNodeStore().earliestLedgerSeq()};
                earliestShard = NodeStore::seqToShardIndex(earliestLedgerSeq);
                if (curLedgerSeq >= earliestLedgerSeq)
                    latestShard = NodeStore::seqToShardIndex(curLedgerSeq);
            }
        }

        if (boost::icl::first(shardIndexes) < earliestShard ||
            (latestShard && boost::icl::last(shardIndexes) > latestShard))
        {
            return badData("Invalid shard indexes");
        }
    }

    // Get the IP of the node reporting the shard info
    beast::IP::Endpoint endpoint;
    if (m->has_endpoint())
    {
        if (m->endpoint() != "0")
        {
            auto result =
                beast::IP::Endpoint::from_string_checked(m->endpoint());
            if (!result)
                return badData("Invalid incoming endpoint: " + m->endpoint());
            endpoint = std::move(*result);
        }
    }
    else if (crawl())  // Check if peer will share IP publicly
    {
        endpoint = this->getRemoteAddress();
    }

    // Get the Public key of the node reporting the shard info
    PublicKey publicKey;
    if (m->has_nodepubkey())
        publicKey = PublicKey(makeSlice(m->nodepubkey()));
    else
        publicKey = this->getNodePublic();

    {
        std::lock_guard l{shardInfoMutex_};
        auto it{shardInfo_.find(publicKey)};
        if (it != shardInfo_.end())
        {
            // Update the IP address for the node
            it->second.endpoint = std::move(endpoint);

            // Join the shard index range set
            it->second.shardIndexes += shardIndexes;
        }
        else
        {
            // Add a new node
            ShardInfo shardInfo;
            shardInfo.endpoint = std::move(endpoint);
            shardInfo.shardIndexes = std::move(shardIndexes);
            shardInfo_.emplace(publicKey, std::move(shardInfo));
        }
    }

    JLOG(p_journal_.trace())
        << "Consumed TMPeerShardInfo originating from public key "
        << toBase58(TokenType::NodePublic, publicKey) << " shard indexes "
        << m->shardindexes();

    if (m->has_lastlink())
        overlay_.lastLink(this->id());
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMEndpoints> const& m)
{
    // Don't allow endpoints from peers that are not known tracking or are
    // not using a version of the message that we support:
    if (tracking_.load() != Tracking::converged || m->version() != 2)
        return;

    std::vector<PeerFinder::Endpoint> endpoints;
    endpoints.reserve(m->endpoints_v2().size());

    for (auto const& tm : m->endpoints_v2())
    {
        auto result = beast::IP::Endpoint::from_string_checked(tm.endpoint());
        if (!result)
        {
            JLOG(p_journal_.error()) << "failed to parse incoming endpoint: {"
                                     << tm.endpoint() << "}";
            continue;
        }

        // If hops == 0, this Endpoint describes the peer we are connected
        // to -- in that case, we take the remote address seen on the
        // socket and store that in the IP::Endpoint. If this is the first
        // time, then we'll verify that their listener can receive incoming
        // by performing a connectivity test.  if hops > 0, then we just
        // take the address/port we were given

        endpoints.emplace_back(
            tm.hops() > 0 ? *result
                          : this->getRemoteAddress().at_port(result->port()),
            tm.hops());
    }

    if (!endpoints.empty())
        overlay_.p2p().peerFinder().on_endpoints(this->slot(), endpoints);
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMTransaction> const& m)
{
    if (tracking_.load() == Tracking::diverged)
        return;

    if (this->app().getOPs().isNeedNetworkLedger())
    {
        // If we've never been in synch, there's nothing we can do
        // with a transaction
        JLOG(p_journal_.debug()) << "Ignoring incoming transaction: "
                                 << "Need network ledger";
        return;
    }

    SerialIter sit(makeSlice(m->rawtransaction()));

    try
    {
        auto stx = std::make_shared<STTx const>(sit);
        uint256 txID = stx->getTransactionID();

        int flags;
        constexpr std::chrono::seconds tx_interval = 10s;

        if (!this->app().getHashRouter().shouldProcess(
                txID, this->id(), flags, tx_interval))
        {
            // we have seen this transaction recently
            if (flags & SF_BAD)
            {
                fee_ = Resource::feeInvalidSignature;
                JLOG(p_journal_.debug()) << "Ignoring known bad tx " << txID;
            }

            return;
        }

        JLOG(p_journal_.debug()) << "Got tx " << txID;

        bool checkSignature = true;
        if (this->cluster())
        {
            if (!m->has_deferred() || !m->deferred())
            {
                // Skip local checks if a server we trust
                // put the transaction in its open ledger
                flags |= SF_TRUSTED;
            }

            if (this->app().getValidationPublicKey().empty())
            {
                // For now, be paranoid and have each validator
                // check each transaction, regardless of source
                checkSignature = false;
            }
        }

        if (this->app().getJobQueue().getJobCount(jtTRANSACTION) >
            this->app().config().MAX_TRANSACTIONS)
        {
            overlay_.incJqTransOverflow();
            JLOG(p_journal_.info()) << "Transaction queue is full";
        }
        else if (this->app().getLedgerMaster().getValidatedLedgerAge() > 4min)
        {
            JLOG(p_journal_.trace())
                << "No new transactions until synchronized";
        }
        else
        {
            this->app().getJobQueue().addJob(
                jtTRANSACTION,
                "recvTransaction->checkTransaction",
                [weak = std::weak_ptr<PeerImp>(shared()),
                 flags,
                 checkSignature,
                 stx](Job&) {
                    if (auto peer = weak.lock())
                        peer->checkTransaction(flags, checkSignature, stx);
                });
        }
    }
    catch (std::exception const&)
    {
        JLOG(p_journal_.warn())
            << "Transaction invalid: " << strHex(m->rawtransaction());
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMGetLedger> const& m)
{
    fee_ = Resource::feeMediumBurdenPeer;
    std::weak_ptr<PeerImp> weak = shared();
    this->app().getJobQueue().addJob(
        jtLEDGER_REQ, "recvGetLedger", [weak, m](Job&) {
            if (auto peer = weak.lock())
                peer->getLedger(m);
        });
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMProofPathRequest> const& m)
{
    JLOG(p_journal_.trace()) << "onMessage, TMProofPathRequest";
    if (!ledgerReplayEnabled_)
    {
        charge(Resource::feeInvalidRequest);
        return;
    }

    fee_ = Resource::feeMediumBurdenPeer;
    std::weak_ptr<PeerImp> weak = shared();
    this->app().getJobQueue().addJob(
        jtREPLAY_REQ, "recvProofPathRequest", [weak, m](Job&) {
            if (auto peer = weak.lock())
            {
                auto reply =
                    peer->ledgerReplayMsgHandler_.processProofPathRequest(m);
                if (reply.has_error())
                {
                    if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                        peer->charge(Resource::feeInvalidRequest);
                    else
                        peer->charge(Resource::feeRequestNoReply);
                }
                else
                {
                    peer->p2p().send(std::make_shared<Message>(
                        reply, protocol::mtPROOF_PATH_RESPONSE));
                }
            }
        });
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMProofPathResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        charge(Resource::feeInvalidRequest);
        return;
    }

    if (!ledgerReplayMsgHandler_.processProofPathResponse(m))
    {
        charge(Resource::feeBadData);
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMReplayDeltaRequest> const& m)
{
    JLOG(p_journal_.trace()) << "onMessage, TMReplayDeltaRequest";
    if (!ledgerReplayEnabled_)
    {
        charge(Resource::feeInvalidRequest);
        return;
    }

    fee_ = Resource::feeMediumBurdenPeer;
    std::weak_ptr<PeerImp> weak = shared();
    this->app().getJobQueue().addJob(
        jtREPLAY_REQ, "recvReplayDeltaRequest", [weak, m](Job&) {
            if (auto peer = weak.lock())
            {
                auto reply =
                    peer->ledgerReplayMsgHandler_.processReplayDeltaRequest(m);
                if (reply.has_error())
                {
                    if (reply.error() == protocol::TMReplyError::reBAD_REQUEST)
                        peer->charge(Resource::feeInvalidRequest);
                    else
                        peer->charge(Resource::feeRequestNoReply);
                }
                else
                {
                    peer->p2p().send(std::make_shared<Message>(
                        reply, protocol::mtREPLAY_DELTA_RESPONSE));
                }
            }
        });
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMReplayDeltaResponse> const& m)
{
    if (!ledgerReplayEnabled_)
    {
        charge(Resource::feeInvalidRequest);
        return;
    }

    if (!ledgerReplayMsgHandler_.processReplayDeltaResponse(m))
    {
        charge(Resource::feeBadData);
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMLedgerData> const& m)
{
    protocol::TMLedgerData& packet = *m;

    if (m->nodes().size() <= 0)
    {
        JLOG(p_journal_.warn()) << "Ledger/TXset data with no nodes";
        return;
    }

    if (m->has_requestcookie())
    {
        std::shared_ptr<Peer> target =
            overlay_.findPeerByShortID(m->requestcookie());
        if (target)
        {
            m->clear_requestcookie();
            target->p2p().send(
                std::make_shared<Message>(packet, protocol::mtLEDGER_DATA));
        }
        else
        {
            JLOG(p_journal_.info()) << "Unable to route TX/ledger data reply";
            fee_ = Resource::feeUnwantedData;
        }
        return;
    }

    if (!stringIsUint256Sized(m->ledgerhash()))
    {
        JLOG(p_journal_.warn()) << "TX candidate reply with invalid hash size";
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    uint256 const hash{m->ledgerhash()};

    if (m->type() == protocol::liTS_CANDIDATE)
    {
        // got data for a candidate transaction set
        std::weak_ptr<PeerImp> weak = shared();
        auto& app = this->app();
        this->app().getJobQueue().addJob(
            jtTXN_DATA, "recvPeerData", [weak, hash, m, &app](Job&) {
                if (auto peer = weak.lock())
                    app.getInboundTransactions().gotData(hash, peer, m);
            });
        return;
    }

    if (!this->app().getInboundLedgers().gotLedgerData(hash, shared(), m))
    {
        JLOG(p_journal_.trace()) << "Got data for unwanted ledger";
        fee_ = Resource::feeUnwantedData;
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMProposeSet> const& m)
{
    protocol::TMProposeSet& set = *m;

    auto const sig = makeSlice(set.signature());

    // Preliminary check for the validity of the signature: A DER encoded
    // signature can't be longer than 72 bytes.
    if ((boost::algorithm::clamp(sig.size(), 64, 72) != sig.size()) ||
        (publicKeyType(makeSlice(set.nodepubkey())) != KeyType::secp256k1))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_ = Resource::feeInvalidSignature;
        return;
    }

    if (!stringIsUint256Sized(set.currenttxhash()) ||
        !stringIsUint256Sized(set.previousledger()))
    {
        JLOG(p_journal_.warn()) << "Proposal: malformed";
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    uint256 const proposeHash{set.currenttxhash()};
    uint256 const prevLedger{set.previousledger()};

    PublicKey const publicKey{makeSlice(set.nodepubkey())};
    NetClock::time_point const closeTime{NetClock::duration{set.closetime()}};

    uint256 const suppression = proposalUniqueId(
        proposeHash,
        prevLedger,
        set.proposeseq(),
        closeTime,
        publicKey.slice(),
        sig);

    if (auto [added, relayed] =
            this->app().getHashRouter().addSuppressionPeerWithStatus(
                suppression, this->id());
        !added)
    {
        // Count unique messages (Slots has it's own 'HashRouter'), which a peer
        // receives within IDLED seconds since the message has been relayed.
        if (reduceRelayReady() && relayed &&
            (stopwatch().now() - *relayed) < reduce_relay::IDLED)
            overlay_.updateSlotAndSquelch(
                suppression, publicKey, this->id(), protocol::mtPROPOSE_LEDGER);
        JLOG(p_journal_.trace()) << "Proposal: duplicate";
        return;
    }

    auto const isTrusted = this->app().validators().trusted(publicKey);

    if (!isTrusted)
    {
        if (tracking_.load() == Tracking::diverged)
        {
            JLOG(p_journal_.debug())
                << "Proposal: Dropping untrusted (peer divergence)";
            return;
        }

        if (!this->cluster() && this->app().getFeeTrack().isLoadedLocal())
        {
            JLOG(p_journal_.debug()) << "Proposal: Dropping untrusted (load)";
            return;
        }
    }

    JLOG(p_journal_.trace())
        << "Proposal: " << (isTrusted ? "trusted" : "untrusted");

    auto proposal = RCLCxPeerPos(
        publicKey,
        sig,
        suppression,
        RCLCxPeerPos::Proposal{
            prevLedger,
            set.proposeseq(),
            proposeHash,
            closeTime,
            this->app().timeKeeper().closeTime(),
            calcNodeID(
                this->app().validatorManifests().getMasterKey(publicKey))});

    std::weak_ptr<PeerImp> weak = shared();
    this->app().getJobQueue().addJob(
        isTrusted ? jtPROPOSAL_t : jtPROPOSAL_ut,
        "recvPropose->checkPropose",
        [weak, m, proposal](Job& job) {
            if (auto peer = weak.lock())
                peer->checkPropose(job, m, proposal);
        });
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMStatusChange> const& m)
{
    JLOG(p_journal_.trace()) << "Status: Change";

    if (!m->has_networktime())
        m->set_networktime(
            this->app().timeKeeper().now().time_since_epoch().count());

    {
        std::lock_guard sl(this->recentLock());
        if (!last_status_.has_newstatus() || m->has_newstatus())
            last_status_ = *m;
        else
        {
            // preserve old status
            protocol::NodeStatus status = last_status_.newstatus();
            last_status_ = *m;
            m->set_newstatus(status);
        }
    }

    if (m->newevent() == protocol::neLOST_SYNC)
    {
        bool outOfSync{false};
        {
            // Operations on closedLedgerHash_ and previousLedgerHash_ must be
            // guarded by recentLock_.
            std::lock_guard sl(this->recentLock());
            if (!closedLedgerHash_.isZero())
            {
                outOfSync = true;
                closedLedgerHash_.zero();
            }
            previousLedgerHash_.zero();
        }
        if (outOfSync)
        {
            JLOG(p_journal_.debug()) << "Status: Out of sync";
        }
        return;
    }

    {
        uint256 closedLedgerHash{};
        bool const peerChangedLedgers{
            m->has_ledgerhash() && stringIsUint256Sized(m->ledgerhash())};

        {
            // Operations on closedLedgerHash_ and previousLedgerHash_ must be
            // guarded by recentLock_.
            std::lock_guard sl(this->recentLock());
            if (peerChangedLedgers)
            {
                closedLedgerHash_ = m->ledgerhash();
                closedLedgerHash = closedLedgerHash_;
                addLedger(closedLedgerHash, sl);
            }
            else
            {
                closedLedgerHash_.zero();
            }

            if (m->has_ledgerhashprevious() &&
                stringIsUint256Sized(m->ledgerhashprevious()))
            {
                previousLedgerHash_ = m->ledgerhashprevious();
                addLedger(previousLedgerHash_, sl);
            }
            else
            {
                previousLedgerHash_.zero();
            }
        }
        if (peerChangedLedgers)
        {
            JLOG(p_journal_.debug()) << "LCL is " << closedLedgerHash;
        }
        else
        {
            JLOG(p_journal_.debug()) << "Status: No ledger";
        }
    }

    if (m->has_firstseq() && m->has_lastseq())
    {
        std::lock_guard sl(this->recentLock());

        minLedger_ = m->firstseq();
        maxLedger_ = m->lastseq();

        if ((maxLedger_ < minLedger_) || (minLedger_ == 0) || (maxLedger_ == 0))
            minLedger_ = maxLedger_ = 0;
    }

    if (m->has_ledgerseq() &&
        this->app().getLedgerMaster().getValidatedLedgerAge() < 2min)
    {
        checkTracking(
            m->ledgerseq(),
            this->app().getLedgerMaster().getValidLedgerIndex());
    }

    this->app().getOPs().pubPeerStatus([=]() -> Json::Value {
        Json::Value j = Json::objectValue;

        if (m->has_newstatus())
        {
            switch (m->newstatus())
            {
                case protocol::nsCONNECTING:
                    j[jss::status] = "CONNECTING";
                    break;
                case protocol::nsCONNECTED:
                    j[jss::status] = "CONNECTED";
                    break;
                case protocol::nsMONITORING:
                    j[jss::status] = "MONITORING";
                    break;
                case protocol::nsVALIDATING:
                    j[jss::status] = "VALIDATING";
                    break;
                case protocol::nsSHUTTING:
                    j[jss::status] = "SHUTTING";
                    break;
            }
        }

        if (m->has_newevent())
        {
            switch (m->newevent())
            {
                case protocol::neCLOSING_LEDGER:
                    j[jss::action] = "CLOSING_LEDGER";
                    break;
                case protocol::neACCEPTED_LEDGER:
                    j[jss::action] = "ACCEPTED_LEDGER";
                    break;
                case protocol::neSWITCHED_LEDGER:
                    j[jss::action] = "SWITCHED_LEDGER";
                    break;
                case protocol::neLOST_SYNC:
                    j[jss::action] = "LOST_SYNC";
                    break;
            }
        }

        if (m->has_ledgerseq())
        {
            j[jss::ledger_index] = m->ledgerseq();
        }

        if (m->has_ledgerhash())
        {
            uint256 closedLedgerHash{};
            {
                std::lock_guard sl(this->recentLock());
                closedLedgerHash = closedLedgerHash_;
            }
            j[jss::ledger_hash] = to_string(closedLedgerHash);
        }

        if (m->has_networktime())
        {
            j[jss::date] = Json::UInt(m->networktime());
        }

        if (m->has_firstseq() && m->has_lastseq())
        {
            j[jss::ledger_index_min] = Json::UInt(m->firstseq());
            j[jss::ledger_index_max] = Json::UInt(m->lastseq());
        }

        return j;
    });
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::checkTracking(std::uint32_t validationSeq)
{
    std::uint32_t serverSeq;
    {
        // Extract the sequence number of the highest
        // ledger this peer has
        std::lock_guard sl(this->recentLock());

        serverSeq = maxLedger_;
    }
    if (serverSeq != 0)
    {
        // Compare the peer's ledger sequence to the
        // sequence of a recently-validated ledger
        checkTracking(serverSeq, validationSeq);
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::checkTracking(std::uint32_t seq1, std::uint32_t seq2)
{
    int diff = std::max(seq1, seq2) - std::min(seq1, seq2);

    if (diff < Tuning::convergedLedgerLimit)
    {
        // The peer's ledger sequence is close to the validation's
        tracking_ = Tracking::converged;
    }

    if ((diff > Tuning::divergedLedgerLimit) &&
        (tracking_.load() != Tracking::diverged))
    {
        // The peer's ledger sequence is way off the validation's
        std::lock_guard sl(this->recentLock());

        tracking_ = Tracking::diverged;
        trackingTime_ = clock_type::now();
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMHaveTransactionSet> const& m)
{
    if (!stringIsUint256Sized(m->hash()))
    {
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    uint256 const hash{m->hash()};

    if (m->status() == protocol::tsHAVE)
    {
        std::lock_guard sl(this->recentLock());

        if (std::find(recentTxSets_.begin(), recentTxSets_.end(), hash) !=
            recentTxSets_.end())
        {
            fee_ = Resource::feeUnwantedData;
            return;
        }

        recentTxSets_.push_back(hash);
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onValidatorListMessage(
    std::string const& messageType,
    std::string const& manifest,
    std::uint32_t version,
    std::vector<ValidatorBlobInfo> const& blobs)
{
    // If there are no blobs, the message is malformed (possibly because of
    // ValidatorList class rules), so charge accordingly and skip processing.
    if (blobs.empty())
    {
        JLOG(p_journal_.warn()) << "Ignored malformed " << messageType
                                << " from peer " << this->getRemoteAddress();
        // This shouldn't ever happen with a well-behaved peer
        fee_ = Resource::feeHighBurdenPeer;
        return;
    }

    auto const hash = sha512Half(manifest, blobs, version);

    JLOG(p_journal_.debug())
        << "Received " << messageType << " from "
        << this->getRemoteAddress().to_string() << " (" << this->id() << ")";

    if (!this->app().getHashRouter().addSuppressionPeer(hash, this->id()))
    {
        JLOG(p_journal_.debug())
            << messageType << ": received duplicate " << messageType;
        // Charging this fee here won't hurt the peer in the normal
        // course of operation (ie. refresh every 5 minutes), but
        // will add up if the peer is misbehaving.
        fee_ = Resource::feeUnwantedData;
        return;
    }

    auto const applyResult = this->app().validators().applyListsAndBroadcast(
        manifest,
        version,
        blobs,
        this->getRemoteAddress().to_string(),
        hash,
        this->app().overlay(),
        this->app().getHashRouter(),
        this->app().getOPs());

    JLOG(p_journal_.debug())
        << "Processed " << messageType << " version " << version << " from "
        << (applyResult.publisherKey ? strHex(*applyResult.publisherKey)
                                     : "unknown or invalid publisher")
        << " from " << this->getRemoteAddress().to_string() << " ("
        << this->id() << ") with best result "
        << to_string(applyResult.bestDisposition());

    // Act based on the best result
    switch (applyResult.bestDisposition())
    {
        // New list
        case ListDisposition::accepted:
            // Newest list is expired, and that needs to be broadcast, too
        case ListDisposition::expired:
            // Future list
        case ListDisposition::pending: {
            std::lock_guard<std::mutex> sl(this->recentLock());

            assert(applyResult.publisherKey);
            auto const& pubKey = *applyResult.publisherKey;
#ifndef NDEBUG
            if (auto const iter = publisherListSequences_.find(pubKey);
                iter != publisherListSequences_.end())
            {
                assert(iter->second < applyResult.sequence);
            }
#endif
            publisherListSequences_[pubKey] = applyResult.sequence;
        }
        break;
        case ListDisposition::same_sequence:
        case ListDisposition::known_sequence:
#ifndef NDEBUG
        {
            std::lock_guard<std::mutex> sl(this->recentLock());
            assert(applyResult.sequence && applyResult.publisherKey);
            assert(
                publisherListSequences_[*applyResult.publisherKey] <=
                applyResult.sequence);
        }
#endif  // !NDEBUG

        break;
        case ListDisposition::stale:
        case ListDisposition::untrusted:
        case ListDisposition::invalid:
        case ListDisposition::unsupported_version:
            break;
        default:
            assert(false);
    }

    // Charge based on the worst result
    switch (applyResult.worstDisposition())
    {
        case ListDisposition::accepted:
        case ListDisposition::expired:
        case ListDisposition::pending:
            // No charges for good data
            break;
        case ListDisposition::same_sequence:
        case ListDisposition::known_sequence:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_ = Resource::feeUnwantedData;
            break;
        case ListDisposition::stale:
            // There are very few good reasons for a peer to send an
            // old list, particularly more than once.
            fee_ = Resource::feeBadData;
            break;
        case ListDisposition::untrusted:
            // Charging this fee here won't hurt the peer in the normal
            // course of operation (ie. refresh every 5 minutes), but
            // will add up if the peer is misbehaving.
            fee_ = Resource::feeUnwantedData;
            break;
        case ListDisposition::invalid:
            // This shouldn't ever happen with a well-behaved peer
            fee_ = Resource::feeInvalidSignature;
            break;
        case ListDisposition::unsupported_version:
            // During a version transition, this may be legitimate.
            // If it happens frequently, that's probably bad.
            fee_ = Resource::feeBadData;
            break;
        default:
            assert(false);
    }

    // Log based on all the results.
    for (auto const [disp, count] : applyResult.dispositions)
    {
        switch (disp)
        {
            // New list
            case ListDisposition::accepted:
                JLOG(p_journal_.debug())
                    << "Applied " << count << " new " << messageType
                    << "(s) from peer " << this->getRemoteAddress();
                break;
                // Newest list is expired, and that needs to be broadcast, too
            case ListDisposition::expired:
                JLOG(p_journal_.debug())
                    << "Applied " << count << " expired " << messageType
                    << "(s) from peer " << this->getRemoteAddress();
                break;
                // Future list
            case ListDisposition::pending:
                JLOG(p_journal_.debug())
                    << "Processed " << count << " future " << messageType
                    << "(s) from peer " << this->getRemoteAddress();
                break;
            case ListDisposition::same_sequence:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << " " << messageType
                    << "(s) with current sequence from peer "
                    << this->getRemoteAddress();
                break;
            case ListDisposition::known_sequence:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << " " << messageType
                    << "(s) with future sequence from peer "
                    << this->getRemoteAddress();
                break;
            case ListDisposition::stale:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << "stale " << messageType
                    << "(s) from peer " << this->getRemoteAddress();
                break;
            case ListDisposition::untrusted:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << " untrusted " << messageType
                    << "(s) from peer " << this->getRemoteAddress();
                break;
            case ListDisposition::unsupported_version:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << "unsupported version "
                    << messageType << "(s) from peer "
                    << this->getRemoteAddress();
                break;
            case ListDisposition::invalid:
                JLOG(p_journal_.warn())
                    << "Ignored " << count << "invalid " << messageType
                    << "(s) from peer " << this->getRemoteAddress();
                break;
            default:
                assert(false);
        }
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMValidatorList> const& m)
{
    try
    {
        if (!supportsFeature(ProtocolFeature::ValidatorListPropagation))
        {
            JLOG(p_journal_.debug())
                << "ValidatorList: received validator list from peer using "
                << "protocol version " << to_string(this->protocol())
                << " which shouldn't support this feature.";
            fee_ = Resource::feeUnwantedData;
            return;
        }
        onValidatorListMessage(
            "ValidatorList",
            m->manifest(),
            m->version(),
            ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn()) << "ValidatorList: Exception, " << e.what()
                                << " from peer " << this->getRemoteAddress();
        fee_ = Resource::feeBadData;
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMValidatorListCollection> const& m)
{
    try
    {
        if (!supportsFeature(ProtocolFeature::ValidatorList2Propagation))
        {
            JLOG(p_journal_.debug())
                << "ValidatorListCollection: received validator list from peer "
                << "using protocol version " << to_string(this->protocol())
                << " which shouldn't support this feature.";
            fee_ = Resource::feeUnwantedData;
            return;
        }
        else if (m->version() < 2)
        {
            JLOG(p_journal_.debug())
                << "ValidatorListCollection: received invalid validator list "
                   "version "
                << m->version() << " from peer using protocol version "
                << to_string(this->protocol());
            fee_ = Resource::feeBadData;
            return;
        }
        onValidatorListMessage(
            "ValidatorListCollection",
            m->manifest(),
            m->version(),
            ValidatorList::parseBlobs(*m));
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn())
            << "ValidatorListCollection: Exception, " << e.what()
            << " from peer " << this->getRemoteAddress();
        fee_ = Resource::feeBadData;
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMValidation> const& m)
{
    if (m->validation().size() < 50)
    {
        JLOG(p_journal_.warn()) << "Validation: Too small";
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    try
    {
        auto const closeTime = this->app().timeKeeper().closeTime();

        std::shared_ptr<STValidation> val;
        {
            SerialIter sit(makeSlice(m->validation()));
            val = std::make_shared<STValidation>(
                std::ref(sit),
                [this](PublicKey const& pk) {
                    return calcNodeID(
                        this->app().validatorManifests().getMasterKey(pk));
                },
                false);
            val->setSeen(closeTime);
        }

        if (!isCurrent(
                this->app().getValidations().parms(),
                this->app().timeKeeper().closeTime(),
                val->getSignTime(),
                val->getSeenTime()))
        {
            JLOG(p_journal_.trace()) << "Validation: Not current";
            fee_ = Resource::feeUnwantedData;
            return;
        }

        auto key = sha512Half(makeSlice(m->validation()));
        if (auto [added, relayed] =
                this->app().getHashRouter().addSuppressionPeerWithStatus(
                    key, this->id());
            !added)
        {
            // Count unique messages (Slots has it's own 'HashRouter'), which a
            // peer receives within IDLED seconds since the message has been
            // relayed. Wait WAIT_ON_BOOTUP time to let the server establish
            // connections to peers.
            if (reduceRelayReady() && relayed &&
                (stopwatch().now() - *relayed) < reduce_relay::IDLED)
                overlay_.updateSlotAndSquelch(
                    key,
                    val->getSignerPublic(),
                    this->id(),
                    protocol::mtVALIDATION);
            JLOG(p_journal_.trace()) << "Validation: duplicate";
            return;
        }

        auto const isTrusted =
            this->app().validators().trusted(val->getSignerPublic());

        if (!isTrusted && (tracking_.load() == Tracking::diverged))
        {
            JLOG(p_journal_.debug())
                << "Validation: dropping untrusted from diverged peer";
        }
        if (isTrusted || this->cluster() ||
            !this->app().getFeeTrack().isLoadedLocal())
        {
            std::weak_ptr<PeerImp> weak = shared();
            this->app().getJobQueue().addJob(
                isTrusted ? jtVALIDATION_t : jtVALIDATION_ut,
                "recvValidation->checkValidation",
                [weak, val, m](Job&) {
                    if (auto peer = weak.lock())
                        peer->checkValidation(val, m);
                });
        }
        else
        {
            JLOG(p_journal_.debug()) << "Validation: Dropping UNTRUSTED (load)";
        }
    }
    catch (std::exception const& e)
    {
        JLOG(p_journal_.warn())
            << "Exception processing validation: " << e.what();
        fee_ = Resource::feeInvalidRequest;
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMGetObjectByHash> const& m)
{
    protocol::TMGetObjectByHash& packet = *m;

    if (packet.query())
    {
        // this is a query
        if (this->queueSize() >= Tuning::dropSendQueue)
        {
            JLOG(p_journal_.debug()) << "GetObject: Large send queue";
            return;
        }

        if (packet.type() == protocol::TMGetObjectByHash::otFETCH_PACK)
        {
            doFetchPack(m);
            return;
        }

        fee_ = Resource::feeMediumBurdenPeer;

        protocol::TMGetObjectByHash reply;

        reply.set_query(false);

        if (packet.has_seq())
            reply.set_seq(packet.seq());

        reply.set_type(packet.type());

        if (packet.has_ledgerhash())
        {
            if (!stringIsUint256Sized(packet.ledgerhash()))
            {
                fee_ = Resource::feeInvalidRequest;
                return;
            }

            reply.set_ledgerhash(packet.ledgerhash());
        }

        // This is a very minimal implementation
        for (int i = 0; i < packet.objects_size(); ++i)
        {
            auto const& obj = packet.objects(i);
            if (obj.has_hash() && stringIsUint256Sized(obj.hash()))
            {
                uint256 const hash{obj.hash()};
                // VFALCO TODO Move this someplace more sensible so we dont
                //             need to inject the NodeStore interfaces.
                std::uint32_t seq{obj.has_ledgerseq() ? obj.ledgerseq() : 0};
                auto nodeObject{
                    this->app().getNodeStore().fetchNodeObject(hash, seq)};
                if (!nodeObject)
                {
                    if (auto shardStore = this->app().getShardStore())
                    {
                        if (seq >= shardStore->earliestLedgerSeq())
                            nodeObject = shardStore->fetchNodeObject(hash, seq);
                    }
                }
                if (nodeObject)
                {
                    protocol::TMIndexedObject& newObj = *reply.add_objects();
                    newObj.set_hash(hash.begin(), hash.size());
                    newObj.set_data(
                        &nodeObject->getData().front(),
                        nodeObject->getData().size());

                    if (obj.has_nodeid())
                        newObj.set_index(obj.nodeid());
                    if (obj.has_ledgerseq())
                        newObj.set_ledgerseq(obj.ledgerseq());

                    // VFALCO NOTE "seq" in the message is obsolete
                }
            }
        }

        JLOG(p_journal_.trace()) << "GetObj: " << reply.objects_size() << " of "
                                 << packet.objects_size();
        this->send(std::make_shared<Message>(reply, protocol::mtGET_OBJECTS));
    }
    else
    {
        // this is a reply
        std::uint32_t pLSeq = 0;
        bool pLDo = true;
        bool progress = false;

        for (int i = 0; i < packet.objects_size(); ++i)
        {
            const protocol::TMIndexedObject& obj = packet.objects(i);

            if (obj.has_hash() && stringIsUint256Sized(obj.hash()))
            {
                if (obj.has_ledgerseq())
                {
                    if (obj.ledgerseq() != pLSeq)
                    {
                        if (pLDo && (pLSeq != 0))
                        {
                            JLOG(p_journal_.debug())
                                << "GetObj: Full fetch pack for " << pLSeq;
                        }
                        pLSeq = obj.ledgerseq();
                        pLDo = !this->app().getLedgerMaster().haveLedger(pLSeq);

                        if (!pLDo)
                        {
                            JLOG(p_journal_.debug())
                                << "GetObj: Late fetch pack for " << pLSeq;
                        }
                        else
                            progress = true;
                    }
                }

                if (pLDo)
                {
                    uint256 const hash{obj.hash()};

                    this->app().getLedgerMaster().addFetchPack(
                        hash,
                        std::make_shared<Blob>(
                            obj.data().begin(), obj.data().end()));
                }
            }
        }

        if (pLDo && (pLSeq != 0))
        {
            JLOG(p_journal_.debug())
                << "GetObj: Partial fetch pack for " << pLSeq;
        }
        if (packet.type() == protocol::TMGetObjectByHash::otFETCH_PACK)
            this->app().getLedgerMaster().gotFetchPack(progress, pLSeq);
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(std::shared_ptr<protocol::TMSquelch> const& m)
{
    using on_message_fn = void (PeerImp<P2PeerImplmnt>::*)(
        std::shared_ptr<protocol::TMSquelch> const&);
    if (!this->strand().running_in_this_thread())
        return post(
            this->strand(),
            std::bind(
                (on_message_fn)&PeerImp<P2PeerImplmnt>::onMessage,
                shared(),
                m));

    if (!m->has_validatorpubkey())
    {
        charge(Resource::feeBadData);
        return;
    }
    auto validator = m->validatorpubkey();
    auto const slice{makeSlice(validator)};
    if (!publicKeyType(slice))
    {
        charge(Resource::feeBadData);
        return;
    }
    PublicKey key(slice);

    // Ignore the squelch for validator's own messages.
    if (key == this->app().getValidationPublicKey())
    {
        JLOG(p_journal_.debug())
            << "onMessage: TMSquelch discarding validator's squelch " << slice;
        return;
    }

    std::uint32_t duration =
        m->has_squelchduration() ? m->squelchduration() : 0;
    if (!m->squelch())
        squelch_.removeSquelch(key);
    else if (!squelch_.addSquelch(key, std::chrono::seconds{duration}))
        charge(Resource::feeBadData);

    JLOG(p_journal_.debug()) << "onMessage: TMSquelch " << slice << " "
                             << this->id() << " " << duration;
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onMessage(
    std::shared_ptr<protocol::TMProtocolStarted> const& m)
{
    sendOnProtocolStart(this->isInbound());
}

//--------------------------------------------------------------------------

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::addLedger(
    uint256 const& hash,
    std::lock_guard<std::mutex> const& lockedRecentLock)
{
    // lockedRecentLock is passed as a reminder that recentLock_ must be
    // locked by the caller.
    (void)lockedRecentLock;

    if (std::find(recentLedgers_.begin(), recentLedgers_.end(), hash) !=
        recentLedgers_.end())
        return;

    recentLedgers_.push_back(hash);
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::doFetchPack(
    const std::shared_ptr<protocol::TMGetObjectByHash>& packet)
{
    // VFALCO TODO Invert this dependency using an observer and shared state
    // object. Don't queue fetch pack jobs if we're under load or we already
    // have some queued.
    if (this->app().getFeeTrack().isLoadedLocal() ||
        (this->app().getLedgerMaster().getValidatedLedgerAge() > 40s) ||
        (this->app().getJobQueue().getJobCount(jtPACK) > 10))
    {
        JLOG(p_journal_.info()) << "Too busy to make fetch pack";
        return;
    }

    if (!stringIsUint256Sized(packet->ledgerhash()))
    {
        JLOG(p_journal_.warn()) << "FetchPack hash size malformed";
        fee_ = Resource::feeInvalidRequest;
        return;
    }

    fee_ = Resource::feeHighBurdenPeer;

    uint256 const hash{packet->ledgerhash()};

    std::weak_ptr<PeerImp> weak = shared();
    auto elapsed = UptimeClock::now();
    auto const pap = &this->app();
    this->app().getJobQueue().addJob(
        jtPACK, "MakeFetchPack", [pap, weak, packet, hash, elapsed](Job&) {
            pap->getLedgerMaster().makeFetchPack(weak, packet, hash, elapsed);
        });
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::checkTransaction(
    int flags,
    bool checkSignature,
    std::shared_ptr<STTx const> const& stx)
{
    // VFALCO TODO Rewrite to not use exceptions
    try
    {
        // Expired?
        if (stx->isFieldPresent(sfLastLedgerSequence) &&
            (stx->getFieldU32(sfLastLedgerSequence) <
             this->app().getLedgerMaster().getValidLedgerIndex()))
        {
            this->app().getHashRouter().setFlags(
                stx->getTransactionID(), SF_BAD);
            charge(Resource::feeUnwantedData);
            return;
        }

        if (checkSignature)
        {
            // Check the signature before handing off to the job queue.
            if (auto [valid, validReason] = checkValidity(
                    this->app().getHashRouter(),
                    *stx,
                    this->app().getLedgerMaster().getValidatedRules(),
                    this->app().config());
                valid != Validity::Valid)
            {
                if (!validReason.empty())
                {
                    JLOG(p_journal_.trace())
                        << "Exception checking transaction: " << validReason;
                }

                // Probably not necessary to set SF_BAD, but doesn't hurt.
                this->app().getHashRouter().setFlags(
                    stx->getTransactionID(), SF_BAD);
                charge(Resource::feeInvalidSignature);
                return;
            }
        }
        else
        {
            forceValidity(
                this->app().getHashRouter(),
                stx->getTransactionID(),
                Validity::Valid);
        }

        std::string reason;
        auto tx = std::make_shared<Transaction>(stx, reason, this->app());

        if (tx->getStatus() == INVALID)
        {
            if (!reason.empty())
            {
                JLOG(p_journal_.trace())
                    << "Exception checking transaction: " << reason;
            }
            this->app().getHashRouter().setFlags(
                stx->getTransactionID(), SF_BAD);
            charge(Resource::feeInvalidSignature);
            return;
        }

        bool const trusted(flags & SF_TRUSTED);
        this->app().getOPs().processTransaction(
            tx, trusted, false, NetworkOPs::FailHard::no);
    }
    catch (std::exception const&)
    {
        this->app().getHashRouter().setFlags(stx->getTransactionID(), SF_BAD);
        charge(Resource::feeBadData);
    }
}

// Called from our JobQueue
template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::checkPropose(
    Job& job,
    std::shared_ptr<protocol::TMProposeSet> const& packet,
    RCLCxPeerPos peerPos)
{
    bool isTrusted = (job.getType() == jtPROPOSAL_t);

    JLOG(p_journal_.trace())
        << "Checking " << (isTrusted ? "trusted" : "UNTRUSTED") << " proposal";

    assert(packet);

    if (!this->cluster() && !peerPos.checkSign())
    {
        JLOG(p_journal_.warn()) << "Proposal fails sig check";
        charge(Resource::feeInvalidSignature);
        return;
    }

    bool relay;

    if (isTrusted)
        relay = this->app().getOPs().processTrustedProposal(peerPos);
    else
        relay =
            this->app().config().RELAY_UNTRUSTED_PROPOSALS || this->cluster();

    if (relay)
    {
        // haveMessage contains peers, which are suppressed; i.e. the peers
        // are the source of the message, consequently the message should
        // not be relayed to these peers. But the message must be counted
        // as part of the squelch logic.
        auto haveMessage = this->app().overlay().relay(
            *packet, peerPos.suppressionID(), peerPos.publicKey());
        if (reduceRelayReady() && !haveMessage.empty())
            overlay_.updateSlotAndSquelch(
                peerPos.suppressionID(),
                peerPos.publicKey(),
                std::move(haveMessage),
                protocol::mtPROPOSE_LEDGER);
    }
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::checkValidation(
    std::shared_ptr<STValidation> const& val,
    std::shared_ptr<protocol::TMValidation> const& packet)
{
    if (!this->cluster() && !val->isValid())
    {
        JLOG(p_journal_.debug()) << "Validation forwarded by peer is invalid";
        charge(Resource::feeInvalidRequest);
        return;
    }

    // FIXME it should be safe to remove this try/catch. Investigate codepaths.
    try
    {
        if (this->app().getOPs().recvValidation(
                val, std::to_string(this->id())) ||
            this->cluster())
        {
            auto const suppression =
                sha512Half(makeSlice(val->getSerialized()));
            // haveMessage contains peers, which are suppressed; i.e. the peers
            // are the source of the message, consequently the message should
            // not be relayed to these peers. But the message must be counted
            // as part of the squelch logic.
            auto haveMessage =
                overlay_.relay(*packet, suppression, val->getSignerPublic());
            if (reduceRelayReady() && !haveMessage.empty())
            {
                overlay_.updateSlotAndSquelch(
                    suppression,
                    val->getSignerPublic(),
                    std::move(haveMessage),
                    protocol::mtVALIDATION);
            }
        }
    }
    catch (std::exception const&)
    {
        JLOG(p_journal_.trace()) << "Exception processing validation";
        charge(Resource::feeInvalidRequest);
    }
}

// Returns the set of peers that can help us get
// the TX tree with the specified root hash.
//
template <typename P2PeerImplmnt>
static std::shared_ptr<Peer>
getPeerWithTree(
    OverlayImpl<typename P2PeerImplmnt::P2POverlayImpl_t>& ov,
    uint256 const& rootHash,
    PeerImp<P2PeerImplmnt> const* skip)
{
    std::shared_ptr<Peer> ret;
    int retScore = 0;

    ov.foreach([&](std::shared_ptr<Peer> const& p) {
        if (p->hasTxSet(rootHash) && p->p2p().id() != skip->id())
        {
            auto score = p->getScore(true);
            if (!ret || (score > retScore))
            {
                ret = std::move(p);
                retScore = score;
            }
        }
    });

    return ret;
}

// Returns a random peer weighted by how likely to
// have the ledger and how responsive it is.
//
template <typename P2PeerImplmnt>
static std::shared_ptr<Peer>
getPeerWithLedger(
    OverlayImpl<typename P2PeerImplmnt::P2POverlayImpl_t>& ov,
    uint256 const& ledgerHash,
    LedgerIndex ledger,
    PeerImp<P2PeerImplmnt> const* skip)
{
    std::shared_ptr<Peer> ret;
    int retScore = 0;

    ov.foreach([&](std::shared_ptr<Peer> const& p) {
        if (p->hasLedger(ledgerHash, ledger) && p->p2p().id() != skip->id())
        {
            auto score = p->getScore(true);
            if (!ret || (score > retScore))
            {
                ret = std::move(p);
                retScore = score;
            }
        }
    });

    return ret;
}

// VFALCO NOTE This function is way too big and cumbersome.
template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::getLedger(
    std::shared_ptr<protocol::TMGetLedger> const& m)
{
    protocol::TMGetLedger& packet = *m;
    std::shared_ptr<SHAMap> shared;
    SHAMap const* map = nullptr;
    protocol::TMLedgerData reply;
    bool fatLeaves = true;
    std::shared_ptr<Ledger const> ledger;

    if (packet.has_requestcookie())
        reply.set_requestcookie(packet.requestcookie());

    std::string logMe;

    if (packet.itype() == protocol::liTS_CANDIDATE)
    {
        // Request is for a transaction candidate set
        JLOG(p_journal_.trace()) << "GetLedger: Tx candidate set";

        if (!packet.has_ledgerhash() ||
            !stringIsUint256Sized(packet.ledgerhash()))
        {
            charge(Resource::feeInvalidRequest);
            JLOG(p_journal_.warn()) << "GetLedger: Tx candidate set invalid";
            return;
        }

        uint256 const txHash{packet.ledgerhash()};

        shared = this->app().getInboundTransactions().getSet(txHash, false);
        map = shared.get();

        if (!map)
        {
            if (packet.has_querytype() && !packet.has_requestcookie())
            {
                JLOG(p_journal_.debug()) << "GetLedger: Routing Tx set request";

                if (auto const v = getPeerWithTree(overlay_, txHash, this))
                {
                    packet.set_requestcookie(this->id());
                    v->p2p().send(std::make_shared<Message>(
                        packet, protocol::mtGET_LEDGER));
                    return;
                }

                JLOG(p_journal_.info()) << "GetLedger: Route TX set failed";
                return;
            }

            JLOG(p_journal_.debug()) << "GetLedger: Can't provide map ";
            charge(Resource::feeInvalidRequest);
            return;
        }

        reply.set_ledgerseq(0);
        reply.set_ledgerhash(txHash.begin(), txHash.size());
        reply.set_type(protocol::liTS_CANDIDATE);
        fatLeaves = false;  // We'll already have most transactions
    }
    else
    {
        if (this->queueSize() >= Tuning::dropSendQueue)
        {
            JLOG(p_journal_.debug()) << "GetLedger: Large send queue";
            return;
        }

        if (this->app().getFeeTrack().isLoadedLocal() && !this->cluster())
        {
            JLOG(p_journal_.debug()) << "GetLedger: Too busy";
            return;
        }

        // Figure out what ledger they want
        JLOG(p_journal_.trace()) << "GetLedger: Received";

        if (packet.has_ledgerhash())
        {
            if (!stringIsUint256Sized(packet.ledgerhash()))
            {
                charge(Resource::feeInvalidRequest);
                JLOG(p_journal_.warn()) << "GetLedger: Invalid request";
                return;
            }

            uint256 const ledgerhash{packet.ledgerhash()};
            logMe += "LedgerHash:";
            logMe += to_string(ledgerhash);
            ledger = this->app().getLedgerMaster().getLedgerByHash(ledgerhash);

            if (!ledger && packet.has_ledgerseq())
            {
                if (auto shardStore = this->app().getShardStore())
                {
                    auto seq = packet.ledgerseq();
                    if (seq >= shardStore->earliestLedgerSeq())
                        ledger = shardStore->fetchLedger(ledgerhash, seq);
                }
            }

            if (!ledger)
            {
                JLOG(p_journal_.trace())
                    << "GetLedger: Don't have " << ledgerhash;
            }

            if (!ledger &&
                (packet.has_querytype() && !packet.has_requestcookie()))
            {
                // We don't have the requested ledger
                // Search for a peer who might
                auto const v = getPeerWithLedger(
                    overlay_,
                    ledgerhash,
                    packet.has_ledgerseq() ? packet.ledgerseq() : 0,
                    this);
                if (!v)
                {
                    JLOG(p_journal_.trace()) << "GetLedger: Cannot route";
                    return;
                }

                packet.set_requestcookie(this->id());
                v->p2p().send(
                    std::make_shared<Message>(packet, protocol::mtGET_LEDGER));
                JLOG(p_journal_.debug()) << "GetLedger: Request routed";
                return;
            }
        }
        else if (packet.has_ledgerseq())
        {
            if (packet.ledgerseq() <
                this->app().getLedgerMaster().getEarliestFetch())
            {
                JLOG(p_journal_.debug()) << "GetLedger: Early ledger request";
                return;
            }
            ledger = this->app().getLedgerMaster().getLedgerBySeq(
                packet.ledgerseq());
            if (!ledger)
            {
                JLOG(p_journal_.debug())
                    << "GetLedger: Don't have " << packet.ledgerseq();
            }
        }
        else if (packet.has_ltype() && (packet.ltype() == protocol::ltCLOSED))
        {
            ledger = this->app().getLedgerMaster().getClosedLedger();
            assert(!ledger->open());
            // VFALCO ledger should never be null!
            // VFALCO How can the closed ledger be open?
#if 0
            if (ledger && ledger->info().open)
            ledger = app_.getLedgerMaster ().getLedgerBySeq (
                ledger->info().seq - 1);
#endif
        }
        else
        {
            charge(Resource::feeInvalidRequest);
            JLOG(p_journal_.warn()) << "GetLedger: Unknown request";
            return;
        }

        if ((!ledger) ||
            (packet.has_ledgerseq() &&
             (packet.ledgerseq() != ledger->info().seq)))
        {
            charge(Resource::feeInvalidRequest);

            if (ledger)
            {
                JLOG(p_journal_.warn()) << "GetLedger: Invalid sequence";
            }
            return;
        }

        if (!packet.has_ledgerseq() &&
            (ledger->info().seq <
             this->app().getLedgerMaster().getEarliestFetch()))
        {
            JLOG(p_journal_.debug()) << "GetLedger: Early ledger request";
            return;
        }

        // Fill out the reply
        auto const lHash = ledger->info().hash;
        reply.set_ledgerhash(lHash.begin(), lHash.size());
        reply.set_ledgerseq(ledger->info().seq);
        reply.set_type(packet.itype());

        if (packet.itype() == protocol::liBASE)
        {
            // they want the ledger base data
            JLOG(p_journal_.trace()) << "GetLedger: Base data";
            Serializer nData(128);
            addRaw(ledger->info(), nData);
            reply.add_nodes()->set_nodedata(
                nData.getDataPtr(), nData.getLength());

            auto const& stateMap = ledger->stateMap();
            if (stateMap.getHash() != beast::zero)
            {
                // return account state root node if possible
                Serializer rootNode(768);

                stateMap.serializeRoot(rootNode);
                reply.add_nodes()->set_nodedata(
                    rootNode.getDataPtr(), rootNode.getLength());

                if (ledger->info().txHash != beast::zero)
                {
                    auto const& txMap = ledger->txMap();
                    if (txMap.getHash() != beast::zero)
                    {
                        rootNode.erase();

                        txMap.serializeRoot(rootNode);
                        reply.add_nodes()->set_nodedata(
                            rootNode.getDataPtr(), rootNode.getLength());
                    }
                }
            }

            auto oPacket =
                std::make_shared<Message>(reply, protocol::mtLEDGER_DATA);
            this->send(oPacket);
            return;
        }

        if (packet.itype() == protocol::liTX_NODE)
        {
            map = &ledger->txMap();
            logMe += " TX:";
            logMe += to_string(map->getHash());
        }
        else if (packet.itype() == protocol::liAS_NODE)
        {
            map = &ledger->stateMap();
            logMe += " AS:";
            logMe += to_string(map->getHash());
        }
    }

    if (!map || (packet.nodeids_size() == 0))
    {
        JLOG(p_journal_.warn()) << "GetLedger: Can't find map or empty request";
        charge(Resource::feeInvalidRequest);
        return;
    }

    JLOG(p_journal_.trace()) << "GetLedger: " << logMe;

    auto const depth = packet.has_querydepth()
        ? (std::min(packet.querydepth(), 3u))
        : (isHighLatency() ? 2 : 1);

    for (int i = 0;
         (i < packet.nodeids().size() &&
          (reply.nodes().size() < Tuning::maxReplyNodes));
         ++i)
    {
        auto const mn = deserializeSHAMapNodeID(packet.nodeids(i));

        if (!mn)
        {
            JLOG(p_journal_.warn()) << "GetLedger: Invalid node " << logMe;
            charge(Resource::feeBadData);
            return;
        }

        std::vector<SHAMapNodeID> nodeIDs;
        std::vector<Blob> rawNodes;

        try
        {
            if (map->getNodeFat(*mn, nodeIDs, rawNodes, fatLeaves, depth))
            {
                assert(nodeIDs.size() == rawNodes.size());
                JLOG(p_journal_.trace()) << "GetLedger: getNodeFat got "
                                         << rawNodes.size() << " nodes";
                std::vector<SHAMapNodeID>::iterator nodeIDIterator;
                std::vector<Blob>::iterator rawNodeIterator;

                for (nodeIDIterator = nodeIDs.begin(),
                    rawNodeIterator = rawNodes.begin();
                     nodeIDIterator != nodeIDs.end();
                     ++nodeIDIterator, ++rawNodeIterator)
                {
                    protocol::TMLedgerNode* node = reply.add_nodes();
                    node->set_nodeid(nodeIDIterator->getRawString());
                    node->set_nodedata(
                        &rawNodeIterator->front(), rawNodeIterator->size());
                }
            }
            else
            {
                JLOG(p_journal_.warn())
                    << "GetLedger: getNodeFat returns false";
            }
        }
        catch (std::exception&)
        {
            std::string info;

            if (packet.itype() == protocol::liTS_CANDIDATE)
                info = "TS candidate";
            else if (packet.itype() == protocol::liBASE)
                info = "Ledger base";
            else if (packet.itype() == protocol::liTX_NODE)
                info = "TX node";
            else if (packet.itype() == protocol::liAS_NODE)
                info = "AS node";

            if (!packet.has_ledgerhash())
                info += ", no hash specified";

            JLOG(p_journal_.warn())
                << "getNodeFat( " << *mn << ") throws exception: " << info;
        }
    }

    JLOG(p_journal_.info())
        << "Got request for " << packet.nodeids().size() << " nodes at depth "
        << depth << ", return " << reply.nodes().size() << " nodes";

    auto oPacket = std::make_shared<Message>(reply, protocol::mtLEDGER_DATA);
    this->send(oPacket);
}

template <typename P2PeerImplmnt>
int
PeerImp<P2PeerImplmnt>::getScore(bool haveItem) const
{
    // Random component of score, used to break ties and avoid
    // overloading the "best" peer
    static const int spRandomMax = 9999;

    // Score for being very likely to have the thing we are
    // look for; should be roughly spRandomMax
    static const int spHaveItem = 10000;

    // Score reduction for each millisecond of latency; should
    // be roughly spRandomMax divided by the maximum reasonable
    // latency
    static const int spLatency = 30;

    // Penalty for unknown latency; should be roughly spRandomMax
    static const int spNoLatency = 8000;

    int score = rand_int(spRandomMax);

    if (haveItem)
        score += spHaveItem;

    std::optional<std::chrono::milliseconds> latency;
    {
        std::lock_guard sl(this->recentLock());
        latency = latency_;
    }

    if (latency)
        score -= latency->count() * spLatency;
    else
        score -= spNoLatency;

    return score;
}

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::isHighLatency() const
{
    std::lock_guard sl(this->recentLock());
    return latency_ >= peerHighLatency;
}

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::reduceRelayReady()
{
    if (!reduceRelayReady_)
        reduceRelayReady_ =
            reduce_relay::epoch<std::chrono::minutes>(UptimeClock::now()) >
            reduce_relay::WAIT_ON_BOOTUP;
    return vpReduceRelayEnabled_ && reduceRelayReady_;
}

/////////////////////////////////////////////////////////////////
// Hooks
////////////////////////////////////////////////////////////////
template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onEvtClose()
{
    cancelTimer();
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onEvtShutdown()
{
    cancelTimer();
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onEvtGracefulClose()
{
    setTimer();
}

template <typename P2PeerImplmnt>
bool
PeerImp<P2PeerImplmnt>::squelched(std::shared_ptr<Message> const& m)
{
    auto validator = m->getValidatorKey();
    return validator && !squelch_.expireSquelch(*validator);
}

template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onEvtProtocolStart()
{
    sendOnProtocolStart(!this->isInbound());

    setTimer();
}

// should this go into run() where it was originally?
template <typename P2PeerImplmnt>
void
PeerImp<P2PeerImplmnt>::onEvtRun()
{
    auto parseLedgerHash =
        [](std::string const& value) -> std::optional<uint256> {
        if (uint256 ret; ret.parseHex(value))
            return ret;

        if (auto const s = base64_decode(value); s.size() == uint256::size())
            return uint256{s};

        return std::nullopt;
    };

    std::optional<uint256> closed;
    std::optional<uint256> previous;

    if (auto const iter = this->headers().find("Closed-Ledger");
        iter != this->headers().end())
    {
        closed = parseLedgerHash(iter->value().to_string());

        if (!closed)
            this->fail("Malformed handshake data (1)");
    }

    if (auto const iter = this->headers().find("Previous-Ledger");
        iter != this->headers().end())
    {
        previous = parseLedgerHash(iter->value().to_string());

        if (!previous)
            this->fail("Malformed handshake data (2)");
    }

    if (previous && !closed)
        this->fail("Malformed handshake data (3)");

    {
        std::lock_guard<std::mutex> sl(this->recentLock());
        if (closed)
            closedLedgerHash_ = *closed;
        if (previous)
            previousLedgerHash_ = *previous;
    }
}

template <typename P2PeerImplmnt>
std::pair<size_t, boost::system::error_code>
PeerImp<P2PeerImplmnt>::onEvtProtocolMessage(
    boost::beast::multi_buffer const& mbuffers,
    size_t& hint)
{
    std::pair<std::size_t, boost::system::error_code> result = {0, {}};
    bool success;

    auto buffers = mbuffers.data();

    auto header = getHeader(buffers, p2p_, hint);

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
        case protocol::mtMANIFESTS:
            success = invoke<protocol::TMManifests>(*header.first, buffers);
            break;
        case protocol::mtPING:
            success = invoke<protocol::TMPing>(*header.first, buffers);
            break;
        case protocol::mtCLUSTER:
            success = invoke<protocol::TMCluster>(*header.first, buffers);
            break;
        case protocol::mtGET_SHARD_INFO:
            success = invoke<protocol::TMGetShardInfo>(*header.first, buffers);
            break;
        case protocol::mtSHARD_INFO:
            success = invoke<protocol::TMShardInfo>(*header.first, buffers);
            break;
        case protocol::mtGET_PEER_SHARD_INFO:
            success =
                invoke<protocol::TMGetPeerShardInfo>(*header.first, buffers);
            break;
        case protocol::mtPEER_SHARD_INFO:
            success = invoke<protocol::TMPeerShardInfo>(*header.first, buffers);
            break;
        case protocol::mtENDPOINTS:
            success = invoke<protocol::TMEndpoints>(*header.first, buffers);
            break;
        case protocol::mtTRANSACTION:
            success = invoke<protocol::TMTransaction>(*header.first, buffers);
            break;
        case protocol::mtGET_LEDGER:
            success = invoke<protocol::TMGetLedger>(*header.first, buffers);
            break;
        case protocol::mtLEDGER_DATA:
            success = invoke<protocol::TMLedgerData>(*header.first, buffers);
            break;
        case protocol::mtPROPOSE_LEDGER:
            success = invoke<protocol::TMProposeSet>(*header.first, buffers);
            break;
        case protocol::mtSTATUS_CHANGE:
            success = invoke<protocol::TMStatusChange>(*header.first, buffers);
            break;
        case protocol::mtHAVE_SET:
            success =
                invoke<protocol::TMHaveTransactionSet>(*header.first, buffers);
            break;
        case protocol::mtVALIDATION:
            success = invoke<protocol::TMValidation>(*header.first, buffers);
            break;
        case protocol::mtVALIDATORLIST:
            success = invoke<protocol::TMValidatorList>(*header.first, buffers);
            break;
        case protocol::mtVALIDATORLISTCOLLECTION:
            success = invoke<protocol::TMValidatorListCollection>(
                *header.first, buffers);
            break;
        case protocol::mtGET_OBJECTS:
            success =
                invoke<protocol::TMGetObjectByHash>(*header.first, buffers);
            break;
        case protocol::mtSQUELCH:
            success = invoke<protocol::TMSquelch>(*header.first, buffers);
            break;
        case protocol::mtPROOF_PATH_REQ:
            success =
                invoke<protocol::TMProofPathRequest>(*header.first, buffers);
            break;
        case protocol::mtPROOF_PATH_RESPONSE:
            success =
                invoke<protocol::TMProofPathResponse>(*header.first, buffers);
            break;
        case protocol::mtREPLAY_DELTA_REQ:
            success =
                invoke<protocol::TMReplayDeltaRequest>(*header.first, buffers);
            break;
        case protocol::mtREPLAY_DELTA_RESPONSE:
            success =
                invoke<protocol::TMReplayDeltaResponse>(*header.first, buffers);
            break;
        case protocol::mtPROTOCOL_STARTED:
            success =
                invoke<protocol::TMProtocolStarted>(*header.first, buffers);
            break;
        default:
            onMessageUnknown(header.first->message_type);
            success = true;
            break;
    }

    result.first = header.first->total_wire_size;

    if (!success)
        result.second = make_error_code(boost::system::errc::bad_message);

    return result;
}

}  // namespace ripple

#endif
