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
#include <ripple/basics/ResolverAsio.h>
#include <ripple/beast/unit_test.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/impl/P2PeerImp.h>
#include <ripple/overlay/make_Overlay.h>
#include <test/jtx/Env.h>

namespace ripple {

namespace test {

class PeerImpTest : public P2PeerImp
{
public:
    PeerImpTest(
        Application& app,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              app,
              id,
              slot,
              std::move(request),
              publicKey,
              protocol,
              std::move(stream_ptr),
              overlay)
    {
    }

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    PeerImpTest(
        Application& app,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        P2POverlayImpl& overlay)
        : P2PeerImp(
              app,
              std::move(stream_ptr),
              buffers,
              std::move(slot),
              std::move(response),
              publicKey,
              protocol,
              id,
              overlay)
    {
    }

protected:
    bool
    squelched(std::shared_ptr<Message> const&) override
    {
        return false;
    }

    void
    onEvtProtocolStart() override
    {
    }

    void
    onEvtRun() override
    {
    }

    void
    onEvtClose() override
    {
    }

    void
    onEvtGracefulClose() override
    {
    }

    void
    onEvtShutdown() override
    {
    }

    std::pair<size_t, boost::system::error_code>
    onEvtProtocolMessage(boost::beast::multi_buffer const&, size_t&) override
    {
        return {};
    }
};

class OverlayImplTest : public P2POverlayImpl
{
public:
    OverlayImplTest(
        Application& app,
        Setup const& setup,
        Stoppable& parent,
        std::uint16_t overlayPort,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector)
        : P2POverlayImpl(
              app,
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

protected:
    bool
    processRequest(http_request_type const& req, Handoff& handoff) override
    {
        return false;
    }

    std::shared_ptr<P2PeerImp>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        Resource::Consumer consumer,
        ProtocolVersion protocol,
        std::unique_ptr<stream_type>&& stream_ptr) override
    {
        auto peer = std::make_shared<PeerImpTest>(
            this->app(),
            id,
            slot,
            std::move(request),
            publicKey,
            protocol,
            std::move(stream_ptr),
            *this);
        return peer;
    }

    std::shared_ptr<P2PeerImp>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) override
    {
        auto peer = std::make_shared<PeerImpTest>(
            this->app(),
            std::move(stream_ptr),
            buffers.data(),
            std::move(slot),
            std::move(response),
            publicKey,
            protocol,
            id,
            *this);
        return peer;
    }

    void
    onPeerDeactivate(
        P2Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot) override
    {
    }
};

class overlay_test : public beast::unit_test::suite
{
    std::unique_ptr<P2POverlayImpl> overlay1_;
    std::unique_ptr<P2POverlayImpl> overlay2_;
    jtx::Env env_;

public:
    overlay_test() : env_(*this)
    {
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        auto resolver = ResolverAsio::New(
                env_.app().getIOService(), env_.app().journal("OverlayTest"));
        overlay1_ = std::make_unique<OverlayImplTest>(
            env_.app(),
            setup_Overlay(env_.app().config()),
            env_.app().getJobQueue(),
            5005,
            env_.app().getResourceManager(),
            *resolver,
            env_.app().getIOService(),
            env_.app().config(),
            env_.app().getCollectorManager().collector());
        BEAST_EXPECT(1);
    }

    void
    run() override
    {
        testOverlay();
    }
};

BEAST_DEFINE_TESTSUITE(overlay, ripple_data, ripple);

}  // namespace test

}  // namespace ripple