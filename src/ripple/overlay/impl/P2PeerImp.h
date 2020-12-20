//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED
#define RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED

#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/Squelch.h>
#include <ripple/overlay/P2Peer.h>
#include <ripple/overlay/Compression.h>

#include <boost/circular_buffer.hpp>

#include <queue>

namespace ripple {

class P2PeerImp : public P2Peer,
                  public std::enable_shared_from_this<P2PeerImp>
{
protected:

    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer =
    boost::asio::basic_waitable_timer<std::chrono::steady_clock>;
    using Compressed = compression::Compressed;

    Application& app_;
    id_t const id_;
    beast::WrappedSink sink_;
    beast::WrappedSink p_sink_;
    beast::Journal const journal_;
    beast::Journal const p_journal_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::strand<boost::asio::executor> strand_;
    //waitable_timer timer_;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remote_address_;

    // These are up here to prevent warnings about order of initializations
    //
    OverlayImpl& overlay_;
    bool const inbound_;

    // Protocol version to use for this link
    ProtocolVersion protocol_;

    //- std::atomic<Tracking> tracking_;
    clock_type::time_point trackingTime_;
    bool detaching_ = false;
    // Node public key of peer.
    PublicKey const publicKey_;
    std::string name_;
    boost::shared_mutex mutable nameMutex_;

    // The indices of the smallest and largest ledgers this peer has available
    //
    //-LedgerIndex minLedger_ = 0;
    //-LedgerIndex maxLedger_ = 0;
    //-uint256 closedLedgerHash_;
    //-uint256 previousLedgerHash_;

    //-boost::circular_buffer<uint256> recentLedgers_{128};
    //-boost::circular_buffer<uint256> recentTxSets_{128};

    boost::optional<std::chrono::milliseconds> latency_;
    boost::optional<std::uint32_t> lastPingSeq_;
    clock_type::time_point lastPingTime_;
    clock_type::time_point const creationTime_;

    squelch::Squelch<UptimeClock> squelch_;

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
    //-protocol::TMStatusChange last_status_;
    //-Resource::Consumer usage_;
    //-Resource::Charge fee_;
    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::multi_buffer read_buffer_;
    http_request_type request_;
    http_response_type response_;
    boost::beast::http::fields const& headers_;
    std::queue<std::shared_ptr<Message>> send_queue_;
    bool gracefulClose_ = false;
    int large_sendq_ = 0;
    //-std::unique_ptr<LoadEvent> load_event_;
    // The highest sequence of each PublisherList that has
    // been sent to or received from this peer.
    hash_map<PublicKey, std::size_t> publisherListSequences_;

    //-std::mutex mutable shardInfoMutex_;
    //-hash_map<PublicKey, ShardInfo> shardInfo_;

    Compressed compressionEnabled_ = Compressed::Off;

    friend class OverlayImpl;

    class Metrics
    {
    public:
        Metrics() = default;
        Metrics(Metrics const&) = delete;
        Metrics&
        operator=(Metrics const&) = delete;
        Metrics(Metrics&&) = delete;
        Metrics&
        operator=(Metrics&&) = delete;

        void
        add_message(std::uint64_t bytes);
        std::uint64_t
        average_bytes() const;
        std::uint64_t
        total_bytes() const;

    private:
        boost::shared_mutex mutable mutex_;
        boost::circular_buffer<std::uint64_t> rollingAvg_{30, 0ull};
        clock_type::time_point intervalStart_{clock_type::now()};
        std::uint64_t totalBytes_{0};
        std::uint64_t accumBytes_{0};
        std::uint64_t rollingAvgBytes_{0};
    };

    struct
    {
        Metrics sent;
        Metrics recv;
    } metrics_;
public:
    P2PeerImp(P2PeerImp const&) = delete;
    P2PeerImp&
    operator=(P2PeerImp const&) = delete;

    P2PeerImp(
        Application& app,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        OverlayImpl& overlay);

    template <class Buffers>
    P2PeerImp(
        Application& app,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        OverlayImpl& overlay);

    void
    send(std::shared_ptr<Message> const&) override;

    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remote_address_;
    }

    id_t
    id() const override
    {
        return id_;
    }

    /** Returns `true` if this connection will publicly share its IP address. */
    bool
    crawl() const;

    bool
    cluster() const override;

    // Called to determine our priority for querying
    int
    getScore(bool haveItem) const override;

    bool
    isHighLatency() const override;

    void
    fail(std::string const&);

    Json::Value
    json() override;

    bool supportsFeature(ProtocolFeature) const override;

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
    setPublisherListSequence(PublicKey const& pubKey, std::size_t const seq) override
    {
        std::lock_guard<std::mutex> sl(recentLock_);

        publisherListSequences_[pubKey] = seq;
    }

    bool
    compressionEnabled() const override
    {
        return compressionEnabled_ == Compressed::On;
    }

    /** Return the version of rippled that the peer is running, if reported. */
    std::string
    getVersion() const;

    // Return the connection elapsed time.
    clock_type::duration
    uptime() const
    {
        return clock_type::now() - creationTime_;
    }

    PublicKey const&
    getNodePublic() const override
    {
        return publicKey_;
    }

    // TBD should be private, need better encapsulation
protected:

    void
    close();

    void
    fail(std::string const& name, error_code ec);

    void
    gracefulClose();

    // TBD - timer is defined in
    // the subclass PeerImp, but need setTimer() and cancelTimer()
    virtual void
    setTimer() {}

    virtual void
    cancelTimer() {}

    static std::string
    makePrefix(id_t id);

    void
    doAccept();

    void
    doProtocolStart();

    void
    onReadMessage(error_code ec, std::size_t bytes_transferred);

    void
    onWriteMessage(error_code ec, std::size_t bytes_transferred);

    std::string
    name() const;

    std::string
    domain() const;

    void
    onShutdown(error_code);
};

template <class Buffers>
P2PeerImp::P2PeerImp(
    Application& app,
    std::unique_ptr<stream_type>&& stream_ptr,
    Buffers const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    OverlayImpl& overlay)
    : app_(app)
    , id_(id)
    , sink_(app_.journal("Peer"), makePrefix(id))
    , p_sink_(app_.journal("Protocol"), makePrefix(id))
    , journal_(sink_)
    , p_journal_(p_sink_)
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , overlay_(overlay)
    , inbound_(false)
    , protocol_(protocol)
    , trackingTime_(clock_type::now())
    , publicKey_(publicKey)
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , slot_(std::move(slot))
    , response_(std::move(response))
    , headers_(response_)
    , compressionEnabled_(
        headers_["X-Offer-Compression"] == "lz4" && app_.config().COMPRESSION
        ? Compressed::On
        : Compressed::Off)
{
    read_buffer_.commit(boost::asio::buffer_copy(
        read_buffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
}

} // ripple

#endif  // RIPPLED_P2PEERIMP_H
