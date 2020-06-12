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

#include <ripple/overlay/Peer.h>
#include <ripple/overlay/Squelch.h>
#include <ripple/overlay/SquelchCommon.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple.pb.h>

namespace ripple {

namespace squelch {

template <typename clock_type>
class Slots;

/** Peer's State */
enum class PeerState : uint8_t {
    Counting,   // counting messages
    Selected,   // selected to relay, counting if Slot in Counting
    Squelched,  // squelched, doesn't relay
};
/** Slot's State */
enum class SlotState : uint8_t {
    Counting,  // counting messages
    Selected,  // peers selected, stop counting
};

template <typename Unit, typename TP>
Unit
epoch(TP const& t)
{
    return duration_cast<Unit>(t.time_since_epoch());
}

/** Abstract class. Declares squelch and unsquelch handlers */
class SquelchHandler
{
public:
    virtual ~SquelchHandler()
    {
    }
    /** Squelch handler
     * @param validator Public key of the source validator
     * @param peer Peer to squelch
     * @param duration Squelch duration in seconds
     */
    virtual void
    squelch(
        PublicKey const& validator,
        std::weak_ptr<Peer> const& peer,
        std::uint32_t duration) const = 0;
    /** Unsquelch handler
     * @param validator Public key of the source validator
     * @param peer Peer to unsquelch
     */
    virtual void
    unsquelch(PublicKey const& validator, std::weak_ptr<Peer> const& peer)
        const = 0;
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
template <typename clock_type>
class Slot final
{
private:
    friend class Slots<clock_type>;
    using id_t = Peer::id_t;
    using time_point = typename clock_type::time_point;

    /** Constructor
     * @param handler Squelch/Unsquelch implementation
     */
    Slot(SquelchHandler const& handler)
        : reachedThreshold_(0)
        , lastSelected_(clock_type::now())
        , state_(SlotState::Counting)
        , handler_(handler)
    {
    }

    /** Update peer info. If the message is from a new
     * peer or from a previously expired squelched peer then switch
     * the peer's and slot's state to Counting. If time of last
     * selection round is > 2 * MAX_UNSQUELCH_EXPIRE then switch the slot's
     * state to Counting. If the number of messages for the peer
     * is > MIN_MESSAGE_THRESHOLD then add peer to considered peers pool.
     * If the number of considered peers who reached MAX_MESSAGE_THRESHOLD is
     * MAX_SELECTED_PEERS then randomly select MAX_SELECTED_PEERS from
     * considered peers, and call squelch handler for each peer, which is not
     * selected and not already in Squelched state. Set the state for those
     * peers to Squelched and reset the count of all peers. Set slot's state to
     * Selected. Message count is not updated when the slot is in Selected
     * state.
     * @param validator Public key of the source validator
     * @param id Peer id which received the message
     * @param peerPtr Peer which received the message
     * @param type  Message type (Validation and Propose Set only,
     *     others are ignored, future use)
     */
    void
    update(
        PublicKey const& validator,
        id_t id,
        std::weak_ptr<Peer> const& peerPtr,
        protocol::MessageType type);

    /** Handle peer deletion when a peer disconnects.
     * If the peer is in Selected state then
     * call unsquelch handler for every peer in squelched state and reset
     * every peer's state to Counting. Switch Slot's state to Counting.
     * @param validator Public key of the source validator
     * @param id Deleted peer id
     * @param erase If true then erase the peer. The peer is not erased
     *      when the peer when is idled. The peer is deleted when it
     *      disconnects
     */
    void
    deletePeer(PublicKey const& validator, id_t id, bool erase);

    /** Get the time of the last peer selection round */
    const time_point&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /** Return number of peers in state */
    std::uint16_t
    inState(PeerState state) const;

    /** Return number of peers not in state */
    std::uint16_t
    notInState(PeerState state) const;

    /** Return Slot's state */
    SlotState
    getState() const
    {
        return state_;
    }

    /** Return selected peers */
    std::set<id_t>
    getSelected() const;

    /** Get peers info. Return map of peer's state, count, squelch
     * expiration milsec, and last message time milsec.
     */
    std::
        unordered_map<id_t, std::tuple<PeerState, uint16_t, uint32_t, uint32_t>>
        getPeers() const;

    /** Check if peers stopped relaying messages. If a peer is
     * selected peer then call unsquelch handler for all
     * currently squelched peers and switch the slot to
     * Counting state.
     * @param validator Public key of the source validator
     */
    void
    checkIdle(PublicKey const& validator);

private:
    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts();

    /** Initialize slot to Counting state */
    void
    initCounting();

    /** Data maintained for each peer */
    struct PeerInfo
    {
        std::weak_ptr<Peer> const peer;  // peer's weak_ptr
        PeerState state;                 // peer's state
        std::size_t count;               // message count
        time_point expire;               // squelch expiration time
        time_point lastMessage;          // time last message received
    };
    std::unordered_map<id_t, PeerInfo> peers_;  // peer's data
    std::unordered_set<id_t> considered_;       // pool of peers considered
                                                // as the source of messages
                                                // from validator - peers
                                           // that reached MIN_MESSAGE_THRESHOLD
    std::uint8_t reachedThreshold_;  // number of peers that reached
                                     // MAX_MESSAGE_THRESHOLD
    typename clock_type::time_point
        lastSelected_;  // last time peers were selected,
                        // used to age the slot
    SlotState state_;   // slot's state
    SquelchHandler const& handler_;
};

template <typename clock_type>
void
Slot<clock_type>::checkIdle(PublicKey const& validator)
{
    auto now = clock_type::now();
    for (auto it = peers_.begin(); it != peers_.end();)
    {
        auto& peer = it->second;
        auto id = it->first;
        ++it;
        if (now - peer.lastMessage > IDLED)
            deletePeer(validator, id, false);
    }
}

template <typename clock_type>
void
Slot<clock_type>::update(
    PublicKey const& validator,
    id_t id,
    std::weak_ptr<Peer> const& peerPtr,
    protocol::MessageType type)
{
    auto now = clock_type::now();
    auto it = peers_.find(id);
    // First message from this peer
    if (it == peers_.end())
    {
        peers_.emplace(std::make_pair(
            id, PeerInfo{peerPtr, PeerState::Counting, 0, now, now}));
        initCounting();
        return;
    }
    // Message from a peer with expired squelch
    else if (
        it->second.state == PeerState::Squelched && now > it->second.expire)
    {
        it->second.state = PeerState::Counting;
        it->second.lastMessage = now;
        initCounting();
        return;
    }

    auto& peer = it->second;
    peer.lastMessage = now;

    if (state_ != SlotState::Counting || peer.state == PeerState::Squelched)
        return;

    if (++peer.count > MIN_MESSAGE_THRESHOLD)
        considered_.insert(id);
    if (peer.count == (MAX_MESSAGE_THRESHOLD + 1))
        ++reachedThreshold_;

    if (now - lastSelected_ > 2 * MAX_UNSQUELCH_EXPIRE)
    {
        initCounting();
        return;
    }

    if (reachedThreshold_ == MAX_SELECTED_PEERS)
    {
        // Randomly select MAX_SELECTED_PEERS peers from considered.
        // Exclude peers that have been idling > IDLED -
        // it's possible that checkIdle() has not been called yet.
        // If number of remaining peers != MAX_SELECTED_PEERS
        // then reset the Counting state and let checkIdle() handle
        // idled peers.
        std::unordered_set<id_t> selected;
        while (selected.size() != MAX_SELECTED_PEERS && considered_.size() != 0)
        {
            auto i =
                considered_.size() == 1 ? 0 : rand_int(considered_.size() - 1);
            auto it = std::next(considered_.begin(), i);
            auto id = *it;
            considered_.erase(it);
            if (peers_.find(id) == peers_.end())
            {
                // TBD have to log this
                continue;
            }
            // TBD : should we be stricter and include < x% of IDLED? (50%?)
            if (now - peers_[id].lastMessage < IDLED)
                selected.insert(id);
        }

        if (selected.size() != MAX_SELECTED_PEERS)
        {
            initCounting();
            return;
        }

        lastSelected_ = now;

        // squelch peers which are not selected and
        // not already squelched
        for (auto& [k, v] : peers_)
        {
            v.count = 0;

            if (selected.find(k) != selected.end())
                v.state = PeerState::Selected;
            else if (v.state != PeerState::Squelched)
            {
                v.state = PeerState::Squelched;
                auto duration = Squelch<clock_type>::getSquelchDuration();
                v.expire = now + duration;
                handler_.squelch(
                    validator,
                    v.peer,
                    duration_cast<milliseconds>(duration).count());
            }
        }
        considered_.clear();
        reachedThreshold_ = 0;
        state_ = SlotState::Selected;
    }
}

template <typename clock_type>
void
Slot<clock_type>::deletePeer(PublicKey const& validator, id_t id, bool erase)
{
    auto it = peers_.find(id);
    if (it != peers_.end())
    {
        if (it->second.state == PeerState::Selected)
        {
            auto now = clock_type::now();
            for (auto& [_, v] : peers_)
            {
                if (v.state == PeerState::Squelched)
                    handler_.unsquelch(validator, v.peer);
                v.state = PeerState::Counting;
                v.count = 0;
                v.expire = now;
            }

            considered_.clear();
            reachedThreshold_ = 0;
            state_ = SlotState::Counting;
        }
        else if (considered_.find(id) != considered_.end())
        {
            if (it->second.count > MAX_MESSAGE_THRESHOLD)
                --reachedThreshold_;
            considered_.erase(id);
        }

        if (erase)
            peers_.erase(id);
    }
}

template <typename clock_type>
void
Slot<clock_type>::resetCounts()
{
    for (auto& [_, peer] : peers_)
        peer.count = 0;
}

template <typename clock_type>
void
Slot<clock_type>::initCounting()
{
    state_ = SlotState::Counting;
    considered_.clear();
    reachedThreshold_ = 0;
    resetCounts();
}

template <typename clock_type>
std::uint16_t
Slot<clock_type>::inState(PeerState state) const
{
    return std::count_if(peers_.begin(), peers_.end(), [&](auto const& it) {
        return (it.second.state == state);
    });
}

template <typename clock_type>
std::uint16_t
Slot<clock_type>::notInState(PeerState state) const
{
    return std::count_if(peers_.begin(), peers_.end(), [&](auto const& it) {
        return (it.second.state != state);
    });
}

template <typename clock_type>
std::set<typename Peer::id_t>
Slot<clock_type>::getSelected() const
{
    std::set<id_t> init;
    return std::accumulate(
        peers_.begin(), peers_.end(), init, [](auto& init, auto const& it) {
            if (it.second.state == PeerState::Selected)
            {
                init.insert(it.first);
                return init;
            }
            return init;
        });
}

template <typename clock_type>
std::unordered_map<
    typename Peer::id_t,
    std::tuple<PeerState, uint16_t, uint32_t, uint32_t>>
Slot<clock_type>::getPeers() const
{
    auto init = std::unordered_map<
        id_t,
        std::tuple<PeerState, std::uint16_t, std::uint32_t, std::uint32_t>>();
    return std::accumulate(
        peers_.begin(), peers_.end(), init, [](auto& init, auto const& it) {
            init.emplace(std::make_pair(
                it.first,
                std::move(std::make_tuple(
                    it.second.state,
                    it.second.count,
                    epoch<milliseconds>(it.second.expire).count(),
                    epoch<milliseconds>(it.second.lastMessage).count()))));
            return init;
        });
}

/** Slots is a container for validator's Slot and handles Slot update
 * when a message is received from a validator. It also handles Slot aging
 * and checks for peers which are disconnected or stopped relaying the messages.
 */
template <typename clock_type>
class Slots final
{
    using time_point = typename clock_type::time_point;
    using id_t = typename Peer::id_t;

public:
    /**
     * @param handler Squelch/unsquelch implementation
     */
    Slots(SquelchHandler const& handler) : handler_(handler)
    {
    }
    ~Slots() = default;
    /** Calls Slot::update of Slot associated with the validator.
     * @param validator Validator's public key
     * @param id Peer's id which received the message
     * @param id Peer's pointer which received the message
     * @param type Received protocol message type
     */
    void
    checkForSquelch(
        PublicKey const& validator,
        id_t id,
        std::weak_ptr<Peer> const& peer,
        protocol::MessageType type);

    /** Called when a peer is deleted. If the peer is selected to be the
     * source of messages from the validator then unsquelch handler
     * is called for all currently squelched peers.
     * @param id Peer's id
     */
    void
    unsquelch(id_t id);

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator.
     */
    void
    checkIdle();

    /** Return number of peers in state in state */
    boost::optional<std::uint16_t>
    inState(PublicKey const& validator, PeerState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.inState(state);
        return {};
    }

    /** Return number of peers in state in state */
    boost::optional<std::uint16_t>
    notInState(PublicKey const& validator, PeerState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.notInState(state);
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
        std::tuple<PeerState, uint16_t, uint32_t, std::uint32_t>>
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
     */
    void
    deletePeer(id_t id, bool erase);

    hash_map<PublicKey, Slot<clock_type>> slots_;
    SquelchHandler const& handler_;
};

template <typename clock_type>
void
Slots<clock_type>::checkForSquelch(
    PublicKey const& validator,
    id_t id,
    std::weak_ptr<Peer> const& peerPtr,
    protocol::MessageType type)
{
    auto& peer =
        [&]() {
            auto it = slots_.find(validator);
            if (it == slots_.end())
            {
                auto [it, _] = slots_.emplace(std::make_pair(
                    validator, std::move(Slot<clock_type>(handler_))));
                return it;
            }
            else
                return it;
        }()
            ->second;

    peer.update(validator, id, peerPtr, type);
}

template <typename clock_type>
void
Slots<clock_type>::deletePeer(id_t id, bool erase)
{
    for (auto& [validator, slot] : slots_)
        slot.deletePeer(validator, id, erase);
}

template <typename clock_type>
void
Slots<clock_type>::unsquelch(id_t id)
{
    deletePeer(id, true);
}

template <typename clock_type>
void
Slots<clock_type>::checkIdle()
{
    auto now = clock_type::now();

    for (auto it = slots_.begin(); it != slots_.end();)
    {
        it->second.checkIdle(it->first);
        if (now - it->second.getLastSelected() > MAX_UNSQUELCH_EXPIRE)
            it = slots_.erase(it);
        else
            ++it;
    }
}

}  // namespace squelch

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_SLOT_H_INCLUDED
