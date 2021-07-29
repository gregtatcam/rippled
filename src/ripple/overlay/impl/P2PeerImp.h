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
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/protocol/Protocol.h>

#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <cstdint>
#include <optional>
#include <queue>

namespace ripple {

class P2PeerImp : public Peer,
                  public std::enable_shared_from_this<P2PeerImp>,
                  public P2POverlayImpl::Child
{
protected:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using Compressed = compression::Compressed;
    using mutable_buffers_type =
        boost::beast::multi_buffer::mutable_buffers_type;
    using const_buffers_type = boost::beast::multi_buffer::const_buffers_type;

private:
    P2PConfig const& p2pConfig_;
    beast::WrappedSink sink_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    bool detaching_ = false;
    std::string name_;
    boost::shared_mutex mutable nameMutex_;
    boost::beast::multi_buffer read_buffer_;
    http_request_type request_;
    http_response_type response_;
    std::queue<std::shared_ptr<Message>> send_queue_;
    bool gracefulClose_ = false;
    std::uint32_t large_sendq_ = 0;

protected:
    id_t const id_;
    beast::Journal const journal_;
    boost::asio::strand<boost::asio::executor> const strand_;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remote_address_;

    bool const inbound_;

    // Protocol version to use for this link
    ProtocolVersion const protocol_;

    // Node public key of peer.
    PublicKey const publicKey_;

    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::http::fields const& headers_;

    Compressed const compressionEnabled_ = Compressed::Off;

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

    /** Create an active incoming peer from an established ssl connection. */
    P2PeerImp(
        P2PConfig const& p2pConfig,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    P2PeerImp(
        P2PConfig const& p2pConfig,
        std::unique_ptr<stream_type>&& stream_ptr,
        const_buffers_type const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay);

    virtual ~P2PeerImp();

    std::shared_ptr<PeerFinder::Slot> const&
    slot()
    {
        return slot_;
    }

    // Work-around for calling shared_from_this in constructors
    void
    run();

    // Called when Overlay gets a stop request.
    void
    stop() override;

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

    bool
    isSocketOpen() const
    {
        return socket_.is_open();
    }

    socket_type::executor_type
    getSocketExecutor() const
    {
        return socket_.get_executor();
    }

    std::size_t
    sendQueueSize() const
    {
        return send_queue_.size();
    }

    std::uint32_t
    incLargeSendQueue()
    {
        auto q = large_sendq_;
        large_sendq_++;
        return q;
    }

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

    static std::string
    makePrefix(id_t id);

    std::string
    name() const;

    std::string
    domain() const;

private:
    void
    gracefulClose();

    // Called when SSL shutdown completes
    void
    onShutdown(error_code ec);

    void
    doAccept();

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

    //--------------------------------------------------------------------------
    // Delegate custom handling of events to the application layer.
    virtual void
    onEvtRun() = 0;

    virtual bool
    onEvtSendFilter(std::shared_ptr<Message> const&) = 0;

    virtual void
    onEvtClose() = 0;

    virtual void
    onEvtGracefulClose() = 0;

    virtual void
    onEvtShutdown() = 0;

    virtual void
    onEvtDoProtocolStart() = 0;

    virtual bool
    onEvtProtocolMessage(
        detail::MessageHeader const& header,
        mutable_buffers_type const& buffers) = 0;

    friend std::pair<std::size_t, boost::system::error_code>
    invokeProtocolMessage<mutable_buffers_type, P2PeerImp>(
        mutable_buffers_type const& buffers,
        P2PeerImp& handler,
        std::size_t& hint);
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED
