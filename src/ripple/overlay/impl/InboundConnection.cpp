//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright(c) 2012-2021 Ripple Labs Inc.

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
//=============================================================================
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/InboundConnection.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/impl/PeerImp.h>

#include <boost/beast/core/ostream.hpp>

namespace ripple {

std::string
makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

InboundConnection::InboundConnection(
    Application& app,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& streamPtr,
    P2POverlayImpl& overlay)
    : Child(overlay)
    , app_(app)
    , id_(id)
    , sink_(app_.journal("InboundConnection"), makePrefix(id))
    , journal_(sink_)
    , streamPtr_(std::move(streamPtr))
    , socket_(streamPtr_->next_layer().socket())
    , stream_(*streamPtr_)
    , strand_(socket_.get_executor())
    , remoteAddress_(slot->remote_endpoint())
    , protocol_(protocol)
    , publicKey_(publicKey)
    , usage_(consumer)
    , slot_(slot)
    , request_(std::move(request))
{
}

void
InboundConnection::run()
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&InboundConnection::run, shared_from_this()));
    sendResponse();
}

void
InboundConnection::stop()
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&InboundConnection::stop, shared_from_this()));
    if (socket_.is_open())
    {
        JLOG(journal_.debug()) << "Stop";
    }
    close();
}

void
InboundConnection::sendResponse()
{
    auto const sharedValue = makeSharedValue(*streamPtr_, journal_);
    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
        return fail("makeSharedValue: Unexpected failure");

    JLOG(journal_.info()) << "Protocol: " << to_string(protocol_);
    JLOG(journal_.info()) << "Public Key: "
                          << toBase58(TokenType::NodePublic, publicKey_);

    auto write_buffer = std::make_shared<boost::beast::multi_buffer>();

    boost::beast::ostream(*write_buffer) << makeResponse(
        !overlay_.peerFinder().config().peerPrivate,
        request_,
        overlay_.setup().public_ip,
        remoteAddress_.address(),
        *sharedValue,
        overlay_.setup().networkID,
        protocol_,
        app_);

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
                    return startProtocol();
                return fail("Failed to write header");
            }));
}

void
InboundConnection::fail(std::string const& name, error_code const& ec)
{
    if (socket_.is_open())
    {
        JLOG(journal_.warn())
            << name << " from " << toBase58(TokenType::NodePublic, publicKey_)
            << " at " << remoteAddress_.to_string() << ": " << ec.message();
    }
    close();
}

void
InboundConnection::fail(std::string const& reason)
{
    if (journal_.active(beast::severities::kWarning) && socket_.is_open())
    {
        auto const n = app_.cluster().member(publicKey_);
        JLOG(journal_.warn())
            << (n ? remoteAddress_.to_string() : *n) << " failed: " << reason;
    }
    close();
}

void
InboundConnection::close()
{
    if (socket_.is_open())
    {
        socket_.close();
        JLOG(journal_.debug()) << "Closed";
    }
}

void
InboundConnection::startProtocol()
{
    overlay_.addInboundPeer(
        id_,
        slot_,
        std::move(request_),
        publicKey_,
        protocol_,
        usage_,
        std::move(streamPtr_));
}

}  // namespace ripple