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
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/resource/Fees.h>

#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cstdint>
#include <optional>
#include <queue>

namespace ripple {

class P2PeerImp : public P2PeerInternal,
                  public P2POverlayImpl::Child,
                  public std::enable_shared_from_this<P2PeerImp>
{
public:
    using P2POverlayImpl_t = P2POverlayImpl;

private:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using Compressed = compression::Compressed;

    Application& app_;
    id_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::strand<boost::asio::executor> strand_;
    beast::IP::Endpoint const remote_address_;
    bool const inbound_;
    ProtocolVersion protocol_;
    bool detaching_ = false;
    PublicKey const publicKey_;
    std::string name_;
    boost::shared_mutex mutable nameMutex_;
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
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <typename Buffers>
    P2PeerImp(
        Application& app,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay);

    virtual ~P2PeerImp();

    ////////////////////////////////////////////////////////////////
    // P2p methods
    ////////////////////////////////////////////////////////////////

    void
    send(std::shared_ptr<Message> const& m) override;

    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remote_address_;
    }

    P2Peer::id_t
    id() const override
    {
        return id_;
    }

    bool
    cluster() const override;

    PublicKey const&
    getNodePublic() const override
    {
        return publicKey_;
    }

    bool
    compressionEnabled() const override
    {
        return compressionEnabled_ == Compressed::On;
    }

    ////////////////////////////////////////////////////////////////
    // Getters and other methods shared with the application layer
    ////////////////////////////////////////////////////////////////
protected:
    Application&
    app() const override
    {
        return app_;
    }

    beast::Journal const&
    journal() override
    {
        return journal_;
    }

    boost::asio::strand<boost::asio::executor>&
    strand() override
    {
        return strand_;
    }

    bool
    isSocketOpen() const override
    {
        return socket_.is_open();
    }

    int
    queueSize() const override
    {
        return send_queue_.size();
    }

    bool
    isInbound() const override
    {
        return inbound_;
    }

    ProtocolVersion
    protocol() const override
    {
        return protocol_;
    }

    int
    incLargeSendq() override
    {
        return ++large_sendq_;
    }

    int
    largeSendq() const override
    {
        return large_sendq_;
    }

    void
    fail(std::string const&) override;

    void
    close() override;

public:  // TODO
    std::string
    getVersion() const override;

    std::shared_ptr<PeerFinder::Slot> const&
    slot() override
    {
        return slot_;
    }

    // Work-around for calling shared_from_this in constructors
    void
    run();

protected:
    virtual boost::beast::http::fields const&
    headers() const override
    {
        return headers_;
    }

    std::string
    name() const override;

    std::string
    domain() const override;

    Json::Value
    json() override;
    //////////////

    // Called when Overlay gets a stop request.
    void
    stop() override;

private:
    socket_type&
    socket() const override
    {
        return socket_;
    }

    void
    fail(std::string const& name, error_code ec);

    void
    gracefulClose();

    // Called when SSL shutdown completes
    void
    onShutdown(error_code ec);

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
};

//------------------------------------------------------------------------------

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
    P2POverlayImpl& overlay)
    : Child(overlay)
    , app_(app)
    , id_(id)
    , sink_(app_.journal("Peer"), P2Peer::makePrefix(id))
    , journal_(sink_)
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(false)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , slot_(std::move(slot))
    , response_(std::move(response))
    , headers_(response_)
    , compressionEnabled_(
          peerFeatureEnabled(
              headers_,
              FEATURE_COMPR,
              "lz4",
              app_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
    if (auto member = app_.cluster().member(publicKey_))
    {
        name_ = *member;
        JLOG(journal_.info()) << "Cluster name: " << *member;
    }
    read_buffer_.commit(boost::asio::buffer_copy(
        read_buffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
    JLOG(journal_.debug()) << "compression enabled "
                           << (compressionEnabled_ == Compressed::On) << " "
                           << id_;
}

}  // namespace ripple

#endif
