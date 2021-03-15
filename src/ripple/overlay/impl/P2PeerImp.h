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

#ifndef RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED
#define RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/overlay/P2Peer.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/OverlayImplTraits.h>
#include <ripple/overlay/impl/P2POverlayBaseImpl.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/overlay/impl/TrafficCount.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/resource/Consumer.h>
#include <ripple/resource/Fees.h>

#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cstdint>
#include <memory>
#include <queue>
#include <shared_mutex>

namespace ripple {

class Application;

namespace {
/** The threshold above which we treat a peer connection as high latency */
std::chrono::milliseconds constexpr peerHighLatency{300};
}  // namespace

class P2PeerImp : public virtual P2Peer,
                  public P2PeerEvents,
                  public P2POverlayBaseImpl::Child
{
protected:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using Compressed = compression::Compressed;

    Application& p2pApp_;
    id_t const id_;
    beast::WrappedSink sink_;
    beast::WrappedSink p_sink_;
    beast::Journal const journal_;
    beast::Journal const p_journal_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::strand<boost::asio::executor> strand_;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remote_address_;

    // These are up here to prevent warnings about order of initializations
    //
    // OverlayImpl& overlay_;
    bool const inbound_;

    // Protocol version to use for this link
    ProtocolVersion protocol_;

    bool detaching_ = false;
    // Node public key of peer.
    PublicKey const publicKey_;
    std::string name_;
    boost::shared_mutex mutable nameMutex_;

    boost::optional<std::chrono::milliseconds> latency_;

    std::mutex mutable recentLock_;
    Resource::Consumer usage_;
    Resource::Charge fee_;
    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::multi_buffer read_buffer_;
    http_request_type request_;
    http_response_type response_;
    boost::beast::http::fields const& headers_;
    std::queue<std::shared_ptr<Message>> send_queue_;
    bool gracefulClose_ = false;
    int large_sendq_ = 0;

    Compressed compressionEnabled_ = Compressed::Off;

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

    /** Create an active incoming peer from an established ssl connection. */
    P2PeerImp(
        Application& app,
        Config const& config,
        Logs& logs,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayBaseImpl& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    P2PeerImp(
        Application& app,
        Config const& config,
        Logs& logs,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayBaseImpl& overlay);

    virtual ~P2PeerImp();

    beast::Journal const&
    pjournal() const
    {
        return p_journal_;
    }

    std::shared_ptr<PeerFinder::Slot> const&
    slot()
    {
        return slot_;
    }

    // Work-around for calling shared_from_this in constructors
    void
    run();

    //
    // Network
    //

    void
    send(std::shared_ptr<Message> const& m) override;

    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remote_address_;
    }

    void
    charge(Resource::Charge const& fee) override;

    //
    // Identity
    //

    Peer::id_t
    id() const override
    {
        return id_;
    }

    PublicKey const&
    getNodePublic() const override
    {
        return publicKey_;
    }

    /** Return the version of rippled that the peer is running, if reported. */
    std::string
    getVersion() const;

    Json::Value
    json() override;

    // Called to determine our priority for querying
    int
    getScore(bool haveItem) const override;

    bool
    isHighLatency() const override;

    void
    fail(std::string const& reason);

    bool
    compressionEnabled() const override
    {
        return compressionEnabled_ == Compressed::On;
    }

protected:
    void
    close();

    void
    fail(std::string const& name, error_code ec);

    void
    gracefulClose();

    static std::string
    makePrefix(id_t id);

    // Called when SSL shutdown completes
    void
    onShutdown(error_code ec);

    void
    doAccept();

    std::string
    name() const;

    std::string
    domain() const;

    std::optional<std::uint32_t>
    networkID() const;

    //
    // protocol message loop
    //

    // Starts the protocol message loop
    void
    doProtocolStart();

    // Called when protocol message bytes are received
    void
    onReadMessage(error_code ec, std::size_t bytes_transferred);

    // Called when protocol messages bytes are sent
    void
    onWriteMessage(error_code ec, std::size_t bytes_transferred);

protected:
    bool
    isSocketOpen() const override
    {
        return socket_.is_open();
    }

    std::size_t
    queueSize() const override
    {
        return send_queue_.size();
    }

    virtual std::shared_ptr<P2PeerImp>
    shared() = 0;

public:
    virtual std::pair<std::size_t, boost::system::error_code>
    invokeProtocolMessage(
        detail::MessageHeader const& header,
        boost::beast::multi_buffer const& buffers,
        std::size_t& hint) = 0;

    virtual bool
    squelched(std::shared_ptr<Message> const&) = 0;
};

template <class Buffers>
P2PeerImp::P2PeerImp(
    Application& app,
    Config const& config,
    Logs& logs,
    std::unique_ptr<stream_type>&& stream_ptr,
    Buffers const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    P2POverlayBaseImpl& overlay)
    : P2POverlayBaseImpl::Child(overlay)
    , p2pApp_(app)
    , id_(id)
    , sink_(logs.journal("Peer"), makePrefix(id))
    , p_sink_(logs.journal("Protocol"), makePrefix(id))
    , journal_(sink_)
    , p_journal_(p_sink_)
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(false)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , usage_(usage)
    , fee_(Resource::feeLightPeer)
    , slot_(std::move(slot))
    , response_(std::move(response))
    , headers_(response_)
    , compressionEnabled_(
          peerFeatureEnabled(headers_, FEATURE_COMPR, "lz4", config.COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
    read_buffer_.commit(boost::asio::buffer_copy(
        read_buffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
    JLOG(journal_.debug()) << "compression enabled "
                           << (compressionEnabled_ == Compressed::On) << " "
                           << id_;
}

}  // namespace ripple

#endif
