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

#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/Message.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>

namespace ripple {

P2PeerImp::P2PeerImp(
    Application& app,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    std::unique_ptr<stream_type>&& stream_ptr,
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
    , inbound_(true)
    , protocol_(protocol)
    , trackingTime_(clock_type::now())
    , publicKey_(publicKey)
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , slot_(slot)
    , request_(std::move(request))
    , headers_(request_)
    , compressionEnabled_(
        headers_["X-Offer-Compression"] == "lz4" ? Compressed::On
                                                 : Compressed::Off)
{
}

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

    auto validator = m->getValidatorKey();
    if (validator && squelch_.isSquelched(*validator))
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
    else if (
        journal_.active(beast::severities::kDebug) &&
        (sendq_size % Tuning::sendQueueLogFreq) == 0)
    {
        std::string const n = name();
        JLOG(journal_.debug()) << (n.empty() ? remote_address_.to_string() : n)
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

bool
P2PeerImp::crawl() const
{
    auto const iter = headers_.find("Crawl");
    if (iter == headers_.end())
        return false;
    return boost::iequals(iter->value(), "public");
}

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


Json::Value
P2PeerImp::json()
{
    Json::Value ret(Json::objectValue);

    ret[jss::public_key] = toBase58(TokenType::NodePublic, publicKey_);
    ret[jss::address] = remote_address_.to_string();

    if (inbound_)
        ret[jss::inbound] = true;

    if (cluster())
    {
        ret[jss::cluster] = true;

        if (auto const n = name(); !n.empty())
            // Could move here if Json::Value supported moving from a string
            ret[jss::name] = n;
    }

    if (auto const d = domain(); !d.empty())
        ret[jss::server_domain] = domain();

    if (auto const nid = headers_["Network-ID"].to_string(); !nid.empty())
        ret[jss::network_id] = nid;

    if (auto const version = getVersion(); !version.empty())
        ret[jss::version] = version;

    ret[jss::protocol] = to_string(protocol_);

    {
        std::lock_guard sl(recentLock_);
        if (latency_)
            ret[jss::latency] = static_cast<Json::UInt>(latency_->count());
    }

    ret[jss::uptime] = static_cast<Json::UInt>(
        std::chrono::duration_cast<std::chrono::seconds>(uptime()).count());

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

bool
P2PeerImp::supportsFeature(ProtocolFeature f) const
{
    switch (f)
    {
        case ProtocolFeature::ValidatorListPropagation:
            return protocol_ >= make_protocol(2, 1);
    }
    return false;
}

void
P2PeerImp::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        detaching_ = true;  // DEPRECATED
        error_code ec;
        cancelTimer();
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
                (void(P2PeerImp::*)(std::string const&)) & P2PeerImp::fail,
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
    setTimer();
    stream_.async_shutdown(bind_executor(
        strand_,
        std::bind(
            &P2PeerImp::onShutdown, shared_from_this(), std::placeholders::_1)));
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
    cancelTimer();
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

    if (auto member = app_.cluster().member(publicKey_))
    {
        {
            std::unique_lock lock{nameMutex_};
            name_ = *member;
        }
        JLOG(journal_.info()) << "Cluster name: " << *member;
    }

    // TBD
    //overlay_.activate(shared_from_this());

    // XXX Set timer: connection is in grace period to be useful.
    // XXX Set timer: connection idle (idle may vary depending on connection
    // type.)

    auto write_buffer = [this, sharedValue]() {
      auto buf = std::make_shared<boost::beast::multi_buffer>();

      http_response_type resp;
      resp.result(boost::beast::http::status::switching_protocols);
      resp.version(request_.version());
      resp.insert("Connection", "Upgrade");
      resp.insert("Upgrade", to_string(protocol_));
      resp.insert("Connect-As", "Peer");
      resp.insert("Server", BuildInfo::getFullVersionString());
      resp.insert(
          "Crawl",
          overlay_.peerFinder().config().peerPrivate ? "private" : "public");

      if (request_["X-Offer-Compression"] == "lz4" &&
          app_.config().COMPRESSION)
          resp.insert("X-Offer-Compression", "lz4");

      buildHandshake(
          resp,
          *sharedValue,
          overlay_.setup().networkID,
          overlay_.setup().public_ip,
          remote_address_.address(),
          app_);

      boost::beast::ostream(*buf) << resp;

      return buf;
    }();

    // Write the whole buffer and only start protocol when that's done.
    boost::asio::async_write(
        stream_,
        write_buffer->data(),
        boost::asio::transfer_all(),
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
        });
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

void
P2PeerImp::doProtocolStart()
{
    onReadMessage(error_code(), 0);

    // Send all the validator lists that have been loaded
    if (supportsFeature(ProtocolFeature::ValidatorListPropagation))
    {
        app_.validators().for_each_available([&](std::string const& manifest,
                                                 std::string const& blob,
                                                 std::string const& signature,
                                                 std::uint32_t version,
                                                 PublicKey const& pubKey,
                                                 std::size_t sequence,
                                                 uint256 const& hash) {
          protocol::TMValidatorList vl;

          vl.set_manifest(manifest);
          vl.set_blob(blob);
          vl.set_signature(signature);
          vl.set_version(version);

          JLOG(p_journal_.debug())
              << "Sending validator list for " << strHex(pubKey)
              << " with sequence " << sequence << " to "
              << remote_address_.to_string() << " (" << id_ << ")";
          send(std::make_shared<Message>(vl, protocol::mtVALIDATORLIST));
          // Don't send it next time.
          app_.getHashRouter().addSuppressionPeer(hash, id_);
          setPublisherListSequence(pubKey, sequence);
        });
    }

    if (auto m = overlay_.getManifestsMessage())
        send(m);
}

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
        // TBD
        //std::tie(bytes_consumed, ec) =
        //    invokeProtocolMessage(read_buffer_.data(), *this, hint);
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

int
P2PeerImp::getScore(bool haveItem) const
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

    boost::optional<std::chrono::milliseconds> latency;
    {
        std::lock_guard sl(recentLock_);
        latency = latency_;
    }

    if (latency)
        score -= latency->count() * spLatency;
    else
        score -= spNoLatency;

    return score;
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

} // ripple