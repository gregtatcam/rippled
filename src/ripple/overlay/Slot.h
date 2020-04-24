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
#include <ripple/protocol/PublicKey.h>

namespace ripple {

namespace Squelch {

using namespace std::chrono;
template <typename Peer> class Slots;

namespace config {
static constexpr seconds IDLED{4};
}

inline
std::uint32_t
toSecs(steady_clock::time_point const& t)
{
    return duration_cast<seconds>(t.time_since_epoch()).count();
}

/** Peer's State */
enum class PeerState : uint8_t {
    Squelched = 0x01, // squelched
    Counting  = 0x02, // counting messages
    Selected  = 0x03, // selected to receive, could be counting too
};
/** Slot's State */
enum class SlotState : uint8_t {
    Counting  = 0x01, // counting messages
    Selected  = 0x02, // peers selected, stop counting
};

/** Maintains Validation and Propose Set message count for specific validator from
 * the upstream peers.
 */
template<typename Peer>
class Slot final
{
public:
    // Message count threshold to select a peer as the source
    // of messages from the validator
    static constexpr uint16_t MESSAGE_COUNT_THRESHOLD = 20;
    // Max selected peers
    static constexpr uint16_t MAX_SELECTED_PEERS = 3;
private:
    friend class Slots<Peer>;
    using clock_type = steady_clock;
    using id_t = typename Peer::id_t;

    Slot ()
    : lastSelected_(clock_type::now())
    , state_(SlotState::Counting)
    {}

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
    template<typename F>
    void
    update(const id_t& id, std::weak_ptr<Peer> peerPtr,
                       protocol::MessageType type, F&& f);

    /** Handle peer deletion. If the peer is in Selected state then
     * call f() for every peer in squelched state and reset every
     * peer's state to Counting. Switch Slot's state to Counting.
     * @param id Deleted peer id
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     */
    template<typename F>
    void
    deletePeer (id_t const& id, F&& f);

    /** Get the time of the last peers selection round */
    const clock_type::time_point&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /** Return if slot is in counting state and if all counts
     * of unsquelched peers are set to 0 */
    std::pair<bool, bool>
    isCountingState() const;

    /** Return number of peers in the state, or not in state */
    template<typename Comp = std::equal_to<>>
    std::uint16_t
    inState(PeerState state, Comp comp = {}) const;

    /** Return selected peers */
    std::set<id_t>
    getSelected() const;
    
    /** Get peers info. Return map of peer's state, count, and squelch expiration
     * seconds.
     */
    std::unordered_map<id_t, std::tuple<PeerState, uint16_t, uint32_t>>
    getPeers();
    
private:
    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts ();

    /** Initialize slot to Counting state */
    void
    initCounting();
    
    /** Accumulate value T over peers. F(T&, id_t const&, PeerInfo const&) */
    template<typename T, typename F>
    T
    accumulate(T t, F&& f) const;

    /** Data maintained for each peer */
    struct PeerInfo {
        std::weak_ptr<Peer> peer_; // peer's weak_ptr passed to callbacks
        PeerState state_; // peer's state
        std::size_t count_; // message's count
        clock_type::time_point expire_; // squelch expiration time
    };
    std::unordered_map<id_t, PeerInfo> peers_; // peer's data
    std::unordered_map<id_t, bool> selected_; // peers selected as the
                                                    // source of messages from
                                                    // validator
    clock_type::time_point lastSelected_; // last time peers were selected,
                                          // used to age the slot
    SlotState state_; // slot's state
};

template<typename Peer>
template<typename F>
void
Slot<Peer>::update(id_t const& id, std::weak_ptr<Peer> peerPtr,
             protocol::MessageType type, F&& f)
{
    auto &peer = [&]() {
        auto it = peers_.find(id);
        // First time message from this peer.
        if (it == peers_.end())
        {
            auto [i, b] = peers_.emplace(std::make_pair(
                id,
                std::move(PeerInfo{
                peerPtr, PeerState::Counting, 0, clock_type::now()})));
            initCounting();
            it = i;
        }
        // Message from a peer with expired squelch
        else if (it->second.state_ == PeerState::Squelched &&
                clock_type::now() > it->second.expire_)
        {
            it->second.state_ = PeerState::Counting;
            initCounting();
        }
        return it;
    }()->second;

    if (state_ != SlotState::Counting || peer.state_ == PeerState::Squelched)
        return;

    if (++peer.count_ > MESSAGE_COUNT_THRESHOLD)
        selected_[id] = true;

    if (selected_.size() == MAX_SELECTED_PEERS)
    {
        lastSelected_ = clock_type::now();

        for (auto &[k,v] : peers_)
        {
            v.count_ = 0;

            if (selected_.find(k) != selected_.end())
                v.state_ = PeerState::Selected;
            else if (v.state_ != PeerState::Squelched)
            {
                v.state_ = PeerState::Squelched;
                auto duration = Squelch::getSquelchDuration();
                v.expire_ = lastSelected_ + duration;
                f(v.peer_, duration.count());
            }
        }
        selected_.clear ();
        state_ = SlotState::Selected;
    }
}

template<typename Peer>
template<typename F>
void
Slot<Peer>::deletePeer (id_t const& id, F&& f)
{
    auto it = peers_.find(id);
    if (it != peers_.end())
    {
        if (it->second.state_ == PeerState::Selected)
        {
            auto now = clock_type::now();
            for (auto &[k, v] : peers_)
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
        peers_.erase(id);
    }
}

template<typename Peer>
void
Slot<Peer>::resetCounts()
{
    for (auto& [k, v] : peers_)
        v.count_ = 0;
}

template<typename Peer>
void
Slot<Peer>::initCounting()
{
    state_ = SlotState::Counting;
    selected_.clear();
    resetCounts();
}

template<typename Peer>
template<typename T, typename F>
T
Slot<Peer>::accumulate(T t, F&& f) const
{
    return std::accumulate(peers_.begin(), peers_.end(), t,
                           [&](T &t, auto const& it) {
        return f(t, it.first, it.second);
    });
}

template<typename Peer>
std::pair<bool, bool>
Slot<Peer>::isCountingState() const
{
    auto resetCounts =
        accumulate(0, [](int& init, id_t const&, PeerInfo const& peer){
            return init += peer.count_;});
    return std::make_pair(state_ == SlotState::Counting, resetCounts == 0);
}

template<typename Peer>
template<typename Comp>
std::uint16_t
Slot<Peer>::inState(PeerState state, Comp comp) const
{
    return accumulate( 0, [&](int &init, id_t const& id, PeerInfo const &peer) {
        if (comp(peer.state_, state))
            return ++init;
        return init;
    });
}

template<typename Peer>
std::set<typename Peer::id_t>
Slot<Peer>::getSelected() const
{
    std::set<id_t> init;
    return accumulate(init,
                           [](auto &init, id_t const& id, PeerInfo const& peer) {
      if (peer.state_ == PeerState::Selected)
      {
          init.insert(id);
          return init;
      }
      return init;
    });
}

template<typename Peer>
std::unordered_map<typename Peer::id_t,
                   std::tuple<PeerState, uint16_t, uint32_t>>
Slot<Peer>::getPeers()
{
    auto init = std::unordered_map<id_t, std::tuple<PeerState,
        std::uint16_t, std::uint32_t>>();
    return accumulate(init, [](auto& init, id_t const& id, PeerInfo const& peer){
        init.emplace(std::make_pair(id,
                     std::make_tuple(peer.state_, peer.count_,
                     toSecs(peer.expire_))));
        return init;
    });
}

template<typename Peer>
class Slots final
{
    using clock_type = steady_clock;
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
    template<typename F>
    void
    checkForSquelch(PublicKey const& validator, id_t const& id,
                    std::weak_ptr<Peer> peer, protocol::MessageType type,
                    F&& f);

    /** Called when the peer is deleted. If the peer was selected to be the source
     * of messages from the validator then squelched peers have to be unsquelched.
     * @param id Peer's id
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     */
    template<typename F>
    void
    unsquelch(id_t const& id, F&& f);

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     */
    template<typename F>
    void
    checkIdle(F&& f);

    /** Update last time peer received a message */
    void
    touchIdle (const id_t&);

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
    template<typename Comp = std::equal_to<>>
    boost::optional<std::uint16_t>
    inState(PublicKey const& validator, PeerState state, Comp comp = {}) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.inState(state, comp);
        return {};
    }

    /** Used in unit testing to age slots/peers sooner */
    static
    void
    configIdled(seconds idled);

    /** Get selected peers */
    std::set<id_t>
    getSelected(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getSelected();
        return {};
    }
   
    /** Get peers info. Return map of peer's state, count, and squelch expiration
     * seconds.
     */
    std::unordered_map<typename Peer::id_t,
                       std::tuple<PeerState, uint16_t, uint32_t>>
    getPeers(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getPeers();
        return {};
    }
    
    /** Get idled info. */
    std::unordered_map<id_t, clock_type::time_point> const&
    getIdled() const
    {
        return idlePeers_;
    }

private:
    
    /** Called when the peer is deleted. If the peer was selected to be the source
     * of messages from the validator then squelched peers have to be unsquelched.
     * @param id Peer's id
     * @param f Function is called for every peer in Squelched state
     *     with peer weak_ptr as the argument
     */
    template<typename F>
    void
    deletePeer(id_t const& id, F&& f);
    
    inline static seconds IDLED = config::IDLED;
    hash_map<PublicKey, Slot<Peer>> slots_;
    std::unordered_map<id_t, clock_type::time_point> idlePeers_;
};

template<typename Peer>
template<typename F>
void
Slots<Peer>::checkForSquelch(
    const PublicKey& validator,
    id_t const& id,
    std::weak_ptr<Peer> peerPtr,
    protocol::MessageType type,
    F&& f)
{
    touchIdle(id);

    auto& peer = [&]() {
        auto it = slots_.find(validator);
        if (it == slots_.end())
        {
            auto [it, b] = slots_.emplace(std::make_pair(validator, Slot<Peer>()));
            return it;
        }
        else
            return it;
    }()->second;

    peer.update(id, peerPtr, type, [&validator, f](std::weak_ptr<Peer> peerPtr,
                                           uint32_t squelchDuration) {
        f(validator, peerPtr, squelchDuration);
    });
}

template<typename Peer>
template<typename F>
void
Slots<Peer>::deletePeer(id_t const& id, F&& f)
{
    for (auto& it : slots_)
        it.second.deletePeer(id, [&](std::weak_ptr<Peer> peer) {
            f(it.first, peer);
        });
}

template<typename Peer>
template<typename F>
void
Slots<Peer>::unsquelch(id_t const& id, F&& f)
{
    deletePeer(id, f);
    idlePeers_.erase(id);
}

template<typename Peer>
template<typename F>
void
Slots<Peer>::checkIdle(F&& f)
{
    auto now = clock_type::now();
    for (auto it = idlePeers_.begin(); it != idlePeers_.end(); )
    {
        if (now > it->second)
        {
            deletePeer(
                it->first, [&](PublicKey const& validator, std::weak_ptr<Peer> peer) {
                    f(validator, peer);
                });
            it = idlePeers_.erase(it);
        }
        else
            ++it;
    }

    for (auto it = slots_.begin(); it != slots_.end();)
    {
        if (now - it->second.getLastSelected() > IDLED)
            it = slots_.erase(it);
        else
            ++it;
    }
}

template<typename Peer>
void
Slots<Peer>::touchIdle(id_t const& id)
{
    idlePeers_[id] = clock_type::now() + IDLED;
}

template<typename Peer>
void
Slots<Peer>::configIdled(seconds idled)
{
    IDLED = idled;
}

} // Squelch

} // ripple

#endif //RIPPLE_OVERLAY_SLOT_H_INCLUDED
