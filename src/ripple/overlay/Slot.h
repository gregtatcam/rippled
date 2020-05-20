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

template <typename Peer, typename clock_type>
class Slots;

/** Peer's State */
enum class PeerState : uint8_t {
    Counting = 0x01,   // counting messages
    Selected = 0x02,   // selected to relay, counting if Slot in Counting
    Squelched = 0x03,  // squelched, doesn't relay
};
/** Slot's State */
enum class SlotState : uint8_t {
    Counting = 0x01,  // counting messages
    Selected = 0x02,  // peers selected, stop counting
};

/**
 * Slot is associated with a specific validator via validator's public key.
 * Slot counts messages from a validator, selects peers to be the source
 * of the messages, and communicates the peers to be squelched. Slot can be
 * in the following states: 1) Counting. This is the peer selection state
 * when Slot counts the messages and selects the peers; 2) Selected. Slot
 * doesn't count messages in this state but a received message
 * may transition Slot to Counting state.
 */
template <typename Peer, typename clock_type>
class Slot final
{
private:
    friend class Slots<Peer, clock_type>;
    using id_t = typename Peer::id_t;
    using time_point = typename clock_type::time_point;

    Slot()
        : lastSelected_(clock_type::now())
        , state_(SlotState::Counting)
        , reachedThreshold_(false)
    {
    }

    /** Update peer info. If the message is from a new
     * peer or from a previously expired squelched peer then switch
     * the peer's and slot's state to Counting. If the number of messages
     * for the peer is greater than MESSAGE_LOW_THRESHOLD then set the peer's
     * state to Selected. If the number of selected peers is MAX_PEERS
     * and there is at least one peer with the message count greater
     * than MESSAGE_UPPER_THRESHOLD then switch slot's state to Selected,
     * randomly select MAX_PEERS from selected peers and call f() for
     * each peer, which is not selected and not already in Squelched state.
     * Set the state for those peers to Squelched and reset the count of
     * all peers. Set slot's state to Selected. Message count is not updated
     * when the slot is in Selected state.
     * @param id Peer id which received the message
     * @param peerPtr Peer which received the message
     * @param type  Message type (Validation and Propose Set only,
     *     others are ignored, future use)
     * @param f Function is called for every peer that has to be
     *     squelched with arguments:
     *     peer weak_ptr
     *     squelch duration
     */
    template <typename F>
    void
    update(
        const id_t& id,
        std::weak_ptr<Peer> peerPtr,
        protocol::MessageType type,
        F&& f);

    /** Handle peer deletion when a peer disconnects.
     * If the peer is in Selected state then
     * call f() for every peer in squelched state and reset every
     * peer's state to Counting. Switch Slot's state to Counting.
     * @param id Deleted peer id
     * @param erase If true then erase the peer. The peer is not erased
     *      when the peer when is idled. The peer is deleted when it
     *      disconnects
     * @param f Function is called for every peer in Squelched state
     *     with arguments:
     *     peer weak_ptr
     */
    template <typename F>
    void
    deletePeer(id_t const& id, bool erase, F&& f);

    /** Get the time of the last peer selection round */
    const time_point&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /** Return number of peers in state/not in state */
    template <typename Comp = std::equal_to<>>
    std::uint16_t
    inState(PeerState state, Comp comp = {}) const;

    /** Return Slot's state */
    SlotState
    getState()
    {
        return state_;
    }

    /** Return selected peers */
    std::set<id_t>
    getSelected() const;

    /** Get peers info. Return map of peer's state, count, and squelch
     * expiration milliseconds.
     */
    std::unordered_map<id_t, std::tuple<PeerState, uint16_t, uint32_t>>
    getPeers();

    /** Check if peers stopped relaying messages
     * @param f Function is called for every peer in Squelched state
     *     with arguments:
     *     peer weak_ptr
     */
    template <typename F>
    void
    checkIdle(F&& f);

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
        std::weak_ptr<Peer> peer_;  // peer's weak_ptr
        PeerState state_;           // peer's state
        std::size_t count_;         // message count
        time_point expire_;         // squelch expiration time
        time_point lastMessage_;    // time last message received
    };
    std::unordered_map<id_t, PeerInfo> peers_;  // peer's data
    std::unordered_map<id_t, bool> selected_;   // pool of peers selected
                                                // as the source of messages
                                                // from validator
    typename clock_type::time_point
        lastSelected_;  // last time peers were selected,
                        // used to age the slot
    SlotState state_;   // slot's state
    bool
        reachedThreshold_;  // at least one peer reached upper message threshold
};

template <typename Peer, typename clock_type>
template <typename F>
void
Slot<Peer, clock_type>::checkIdle(F&& f)
{
    auto now = clock_type::now();
    for (auto it = peers_.begin(); it != peers_.end();)
    {
        auto& peer = it->second;
        auto id = it->first;
        ++it;
        if (now - peer.lastMessage_ > IDLED)
            deletePeer(id, false, std::forward<F>(f));
    }
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slot<Peer, clock_type>::update(
    id_t const& id,
    std::weak_ptr<Peer> peerPtr,
    protocol::MessageType type,
    F&& f)
{
    auto now = clock_type::now();
    auto it = peers_.find(id);
    // First message from this peer
    if (it == peers_.end())
    {
        peers_.emplace(std::make_pair(
            id,
            std::move(PeerInfo{peerPtr, PeerState::Counting, 0, now, now})));
        initCounting();
        return;
    }
    // Message from a peer with expired squelch
    else if (
        it->second.state_ == PeerState::Squelched && now > it->second.expire_)
    {
        it->second.state_ = PeerState::Counting;
        it->second.lastMessage_ = now;
        initCounting();
        return;
    }

    auto& peer = it->second;
    peer.lastMessage_ = now;

    if (state_ != SlotState::Counting || peer.state_ == PeerState::Squelched)
        return;

    if (++peer.count_ > MESSAGE_LOW_THRESHOLD)
        selected_[id] = true;

    if (peer.count_ > MESSAGE_UPPER_THRESHOLD)
        reachedThreshold_ = true;

    if (reachedThreshold_ && selected_.size() >= MAX_SELECTED_PEERS)
    {
        // randomly select MAX_SELECTED_PEERS peers
        while (selected_.size() != MAX_SELECTED_PEERS)
        {
            auto it = selected_.begin();
            auto i = rand_int(selected_.size() - 1);
            selected_.erase(std::next(it, i));
        }

        lastSelected_ = now;

        // squelch peers which are not selected and
        // not already squelched
        for (auto& [k, v] : peers_)
        {
            v.count_ = 0;

            if (selected_.find(k) != selected_.end())
                v.state_ = PeerState::Selected;
            else if (v.state_ != PeerState::Squelched)
            {
                v.state_ = PeerState::Squelched;
                auto duration = Squelch<clock_type>::getSquelchDuration();
                v.expire_ = now + duration;
                f(v.peer_, duration_cast<milliseconds>(duration).count());
            }
        }
        selected_.clear();
        state_ = SlotState::Selected;
        reachedThreshold_ = false;
    }
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slot<Peer, clock_type>::deletePeer(id_t const& id, bool erase, F&& f)
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
        }
        if (erase)
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
    reachedThreshold_ = false;
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
template <typename Comp>
std::uint16_t
Slot<Peer, clock_type>::inState(PeerState state, Comp comp) const
{
    return accumulate(0, [&](int& init, id_t const& id, PeerInfo const& peer) {
        if (comp(peer.getState(), state))
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
                std::move(std::make_tuple(
                    peer.state_,
                    peer.count_,
                    duration_cast<milliseconds>(peer.expire_.time_since_epoch())
                        .count()))));
            return init;
        });
}

/** Slots is a container for validator's Slot and handles Slot update
 * when a message is received from a validator. It also handles Slot aging
 * and checks for peers which are disconnected or stopped relaying the messages.
 */
template <typename Peer, typename clock_type>
class Slots final
{
    using time_point = typename clock_type::time_point;
    using id_t = typename Peer::id_t;

public:
    Slots() = default;
    ~Slots() = default;
    /** Calls Slot::update of Slot associated with the validator.
     * @param validator Validator's public key
     * @param id Peer's id which received the message
     * @param id Peer's pointer which received the message
     * @param type Received protocol message type
     * @param f Function is called for every peer that has to be
     *     squelched with the arguments:
     *     validator's public key
     *     peer weak_ptr
     *     squelch duration
     */
    template <typename F>
    void
    checkForSquelch(
        PublicKey const& validator,
        id_t const& id,
        std::weak_ptr<Peer> peer,
        protocol::MessageType type,
        F&& f);

    /** Called when a peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     * @param f Function is called for every peer in Squelched state
     *     with the arguments:
     *     validator's public key
     *     peer weak_ptr
     */
    template <typename F>
    void
    unsquelch(id_t const& id, F&& f);

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator.
     * @param f Function is called for every peer in Squelched state
     *     with the arguments:
     *     validator's public key
     *     peer weak_ptr
     */
    template <typename F>
    void
    checkIdle(F&& f);

    /** Return number of peers in state/not in state */
    template <typename Comp = std::equal_to<>>
    boost::optional<std::uint16_t>
    inState(PublicKey const& validator, PeerState state, Comp comp = {}) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.inState(state, comp);
        return {};
    }

    /** Return true if Slot is in State */
    template <typename Comp = std::equal_to<>>
    boost::optional<bool>
    inState(PublicKey const& validator, SlotState state, Comp comp = {}) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return comp(it->second.state_, state);
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

    /** Get Slot's state */
    boost::optional<SlotState>
    getState(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getState();
        return {};
    }

private:
    /** Called when a peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     * @param erase If true then erase the peer
     * @param f Function is called for every peer in Squelched state
     *     with the arguments:
     *     validator's public key
     *     peer weak_ptr
     */
    template <typename F>
    void
    deletePeer(id_t const& id, bool erase, F&& f);

    hash_map<PublicKey, Slot<Peer, clock_type>> slots_;
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
    auto& peer =
        [&]() {
            auto it = slots_.find(validator);
            if (it == slots_.end())
            {
                auto [it, b] = slots_.emplace(std::make_pair(
                    validator, std::move(Slot<Peer, clock_type>())));
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
Slots<Peer, clock_type>::deletePeer(id_t const& id, bool erase, F&& f)
{
    for (auto& it : slots_)
        it.second.deletePeer(
            id, erase, [&](std::weak_ptr<Peer> peer) { f(it.first, peer); });
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slots<Peer, clock_type>::unsquelch(id_t const& id, F&& f)
{
    deletePeer(id, true, f);
}

template <typename Peer, typename clock_type>
template <typename F>
void
Slots<Peer, clock_type>::checkIdle(F&& f)
{
    auto now = clock_type::now();

    for (auto it = slots_.begin(); it != slots_.end();)
    {
        it->second.checkIdle([&](std::weak_ptr<Peer> wp) { f(it->first, wp); });
        if (now - it->second.getLastSelected() > MAX_UNSQUELCH_EXPIRE)
            it = slots_.erase(it);
        else
            ++it;
    }
}

}  // namespace Squelch

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_SLOT_H_INCLUDED
