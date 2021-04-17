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

//#include <ripple/basics/UptimeClock.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/random.h>
#include <ripple/basics/safe_cast.h>
//#include <ripple/beast/core/LexicalCast.h>
//#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/digest.h>

//#include <boost/algorithm/clamp.hpp>
//#include <boost/algorithm/string.hpp>
//#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast/core/ostream.hpp>

#include <algorithm>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>

using namespace std::chrono_literals;

namespace ripple {

P2PeerImp::P2PeerImp(
    Application& app,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    std::unique_ptr<stream_type>&& stream_ptr,
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
    , inbound_(true)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , slot_(slot)
    , request_(std::move(request))
    , headers_(request_)
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
    JLOG(journal_.debug()) << " compression enabled "
                           << (compressionEnabled_ == Compressed::On) << " on "
                           << remote_address_ << " " << id_;
}

P2PeerImp::~P2PeerImp()
{
    const bool inCluster{cluster()};

    overlay_.onPeerDistruct(id_, slot_);

    if (inCluster)
    {
        JLOG(journal_.warn()) << name() << " left cluster";
    }
}

void
P2PeerImp::run()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&P2PeerImp::run, shared_from_this()));

    onEvtRun();

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

    if (squelched(m))
        return;

    overlay_.reportTraffic(
        safe_cast<TrafficCount::category>(m->getCategory()),
        false,
        static_cast<int>(m->getBuffer(compressionEnabled_).size()));

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

//------------------------------------------------------------------------------

bool
P2PeerImp::cluster() const
{
    return static_cast<bool>(app_.cluster().member(publicKey_));
}

std::string
P2PeerImp::getVersion() const
{
    if (inbound_)
        return headers_["User-Agent"].to_string();
    return headers_["Server"].to_string();
}

//------------------------------------------------------------------------------

void
P2PeerImp::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        detaching_ = true;  // DEPRECATED
        error_code ec;
        onEvtClose();
        socket_.close(ec);
        overlay_.incPeerDisconnect();
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
                (void (P2PeerImp::*)(std::string const&)) & P2PeerImp::fail,
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
    onEvtGracefulClose();
    stream_.async_shutdown(bind_executor(
        strand_,
        std::bind(
            &P2PeerImp::onShutdown,
            shared_from_this(),
            std::placeholders::_1)));
}

//------------------------------------------------------------------------------

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

Json::Value
P2PeerImp::json()
{
    Json::Value ret(Json::objectValue);

    ret[jss::metrics] = Json::Value(Json::objectValue);
    ret[jss::metrics][jss::total_bytes_recv] =
        std::to_string(metrics_.recv.total_bytes());
    ret[jss::metrics][jss::total_bytes_sent] =
        std::to_string(metrics_.sent.total_bytes());
    ret[jss::metrics][jss::avg_bps_recv] =
        std::to_string(metrics_.recv.average_bytes());
    ret[jss::metrics][jss::avg_bps_sent] =
        std::to_string(metrics_.sent.average_bytes());

    return ret;
}

//------------------------------------------------------------------------------

void
P2PeerImp::doProtocolStart()
{
    onReadMessage(error_code(), 0);

    onEvtProtocolStart();
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
        std::tie(bytes_consumed, ec) = onEvtProtocolMessage(read_buffer_, hint);
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
