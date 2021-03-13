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

#ifndef RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED

#include <ripple/basics/UnorderedContainers.h>
#include <ripple/overlay/impl/P2POverlayBaseImpl.h>
#include <boost/container/flat_map.hpp>
#include <unordered_map>

namespace ripple {

class BasicConfig;

template <typename OverlayImplmnt>
class P2POverlayImpl : public P2POverlayBaseImpl, public P2POverlayEvents
{
    friend OverlayImplmnt;
    using PeerImp_t = typename OverlayImplTraits<OverlayImplmnt>::PeerImp_t;

protected:
    boost::container::flat_map<
        P2POverlayBaseImpl::Child*,
        std::weak_ptr<P2POverlayBaseImpl::Child>>
        list_;
    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImp_t>>
        m_peers;
    hash_map<P2Peer::id_t, std::weak_ptr<PeerImp_t>> ids_;

public:
    P2POverlayImpl(
        Application& app,
        Setup const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    ~P2POverlayImpl();

    P2POverlayImpl(P2POverlayImpl<OverlayImplmnt> const&) = delete;
    P2POverlayImpl&
    operator=(P2POverlayImpl<OverlayImplmnt> const&) = delete;

    std::size_t
    size() const override;

    std::shared_ptr<P2Peer>
    findPeerByShortID(P2Peer::id_t const& id) const override;

    std::shared_ptr<P2Peer>
    findPeerByPublicKey(PublicKey const& pubKey) override;

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    void
    remove(std::shared_ptr<PeerFinder::Slot> const& slot) override;

    // Called when an active peer is destroyed.
    void
    onPeerDeactivate(P2Peer::id_t id) override;

    void
    addOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) override;

    void
    addInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) override;

    /** Called when a peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void
    activate(std::shared_ptr<PeerFinder::Slot> const& peer) override;

private:
    void
    add_active(std::shared_ptr<PeerImp_t> const& peer);

    virtual std::shared_ptr<PeerImp_t>
    mkOutboundPeer(
        std::unique_ptr<stream_type>&& stream_ptr,
        boost::beast::multi_buffer const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id) = 0;

    virtual std::shared_ptr<PeerImp_t>
    mkInboundPeer(
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr) = 0;

protected:
    //
    // Stoppable
    //

    void
    checkStopped();

    void
    onPrepare() override;

    void
    onStart() override;

    void
    onStop() override;

    void
    onChildrenStopped() override;

public:
    void
    remove(P2POverlayBaseImpl::Child& child) override;

protected:
    void
    stop() override;

    void
    addChild(std::shared_ptr<P2POverlayBaseImpl::Child> child) override;
};

//------------------------------------------------------------------------------

template <typename OverlayImplmnt>
P2POverlayImpl<OverlayImplmnt>::P2POverlayImpl(
    Application& app,
    Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : P2POverlayBaseImpl(
          app,
          setup,
          parent,
          serverHandler,
          resourceManager,
          resolver,
          io_service,
          config,
          collector)
{
}

template <typename OverlayImplmnt>
P2POverlayImpl<OverlayImplmnt>::~P2POverlayImpl()
{
    stop();

    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock<decltype(mutex_)> lock(mutex_);
    cond_.wait(lock, [this] { return list_.empty(); });
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::addChild(
    std::shared_ptr<P2POverlayBaseImpl::Child> child)
{
    std::lock_guard lock(mutex_);
    list_.emplace(child.get(), child);
}

//------------------------------------------------------------------------------

// Adds a peer that is already handshaked and active
template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::add_active(
    std::shared_ptr<PeerImp_t> const& peer)
{
    std::lock_guard lock(mutex_);

    {
        auto const result = m_peers.emplace(peer->slot(), peer);
        assert(result.second);
        (void)result.second;
    }

    {
        auto const result = ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->id()),
            std::make_tuple(peer));
        assert(result.second);
        (void)result.second;
    }

    list_.emplace(peer.get(), peer);

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // As we are not on the strand, run() must be called
    // while holding the lock, otherwise new I/O can be
    // queued after a call to stop().
    peer->run();
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::remove(
    std::shared_ptr<PeerFinder::Slot> const& slot)
{
    std::lock_guard lock(mutex_);
    auto const iter = m_peers.find(slot);
    assert(iter != m_peers.end());
    m_peers.erase(iter);
}

//------------------------------------------------------------------------------
//
// Stoppable
//
//------------------------------------------------------------------------------

// Caller must hold the mutex
template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::checkStopped()
{
    if (this->isStopping() && this->areChildrenStopped() && list_.empty())
        this->stopped();
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::onPrepare()
{
    PeerFinder::Config config = PeerFinder::Config::makeConfig(
        app_.config(),
        serverHandler_.setup().overlay.port,
        !app_.getValidationPublicKey().empty(),
        setup_.ipLimit);

    m_peerFinder->setConfig(config);

    // Populate our boot cache: if there are no entries in [ips] then we use
    // the entries in [ips_fixed].
    auto bootstrapIps =
        app_.config().IPS.empty() ? app_.config().IPS_FIXED : app_.config().IPS;

    // If nothing is specified, default to several well-known high-capacity
    // servers to serve as bootstrap:
    if (bootstrapIps.empty())
    {
        // Pool of servers operated by Ripple Labs Inc. - https://ripple.com
        bootstrapIps.push_back("r.ripple.com 51235");

        // Pool of servers operated by Alloy Networks - https://www.alloy.ee
        bootstrapIps.push_back("zaphod.alloy.ee 51235");

        // Pool of servers operated by ISRDC - https://isrdc.in
        bootstrapIps.push_back("sahyadri.isrdc.in 51235");
    }

    m_resolver.resolve(
        bootstrapIps,
        [this](
            std::string const& name,
            std::vector<beast::IP::Endpoint> const& addresses) {
            std::vector<std::string> ips;
            ips.reserve(addresses.size());
            for (auto const& addr : addresses)
            {
                if (addr.port() == 0)
                    ips.push_back(to_string(addr.at_port(DEFAULT_PEER_PORT)));
                else
                    ips.push_back(to_string(addr));
            }

            std::string const base("config: ");
            if (!ips.empty())
                m_peerFinder->addFallbackStrings(base + name, ips);
        });

    // Add the ips_fixed from the rippled.cfg file
    if (!app_.config().standalone() && !app_.config().IPS_FIXED.empty())
    {
        m_resolver.resolve(
            app_.config().IPS_FIXED,
            [this](
                std::string const& name,
                std::vector<beast::IP::Endpoint> const& addresses) {
                std::vector<beast::IP::Endpoint> ips;
                ips.reserve(addresses.size());

                for (auto& addr : addresses)
                {
                    if (addr.port() == 0)
                        ips.emplace_back(addr.address(), DEFAULT_PEER_PORT);
                    else
                        ips.emplace_back(addr);
                }

                if (!ips.empty())
                    m_peerFinder->addFixedPeer(name, ips);
            });
    }
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::onStart()
{
    onEvtStart();
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::onStop()
{
    strand_.dispatch(std::bind(&P2POverlayImpl<OverlayImplmnt>::stop, this));
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::onChildrenStopped()
{
    std::lock_guard lock(mutex_);
    checkStopped();
}

//------------------------------------------------------------------------------
/** A peer has connected successfully
This is called after the peer handshake has been completed and during
peer activation. At this point, the peer address and the public key
are known.
*/
template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::activate(
    std::shared_ptr<PeerFinder::Slot> const& slot)
{
    // Now track this peer
    std::shared_ptr<PeerImp_t> peer;
    {
        std::lock_guard lock(mutex_);
        auto it = m_peers.find(slot);
        assert(it != m_peers.end());
        peer = it->second.lock();
        assert(peer);
        auto const result(ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->id()),
            std::make_tuple(peer)));
        assert(result.second);
        (void)result.second;
    }

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // We just accepted this peer so we have non-zero active peers
    assert(size() != 0);
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::onPeerDeactivate(P2Peer::id_t id)
{
    std::lock_guard lock(mutex_);
    ids_.erase(id);
}

/** The number of active peers on the network
Active peers are only those peers that have completed the handshake
and are running the Ripple protocol.
*/
template <typename OverlayImplmnt>
std::size_t
P2POverlayImpl<OverlayImplmnt>::size() const
{
    std::lock_guard lock(mutex_);
    return ids_.size();
}

template <typename OverlayImplmnt>
std::shared_ptr<P2Peer>
P2POverlayImpl<OverlayImplmnt>::findPeerByShortID(P2Peer::id_t const& id) const
{
    std::lock_guard lock(mutex_);
    auto const iter = ids_.find(id);
    if (iter != ids_.end())
        return iter->second.lock();
    return {};
}

// A public key hash map was not used due to the peer connect/disconnect
// update overhead outweighing the performance of a small set linear search.
template <typename OverlayImplmnt>
std::shared_ptr<P2Peer>
P2POverlayImpl<OverlayImplmnt>::findPeerByPublicKey(PublicKey const& pubKey)
{
    std::lock_guard lock(mutex_);
    for (auto const& e : ids_)
    {
        if (auto peer = e.second.lock())
        {
            if (peer->getNodePublic() == pubKey)
                return peer;
        }
    }
    return {};
}

//------------------------------------------------------------------------------

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::remove(P2POverlayBaseImpl::Child& child)
{
    std::lock_guard lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        checkStopped();
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::stop()
{
    // Calling list_[].second->stop() may cause list_ to be modified
    // (P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::remove() may be called on
    // this same thread).  So iterating directly over list_ to call
    // child->stop() could lead to undefined behavior.
    //
    // Therefore we copy all of the weak/shared ptrs out of list_ before we
    // start calling stop() on them.  That guarantees
    // P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::remove() won't be called
    // until vector<> children leaves scope.
    std::vector<std::shared_ptr<P2POverlayBaseImpl::Child>> children;
    {
        std::lock_guard lock(mutex_);
        if (!work_)
            return;
        work_ = boost::none;

        children.reserve(list_.size());
        for (auto const& element : list_)
        {
            children.emplace_back(element.second.lock());
        }
    }  // lock released

    for (auto const& child : children)
    {
        if (child != nullptr)
            child->stop();
    }
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::addOutboundPeer(
    std::unique_ptr<stream_type>&& stream_ptr,
    boost::beast::multi_buffer const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id)
{
    auto peer = mkOutboundPeer(
        std::move(stream_ptr),
        buffers,
        std::move(slot),
        std::move(response),
        usage,
        publicKey,
        protocol,
        id);
    add_active(peer);
}

template <typename OverlayImplmnt>
void
P2POverlayImpl<OverlayImplmnt>::addInboundPeer(
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr)
{
    auto const peer = mkInboundPeer(
        id,
        slot,
        std::move(request),
        publicKey,
        protocol,
        consumer,
        std::move(stream_ptr));
    {
        // As we are not on the strand, run() must be called
        // while holding the lock, otherwise new I/O can be
        // queued after a call to stop().
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        {
            auto const result = m_peers.emplace(peer->slot(), peer);
            assert(result.second);
            (void)result.second;
        }
        list_.emplace(peer.get(), peer);

        peer->run();
    }
}

}  // namespace ripple

#endif
