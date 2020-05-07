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

#ifndef RIPPLE_OVERLAY_SLOT_H_INCLUDED
#define RIPPLE_OVERLAY_SLOT_H_INCLUDED

#include <ripple/overlay/Squelch.h>
#include <ripple/overlay/SquelchCommon.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple.pb.h>

namespace ripple {

namespace Squelch {

inline bool gLog = false;

template <typename Peer, typename clock_type>
class Slots;

/** Peer's State */
enum class PeerState : uint8_t {
    Counting = 0x01,   // counting messages
    Selected = 0x02,   // selected to receive, could be counting too
    Squelched = 0x03,  // squelched
};
/** Slot's State */
enum class SlotState : uint8_t {
    Counting = 0x01,  // counting messages
    Selected = 0x02,  // peers selected, stop counting
};

/** Maintains Validation and Propose Set message count for specific validator
 * from the upstream peers.
 */
template <typename Peer, typename clock_type>
class Slot final
{
private:
    friend class Slots<Peer, clock_type>;
    using id_t = typename Peer::id_t;
    using time_point = typename clock_type::time_point;

    Slot() : lastSelected_(clock_type::now()), state_(SlotState::Counting)
    {
    }

    /** Update peer info. If the message is from a new
     * peer or from a previously expired squelched peer then switch
     * the peer's and slot's state to Counting. If the number of messages
     * for the peer is COUNT_THRESHOLD then set the peer's state
     * to Selected. If the number of selected peers is MAX_PEERS then
     * call f() for each peer, which is not selected and not already
     * in Squelched state. Set the state for those peers to Squelched
     * and reset the count of all peers. Set slot's state to Selected.
     * Message count is not updated when the slot is in Selected state.
     * @param id Peer id which received the message
     * @param peerPtr Peer which received the message
     * @param type  Message type (Validation and Propose Set only,
     *     others are ignored)
     * @param f Function is called for every peer that has to be
     *     squelched with peer's
     * weak_ptr as the argument
     */
    template <typename F>
    void
    update(
        const id_t& id,
        std::weak_ptr<Peer> peerPtr,
        protocol::MessageType type,
        F&& f);

    /** Handle peer deletion. If the peer is in Selected state then
     * call f() for every peer in squelched state and reset every
     * peer's state to Counting. Switch Slot's state to Counting.
     * @param id Deleted peer id
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     * @param delSelected If true then only delete if peer in Selected state
     */
    template <typename F>
    void
    deletePeer(id_t const& id, F&& f, bool delSelected);

    /** Get the time of the last peers selection round */
    const time_point&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /** Return if slot is in counting state and if all counts
     * of unsquelched peers are set to 0 */
    std::pair<bool, bool>
    isCountingState() const;

    /** Return number of peers in the state, or not in state */
    template <typename Comp = std::equal_to<>>
    std::uint16_t
    inState(PeerState state, Comp comp = {}) const;

    /** Return selected peers */
    std::set<id_t>
    getSelected() const;

    /** Get peers info. Return map of peer's state, count, and squelch
     * expiration milliseconds.
     */
    std::unordered_map<id_t, std::tuple<PeerState, uint16_t, uint32_t>>
    getPeers();

private:
    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts();

    /** Initialize slot to Counting state */
    void
    initCounting();

    /** Accumulate value T over peers. F(T&, id_t const&, PeerInfo const&) */
    template <typename T, typename F>
    T
    accumulate(T t, F&& f) const;

    /** Data maintained for each peer */
    struct PeerInfo
    {
        std::weak_ptr<Peer> peer_;  // peer's weak_ptr passed to callbacks
        PeerState state_;           // peer's state
        std::size_t count_;         // message's count
        time_point expire_;         // squelch expiration time
    };
    std::unordered_map<id_t, PeerInfo> peers_;  // peer's data
    std::unordered_map<id_t, bool> selected_;   // peers selected as the
                                                // source of messages from
                                                // validator
    typename clock_type::time_point
        lastSelected_;  // last time peers were selected,
                        // used to age the slot
    SlotState state_;   // slot's state
};

template <typename Peer, typename clock_type>
template <typename F>
void
Slot<Peer, clock_type>::update(
    id_t const& id,
    std::weak_ptr<Peer> peerPtr,
    protocol::MessageType type,
    F&& f)
{
    auto it = peers_.find(id);
    // First time message from this peer.
    if (it == peers_.end())
    {
        peers_.emplace(std::make_pair(
            id,
            std::move(
                PeerInfo{peerPtr, PeerState::Counting, 0, clock_type::now()})));
        initCounting();
        return;
    }
    // Message from a peer with expired squelch
    else if (
        it->second.state_ == PeerState::Squelched &&
        clock_type::now() > it->second.expire_)
    {
        it->second.state_ = PeerState::Counting;
        initCounting();
        return;
    }

    auto& peer = it->second;

    if (state_ != SlotState::Counting || peer.state_ == PeerState::Squelched)
        return;

    if (++peer.count_ > MESSAGE_COUNT_THRESHOLD)
        selected_[id] = true;

    if (selected_.size() == MAX_SELECTED_PEERS)
    {
        lastSelected_ = clock_type::now();

        for (auto& [k, v] : peers_)
        {
            v.count_ = 0;

            if (selected_.find(k) != selected_.end())
                v.state_ = PeerState::Selected;
            else if (v.state_ != PeerState::Squelched)
            {
                v.state_ = PeerState::Squelched;
                auto duration = Squelch<clock_type>::getSquelchDuration();
                v.expire_ = lastSelected_ + duration;
                f(v.peer_, duration_cast<milliseconds>(duration).count());
            }
        }
        selected_.clear();
        state_ = SlotState::Selected;
    }
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slot<Peer, clock_type>::deletePeer(id_t const& id, F&& f, bool delSelected)
{
    auto it = peers_.find(id);
    if (it != peers_.end())
    {
        if (it->second.state_ == PeerState::Selected)
        {
            auto now = clock_type::now();
            for (auto& [k, v] : peers_)
            {
                if (v.state_ == PeerState::Squelched)
                    f(v.peer_);
                v.state_ = PeerState::Counting;
                v.count_ = 0;
                v.expire_ = now;
            }

            selected_.clear();
            state_ = SlotState::Counting;
            delSelected = false;
        }
        if (!delSelected)
            peers_.erase(id);
    }
}

template <typename Peer, typename clock_type>
void
Slot<Peer, clock_type>::resetCounts()
{
    for (auto& [id, peer] : peers_)
        peer.count_ = 0;
}

template <typename Peer, typename clock_type>
void
Slot<Peer, clock_type>::initCounting()
{
    state_ = SlotState::Counting;
    selected_.clear();
    resetCounts();
}

template <typename Peer, typename clock_type>
template <typename T, typename F>
T
Slot<Peer, clock_type>::accumulate(T t, F&& f) const
{
    return std::accumulate(
        peers_.begin(), peers_.end(), t, [&](T& t, auto const& it) {
            return f(t, it.first, it.second);
        });
}

template <typename Peer, typename clock_type>
std::pair<bool, bool>
Slot<Peer, clock_type>::isCountingState() const
{
    auto resetCounts =
        accumulate(0, [](int& init, id_t const&, PeerInfo const& peer) {
            return init += peer.count_;
        });
    return std::make_pair(state_ == SlotState::Counting, resetCounts == 0);
}

template <typename Peer, typename clock_type>
template <typename Comp>
std::uint16_t
Slot<Peer, clock_type>::inState(PeerState state, Comp comp) const
{
    return accumulate(0, [&](int& init, id_t const& id, PeerInfo const& peer) {
        if (comp(peer.state_, state))
            return ++init;
        return init;
    });
}

template <typename Peer, typename clock_type>
std::set<typename Peer::id_t>
Slot<Peer, clock_type>::getSelected() const
{
    std::set<id_t> init;
    return accumulate(
        init, [](auto& init, id_t const& id, PeerInfo const& peer) {
            if (peer.state_ == PeerState::Selected)
            {
                init.insert(id);
                return init;
            }
            return init;
        });
}

template <typename Peer, typename clock_type>
std::unordered_map<
    typename Peer::id_t,
    std::tuple<PeerState, uint16_t, uint32_t>>
Slot<Peer, clock_type>::getPeers()
{
    auto init = std::unordered_map<
        id_t,
        std::tuple<PeerState, std::uint16_t, std::uint32_t>>();
    return accumulate(
        init, [](auto& init, id_t const& id, PeerInfo const& peer) {
            init.emplace(std::make_pair(
                id,
                std::make_tuple(
                    peer.state_,
                    peer.count_,
                    duration_cast<milliseconds>(peer.expire_.time_since_epoch())
                        .count())));
            return init;
        });
}

template <typename Peer, typename clock_type>
class Slots final
{
    using time_point = typename clock_type::time_point;
    using id_t = typename Peer::id_t;

public:
    Slots() = default;
    ~Slots() = default;
    /** Updates message count for validator/peer. Sends TMSquelch if the number
     * of messages for N peers reaches threshold T.
     * @param validator Validator's public key
     * @param id Peer's id which received the message
     * @param id Peer's pointer which received the message
     * @param type Received protocol message type
     * @param f Function is called for every peer that has to be
     *     squelched with peer's
     */
    template <typename F>
    void
    checkForSquelch(
        PublicKey const& validator,
        id_t const& id,
        std::weak_ptr<Peer> peer,
        protocol::MessageType type,
        F&& f);

    /** Called when the peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     */
    template <typename F>
    void
    unsquelch(id_t const& id, F&& f);

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     */
    template <typename F>
    void
    checkIdle(F&& f);

    /** Update last time peer received a message */
    void
    touch(const id_t&);

    /** Returns if slot is in counting state and if all counts
     * of unsquelched peers are set to 0 */
    std::pair<bool, bool>
    isCountingState(PublicKey const& validator) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.isCountingState();
        return std::make_pair(false, false);
    }

    /** Return number of peers in state, or not in state */
    template <typename Comp = std::equal_to<>>
    boost::optional<std::uint16_t>
    inState(PublicKey const& validator, PeerState state, Comp comp = {}) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.inState(state, comp);
        return {};
    }

    /** Get selected peers */
    std::set<id_t>
    getSelected(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getSelected();
        return {};
    }

    /** Get peers info. Return map of peer's state, count, and squelch
     * expiration milliseconds.
     */
    std::unordered_map<
        typename Peer::id_t,
        std::tuple<PeerState, uint16_t, uint32_t>>
    getPeers(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getPeers();
        return {};
    }

    /** Get last message info for each peer. */
    std::unordered_map<id_t, time_point> const&
    messageTime() const
    {
        return messageTime_;
    }

private:
    /** Called when the peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     * @param delSelected If true then only delete if peer in Selected state
     */
    template <typename F>
    void
    deletePeer(id_t const& id, F&& f, bool delSelected = false);

    hash_map<PublicKey, Slot<Peer, clock_type>> slots_;
    std::unordered_map<id_t, time_point> messageTime_;
};

template <typename Peer, typename clock_type>
template <typename F>
void
Slots<Peer, clock_type>::checkForSquelch(
    const PublicKey& validator,
    id_t const& id,
    std::weak_ptr<Peer> peerPtr,
    protocol::MessageType type,
    F&& f)
{
    touch(id);

    auto& peer =
        [&]() {
            auto it = slots_.find(validator);
            if (it == slots_.end())
            {
                auto [it, b] = slots_.emplace(
                    std::make_pair(validator, Slot<Peer, clock_type>()));
                return it;
            }
            else
                return it;
        }()
            ->second;

    peer.update(
        id,
        peerPtr,
        type,
        [&validator, f](std::weak_ptr<Peer> peerPtr, uint32_t squelchDuration) {
            f(validator, peerPtr, squelchDuration);
        });
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slots<Peer, clock_type>::deletePeer(id_t const& id, F&& f, bool delSelected)
{
    for (auto& it : slots_)
        it.second.deletePeer(
            id,
            [&](std::weak_ptr<Peer> peer) { f(it.first, peer); },
            delSelected);
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slots<Peer, clock_type>::unsquelch(id_t const& id, F&& f)
{
    deletePeer(id, f);
    messageTime_.erase(id);
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slots<Peer, clock_type>::checkIdle(F&& f)
{
    auto now = clock_type::now();
    for (auto it = messageTime_.begin(); it != messageTime_.end();)
    {
        if (now - it->second > IDLED)
        {
            deletePeer(
                it->first,
                [&](PublicKey const& validator, std::weak_ptr<Peer> peer) {
                    f(validator, peer);
                },
                true);
            it = messageTime_.erase(it);
        }
        else
            ++it;
    }

    for (auto it = slots_.begin(); it != slots_.end();)
    {
        if (now - it->second.getLastSelected() > MAX_UNSQUELCH_EXPIRE)
        {
            it = slots_.erase(it);
        }
        else
            ++it;
    }
}

template <typename Peer, typename clock_type>
void
Slots<Peer, clock_type>::touch(id_t const& id)
{
    messageTime_[id] = clock_type::now();
}

}  // namespace Squelch

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_SLOT_H_INCLUDED
