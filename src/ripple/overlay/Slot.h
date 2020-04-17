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

#include <ripple/basics/random.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/Squelch.h>

namespace ripple {

namespace Squelch {

using namespace std::chrono;

/** Maintains Validation and Propose Set message count for specific validator from
 * the upstream peers.
 */
class Slot
{
    // Message count threshold to select a peer as the source
    // of messages from the validator
    static constexpr uint16_t COUNT_THRESHOLD = 10;
    // Max selected peers
    static constexpr uint16_t MAX_PEERS = 3;

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
    using clock_type = system_clock;
public:
    Slot ()
    : timeSelected_(clock_type::now())
    , state_(SlotState::Counting)
    {}

    /** Update message count for the peer. If the message is from a new
     * peer or from a previously expired squelched peer then switch
     * the peer's and slot's state to Counting. If the number of messages
     * for the peer is COUNT_THRESHOLD then set the peer's state
     * to Selected. If the number of selected peers is MAX_PEERS then
     * call f() for each peer, which is not selected and not already
     * in squelched state. Set the state for those peers to Squelched
     * and reset the count of all peers. Set slot's state to Selected.
     * Message count is not update when the slot is in Selected state.
     * @param peer Peer which received the message
     * @param type  Message type (Validation and Propose Set only,
     *     others are ignored)
     * @param f Function is called for every peer that has to be
     *     squelched with peer's
     * weak_ptr as the argument
     */
    template<typename F>
    void
    updateMessageCount (std::shared_ptr<Peer> peer,
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
    deletePeer (Peer::id_t const& id, F&& f);

private:
    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts ();

    /** Initialize slot to Counting state */
    void
    initCounting();

    /** Data maintained for each peer */
    struct Entry {
        std::weak_ptr<Peer> peer_; // peer's weak_ptr passed to callbacks
        PeerState state_; // peer's state
        std::size_t count_; // message's count
        clock_type::time_point expire_; // squelch expiration time
    };
    std::unordered_map<Peer::id_t, Entry> peers_; // peer's data
    std::unordered_map<Peer::id_t, bool> selected_; // peers selected as the
                                                    // source of messages from
                                                    // validator
    clock_type::time_point timeSelected_; // last time peers were selected,
                                          // used to age the slot
    SlotState state_; // slot's state
};

template<typename F>
void
Slot::updateMessageCount (std::shared_ptr<Peer> peer,
                         protocol::MessageType, F&& f)
{
    auto id = peer->id();

    // First time message from this peer.
    if (peers_.find(id) == peers_.end())
    {
        peers_.emplace( std::make_pair(id, std::move(
                 Entry{peer, PeerState::Counting, 0, clock_type::now()})));
        initCounting();
    }
    // Message from a peer with expired squelch
    else if (
        peers_[id].state_ == PeerState::Squelched &&
             clock_type::now() > peers_[id].expire_)
    {
        peers_[id].state_ = PeerState::Counting;
        initCounting();
    }

    if (state_ != SlotState::Counting ||
        peers_[id].state_ == PeerState::Squelched)
        return;

    if (++peers_[id].count_ > COUNT_THRESHOLD)
        selected_.emplace(std::make_pair(id, true));

    if (selected_.size() == MAX_PEERS)
    {
        timeSelected_ = clock_type::now();

        for (auto &[k,v] : peers_)
        {
            v.count_ = 0;

            if (selected_.find(k) != selected_.end())
                v.state_ = PeerState::Selected;
            else if (v.state_ != PeerState::Squelched)
            {
                v.state_ = PeerState::Squelched;
                v.expire_ = timeSelected_ +
                    seconds(rand_int(MIN_UNSQUELCH_EXPIRE.count(),
                                     MAX_UNSQUELCH_EXPIRE.count()));
                f(v.peer_, time_point_cast<seconds>(v.expire_).
                                        time_since_epoch().count());
            }
        }
        selected_.clear ();
        state_ = SlotState::Selected;
    }
}

template<typename F>
void
Slot::deletePeer (Peer::id_t const& id, F&& f)
{
    if (peers_[id].state_ == PeerState::Selected)
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

inline
void
Slot::resetCounts()
{
   for (auto &[k, v] : peers_)
   {
       if (v.state_ != PeerState::Squelched)
           v.count_ = 0;
   }
}

inline
void
Slot::initCounting()
{
    state_ = SlotState::Counting;
    selected_.clear();
    resetCounts();
}

} // Squelch

} // ripple

#endif //RIPPLE_OVERLAY_SLOT_H_INCLUDED
