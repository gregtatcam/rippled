//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_OVERLAY_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAY_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/overlay/P2POverlay.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/server/Handoff.h>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

namespace boost {
namespace asio {
namespace ssl {
class context;
}
}  // namespace asio
}  // namespace boost

namespace ripple {

/** Manages the set of connected peers. */
class Overlay
{
protected:
    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             Stoppable and PropertyStream
    //
    Overlay(Stoppable& parent)
    {
    }

public:
    enum class Promote { automatic, never, always };

    using PeerSequence = std::vector<std::shared_ptr<Peer>>;

    virtual ~Overlay() = default;

    /** Returns the number of active peers.
        Active peers are only those peers that have completed the
        handshake and are using the peer protocol.
    */
    virtual std::size_t
    size() const = 0;

    /** Return diagnostics on the status of all peers.
        @deprecated This is superceded by PropertyStream
    */
    virtual Json::Value
    json() = 0;

    /** Returns a sequence representing the current list of peers.
        The snapshot is made at the time of the call.
    */
    virtual PeerSequence
    getActivePeers() const = 0;

    /** Calls the checkTracking function on each peer
        @param index the value to pass to the peer's checkTracking function
    */
    virtual void
    checkTracking(std::uint32_t index) = 0;

    /** Broadcast a proposal. */
    virtual void
    broadcast(protocol::TMProposeSet& m) = 0;

    /** Broadcast a validation. */
    virtual void
    broadcast(protocol::TMValidation& m) = 0;

    /** Relay a proposal.
     * @param m the serialized proposal
     * @param uid the id used to identify this proposal
     * @param validator The pubkey of the validator that issued this proposal
     * @return the set of peers which have already sent us this proposal
     */
    virtual std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) = 0;

    /** Relay a validation.
     * @param m the serialized validation
     * @param uid the id used to identify this validation
     * @param validator The pubkey of the validator that issued this validation
     * @return the set of peers which have already sent us this validation
     */
    virtual std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) = 0;

    /** Visit every active peer.
     *
     * The visitor must be invocable as:
     *     Function(std::shared_ptr<Peer> const& peer);
     *
     * @param f the invocable to call with every peer
     */
    template <class Function>
    void
    foreach(Function f) const
    {
        for (auto const& p : getActivePeers())
            f(p);
    }

    /** Returns the peer with the matching short id, or null. */
    virtual std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const = 0;

    /** Returns the peer with the matching public key, or null. */
    virtual std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) = 0;

    /** Increment and retrieve counter for transaction job queue overflows. */
    virtual void
    incJqTransOverflow() = 0;
    virtual std::uint64_t
    getJqTransOverflow() const = 0;

    virtual void
    incPeerDisconnectCharges() = 0;
    virtual std::uint64_t
    getPeerDisconnectCharges() const = 0;

    /** Returns information reported to the crawl shard RPC command.

        @param hops the maximum jumps the crawler will attempt.
        The number of hops achieved is not guaranteed.
    */
    virtual Json::Value
    crawlShards(bool pubKey, std::uint32_t hops) = 0;

    virtual P2POverlay&
    p2p() = 0;
};

}  // namespace ripple

#endif
