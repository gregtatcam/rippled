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

#ifndef RIPPLE_DEFAULTOVERLAYIMPL_TESTS_H_INCLUDED
#define RIPPLE_DEFAULTOVERLAYIMPL_TESTS_H_INCLUDED

#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>

namespace ripple {

template <typename PeerImp_t>
class DefaultPeerImp : public P2PeerImp<PeerImp_t>
{
    friend class P2PeerImp<PeerImp_t>;

public:
    virtual ~DefaultPeerImp() = default;
    DefaultPeerImp(
        Logs& logs,
        std::unique_ptr<stream_type>&& stream_ptr,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        bool compressionEnabled,
        P2POverlayImpl& overlay)
        : P2PeerImp<PeerImp_t>(
              logs,
              std::move(stream_ptr),
              std::move(slot),
              std::move(response),
              publicKey,
              protocol,
              id,
              compressionEnabled,
              overlay)
    {
    }
    DefaultPeerImp(
        Logs& logs,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        bool compressionEnabled,
        P2POverlayImpl& overlay)
        : P2PeerImp<PeerImp_t>(
              logs,
              id,
              slot,
              std::move(request),
              publicKey,
              protocol,
              std::move(stream_ptr),
              compressionEnabled,
              overlay)
    {
    }
    // Peer's interface
    void
    charge(Resource::Charge const&) override
    {
    }
    bool
    cluster() const override
    {
        return false;
    }
    bool
    isHighLatency() const override
    {
        return false;
    }
    int
    getScore(bool) const override
    {
        return 0;
    }
    Json::Value
    json() override
    {
        return {};
    }
    bool supportsFeature(ProtocolFeature) const override
    {
        return false;
    }
    std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return std::nullopt;
    }
    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }
    uint256 const&
    getClosedLedgerHash() const override
    {
        static uint256 h{0};
        return h;
    }
    bool
    hasLedger(uint256 const&, std::uint32_t) const override
    {
        return false;
    }
    void
    ledgerRange(std::uint32_t&, std::uint32_t&) const override
    {
    }
    bool hasShard(std::uint32_t) const override
    {
        return false;
    }
    bool
    hasTxSet(uint256 const&) const override
    {
        return false;
    }
    void
    cycleStatus() override
    {
    }
    bool hasRange(std::uint32_t, std::uint32_t) override
    {
        return false;
    }

private:
    void
    onEvtRun()
    {
    }
    bool
    filter(std::shared_ptr<Message> const&)
    {
        return false;
    }
    void
    onEvtClose()
    {
    }
    void
    onEvtGracefulClose()
    {
    }
    void
    onEvtShutdown()
    {
    }
    void
    onEvtProtocolStart()
    {
    }
};

class DefaultOverlayImpl : public P2POverlayImpl
{
public:
    virtual ~DefaultOverlayImpl() = default;

    DefaultOverlayImpl(
        P2PConfig const& p2pConfig,
        Setup const& setup,
        Stoppable& parent,
        std::uint16_t overlayPort,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector)
        : P2POverlayImpl(
              p2pConfig,
              setup,
              parent,
              overlayPort,
              resourceManager,
              resolver,
              io_service,
              config,
              collector)
    {
    }

    std::size_t
    size() const override
    {
        return 0;
    }

    Json::Value
    json() override
    {
        return {};
    }

    PeerSequence
    getActivePeers() const override
    {
        return {};
    }

    void checkTracking(std::uint32_t) override
    {
    }

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const&) const override
    {
        return {};
    }

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const&) override
    {
        return {};
    }

    void
    broadcast(protocol::TMProposeSet&) override
    {
    }

    void
    broadcast(protocol::TMValidation&) override
    {
    }

    std::set<Peer::id_t>
    relay(protocol::TMProposeSet&, uint256 const&, PublicKey const&) override
    {
        return {};
    }

    std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) override
    {
        return {};
    }

    void
    incJqTransOverflow() override
    {
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return 0;
    }

    void
    incPeerDisconnectCharges() override
    {
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return 0;
    }

    Json::Value
    crawlShards(bool pubKey, std::uint32_t hops) override
    {
        return {};
    }

    std::optional<std::uint32_t>
    networkID() const override
    {
        return {};
    }
};

}  // namespace ripple

#endif  // RIPPLE_DEFAULTOVERLAYIMPL_TESTS_H_INCLUDED
