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

#include <ripple/basics/base64.h>
#include <ripple/basics/random.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/digest.h>

#include <boost/beast/core/ostream.hpp>

#include <algorithm>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>

namespace ripple {

P2PeerImp::P2PeerImp(
    P2PConfig const& p2pConfig,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    std::unique_ptr<stream_type>&& stream_ptr,
    P2POverlayImpl& overlay)
    : Child(overlay)
    , p2pConfig_(p2pConfig)
    , sink_(p2pConfig.logs().journal("Peer"), makePrefix(id))
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , request_(std::move(request))
    , id_(id)
    , journal_(sink_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(true)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , slot_(slot)
    , headers_(request_)
    , compressionEnabled_(
          peerFeatureEnabled(
              headers_,
              FEATURE_COMPR,
              "lz4",
              p2pConfig_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
}

P2PeerImp::P2PeerImp(
    P2PConfig const& p2pConfig,
    std::unique_ptr<stream_type>&& stream_ptr,
    const_buffers_type const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    P2POverlayImpl& overlay)
    : Child(overlay)
    , p2pConfig_(p2pConfig)
    , sink_(p2pConfig_.logs().journal("Peer"), makePrefix(id))
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , response_(std::move(response))
    , id_(id)
    , journal_(sink_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(false)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , slot_(std::move(slot))
    , headers_(response_)
    , compressionEnabled_(
          peerFeatureEnabled(
              headers_,
              FEATURE_COMPR,
              "lz4",
              p2pConfig_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
    read_buffer_.commit(boost::asio::buffer_copy(
        read_buffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
}

P2PeerImp::~P2PeerImp()
{
    overlay_.peerFinder().on_closed(slot_);
}

void
P2PeerImp::run()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&P2PeerImp::run, shared_from_this()));

    onEvtRun();

    if (inbound_)
        doAccept();
    else
        doProtocolStart();

    // Anything else that needs to be done with the connection should be
    // done in doProtocolStart
}

void
P2PeerImp::stop()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&P2PeerImp::stop, shared_from_this()));
    if (socket_.is_open())
    {
        // The rationale for using different severity levels is that
        // outbound connections are under our control and may be logged
        // at a higher level, but inbound connections are more numerous and
        // uncontrolled so to prevent log flooding the severity is reduced.
        //
        if (inbound_)
        {
            JLOG(journal_.debug()) << "Stop";
        }
        else
        {
            JLOG(journal_.info()) << "Stop";
        }
    }
    close();
}

//------------------------------------------------------------------------------

void
P2PeerImp::send(std::shared_ptr<Message> const& m)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&P2PeerImp::send, shared_from_this(), m));
    if (gracefulClose_)
        return;
    if (detaching_)
        return;

    if (onEvtSendFilter(m))
        return;

    auto sendq_size = send_queue_.size();

    if (sendq_size < Tuning::targetSendQueue)
    {
        // To detect a peer that does not read from their
        // side of the connection, we expect a peer to have
        // a small senq periodically
        large_sendq_ = 0;
    }
    else if (auto sink = journal_.debug();
             sink && (sendq_size % Tuning::sendQueueLogFreq) == 0)
    {
        std::string const n = name();
        sink << (n.empty() ? remote_address_.to_string() : n)
             << " sendq: " << sendq_size;
    }

    send_queue_.push(m);

    if (sendq_size != 0)
        return;

    boost::asio::async_write(
        stream_,
        boost::asio::buffer(
            send_queue_.front()->getBuffer(compressionEnabled_)),
        bind_executor(
            strand_,
            std::bind(
                &P2PeerImp::onWriteMessage,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

std::string
P2PeerImp::getVersion() const
{
    if (inbound_)
        return headers_["User-Agent"].to_string();
    return headers_["Server"].to_string();
}

void
P2PeerImp::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        detaching_ = true;  // DEPRECATED
        error_code ec;
        // timer_.cancel(ec);
        onEvtClose();
        socket_.close(ec);
        if (inbound_)
        {
            JLOG(journal_.debug()) << "Closed";
        }
        else
        {
            JLOG(journal_.info()) << "Closed";
        }
    }
}

void
P2PeerImp::fail(std::string const& reason)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                (void (Peer::*)(std::string const&)) & P2PeerImp::fail,
                shared_from_this(),
                reason));
    if (journal_.active(beast::severities::kWarning) && socket_.is_open())
    {
        std::string const n = name();
        JLOG(journal_.warn()) << (n.empty() ? remote_address_.to_string() : n)
                              << " failed: " << reason;
    }
    close();
}

void
P2PeerImp::fail(std::string const& name, error_code ec)
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        JLOG(journal_.warn())
            << name << " from " << toBase58(TokenType::NodePublic, publicKey_)
            << " at " << remote_address_.to_string() << ": " << ec.message();
    }
    close();
}

void
P2PeerImp::gracefulClose()
{
    assert(strand_.running_in_this_thread());
    assert(socket_.is_open());
    assert(!gracefulClose_);
    gracefulClose_ = true;
#if 0
    // Flush messages
    while(send_queue_.size() > 1)
        send_queue_.pop_back();
#endif
    if (send_queue_.size() > 0)
        return;
    // setTimer();
    onEvtGracefulClose();
    stream_.async_shutdown(bind_executor(
        strand_,
        std::bind(
            &P2PeerImp::onShutdown,
            shared_from_this(),
            std::placeholders::_1)));
}

std::string
P2PeerImp::makePrefix(id_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

void
P2PeerImp::onShutdown(error_code ec)
{
    onEvtShutdown();
    // If we don't get eof then something went wrong
    if (!ec)
    {
        JLOG(journal_.error()) << "onShutdown: expected error condition";
        return close();
    }
    if (ec != boost::asio::error::eof)
        return fail("onShutdown", ec);
    close();
}

//------------------------------------------------------------------------------
void
P2PeerImp::doAccept()
{
    assert(read_buffer_.size() == 0);

    JLOG(journal_.debug()) << "doAccept: " << remote_address_;

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);

    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
        return fail("makeSharedValue: Unexpected failure");

    JLOG(journal_.info()) << "Protocol: " << to_string(protocol_);
    JLOG(journal_.info()) << "Public Key: "
                          << toBase58(TokenType::NodePublic, publicKey_);

    if (auto member = p2pConfig_.clusterMember(publicKey_))
    {
        {
            std::unique_lock lock{nameMutex_};
            name_ = *member;
        }
        JLOG(journal_.info()) << "Cluster name: " << *member;
    }

    // XXX Set timer: connection is in grace period to be useful.
    // XXX Set timer: connection idle (idle may vary depending on connection
    // type.)

    auto write_buffer = std::make_shared<boost::beast::multi_buffer>();

    boost::beast::ostream(*write_buffer) << makeResponse(
        !overlay_.peerFinder().config().peerPrivate,
        request_,
        overlay_.setup().public_ip,
        remote_address_.address(),
        *sharedValue,
        overlay_.setup().networkID,
        protocol_,
        p2pConfig_);

    // Write the whole buffer and only start protocol when that's done.
    boost::asio::async_write(
        stream_,
        write_buffer->data(),
        boost::asio::transfer_all(),
        bind_executor(
            strand_,
            [this, write_buffer, self = shared_from_this()](
                error_code ec, std::size_t bytes_transferred) {
                if (!socket_.is_open())
                    return;
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("onWriteResponse", ec);
                if (write_buffer->size() == bytes_transferred)
                    return doProtocolStart();
                return fail("Failed to write header");
            }));
}

std::string
P2PeerImp::name() const
{
    std::shared_lock read_lock{nameMutex_};
    return name_;
}

std::string
P2PeerImp::domain() const
{
    return headers_["Server-Domain"].to_string();
}

//------------------------------------------------------------------------------

// Protocol logic

void
P2PeerImp::doProtocolStart()
{
    onReadMessage(error_code(), 0);

    onEvtDoProtocolStart();
}

// Called repeatedly with protocol message data
void
P2PeerImp::onReadMessage(error_code ec, std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec == boost::asio::error::eof)
    {
        JLOG(journal_.info()) << "EOF";
        return gracefulClose();
    }
    if (ec)
        return fail("onReadMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onReadMessage: " << bytes_transferred << " bytes";
        else
            stream << "onReadMessage";
    }

    metrics_.recv.add_message(bytes_transferred);

    read_buffer_.commit(bytes_transferred);

    auto hint = Tuning::readBufferBytes;

    while (read_buffer_.size() > 0)
    {
        std::size_t bytes_consumed;
        std::tie(bytes_consumed, ec) =
            invokeProtocolMessage(read_buffer_.data(), *this, hint);
        if (ec)
            return fail("onReadMessage", ec);
        if (!socket_.is_open())
            return;
        if (gracefulClose_)
            return;
        if (bytes_consumed == 0)
            break;
        read_buffer_.consume(bytes_consumed);
    }

    // Timeout on writes only
    stream_.async_read_some(
        read_buffer_.prepare(std::max(Tuning::readBufferBytes, hint)),
        bind_executor(
            strand_,
            std::bind(
                &P2PeerImp::onReadMessage,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

void
P2PeerImp::onWriteMessage(error_code ec, std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
        return fail("onWriteMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onWriteMessage: " << bytes_transferred << " bytes";
        else
            stream << "onWriteMessage";
    }

    metrics_.sent.add_message(bytes_transferred);

    assert(!send_queue_.empty());
    send_queue_.pop();
    if (!send_queue_.empty())
    {
        // Timeout on writes only
        return boost::asio::async_write(
            stream_,
            boost::asio::buffer(
                send_queue_.front()->getBuffer(compressionEnabled_)),
            bind_executor(
                strand_,
                std::bind(
                    &P2PeerImp::onWriteMessage,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }

    if (gracefulClose_)
    {
        return stream_.async_shutdown(bind_executor(
            strand_,
            std::bind(
                &P2PeerImp::onShutdown,
                shared_from_this(),
                std::placeholders::_1)));
    }
}

void
P2PeerImp::Metrics::add_message(std::uint64_t bytes)
{
    using namespace std::chrono_literals;
    std::unique_lock lock{mutex_};

    totalBytes_ += bytes;
    accumBytes_ += bytes;
    auto const timeElapsed = clock_type::now() - intervalStart_;
    auto const timeElapsedInSecs =
        std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);

    if (timeElapsedInSecs >= 1s)
    {
        auto const avgBytes = accumBytes_ / timeElapsedInSecs.count();
        rollingAvg_.push_back(avgBytes);

        auto const totalBytes =
            std::accumulate(rollingAvg_.begin(), rollingAvg_.end(), 0ull);
        rollingAvgBytes_ = totalBytes / rollingAvg_.size();

        intervalStart_ = clock_type::now();
        accumBytes_ = 0;
    }
}

std::uint64_t
P2PeerImp::Metrics::average_bytes() const
{
    std::shared_lock lock{mutex_};
    return rollingAvgBytes_;
}

std::uint64_t
P2PeerImp::Metrics::total_bytes() const
{
    std::shared_lock lock{mutex_};
    return totalBytes_;
}

}  // namespace ripple
