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
    /** State of the upstream peer */
    enum class State : uint8_t {
        Squelched = 0x01, // squelched
        Counting  = 0x02, // counting messages
        Selected  = 0x03, // selected to receive, could be counting too
    };
    inline static constexpr uint16_t COUNT_THRESHOLD = 10;
    inline static constexpr uint16_t MAX_PEERS = 3;
    using clock_type = system_clock;
public:
    Slot ()
    : timeSelected_(clock_type::now())
    {}

    /** Update message count for the peer. If the message is from a new peer or from
     * a previously squelched peer (subject to expire time) then switch the state to
     * counting. If the number of messages for the peer is COUNT_THRESHOLD then
     * set the peer's state to Selected. If the number of selected peers is MAX_PEERS then
     * call f() for each peer, which is not selected and not already in squelched state.
     * Set the state for those peers to Squelched and reset the count of all peers.
     * @param peer Peer which received the message
     * @param type  Message type (Validation and Propose Set only, others are ignored)
     * @param f Function is called for every peer that has to be squelched with peer's
     * weak_ptr as the argument
     */
    template<typename F>
    void
    updateMessageCount (std::shared_ptr<Peer> peer, protocol::MessageType type, F&& f);

    /** Handle peer deletion. If the peer is in Selected state then call f() for
     * every peer in squelched state and reset every peer's state to Counting.
     * Switch Slot's state to counting.
     * @param id Deleted peer id
     * @param f Function is called for every peer in Squelched state with peer weak_ptr
     * as the argument
     */
    template<typename F>
    void
    deletePeer (Peer::id_t const& id, F&& f);

private:
    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts ();

    /** Data maintained for each peer */
    struct Entry {
        std::weak_ptr<Peer> peer_; // peer's weak_ptr passed to callbacks
        State state_; // peer's state
        std::size_t count_; // message's count
        clock_type::time_point expire_; // Squelched expiration time
    };
    std::unordered_map<Peer::id_t, Entry> messageCount_; // peer's data
    std::unordered_map<Peer::id_t, bool> selected_; // peers selected as the source of messages from validator
    clock_type::time_point timeSelected_; // last time peers were selected, used to age the slot
};

template<typename F>
void
Slot::updateMessageCount (std::shared_ptr<Peer> peer, protocol::MessageType, F&& f)
{
    auto id = peer->id();

    // First time message from this peer.
    if (messageCount_.find(id) == messageCount_.end())
    {
        messageCount_.emplace(std::make_pair(id,
                std::move(Entry{peer, State::Counting, 0, clock_type::now()})));
        resetCounts();
        selected_.clear();
    }
    // Message from a peer that was previously squelched and became
    // unsquelched because of the squelch time limit
    else if (messageCount_[id].state_ == State::Squelched &&
             clock_type::now() > messageCount_[id].expire_)
    {
        messageCount_[id].state_ = State::Counting;
        resetCounts();
        selected_.clear();
    }

    if (messageCount_[id].state_ == State::Squelched)
        return;

    if (++messageCount_[id].count_ > COUNT_THRESHOLD)
        selected_.emplace(std::make_pair(id, true));

    if (selected_.size() == MAX_PEERS)
    {
        timeSelected_ = clock_type::now();

        for (auto &[k,v] : messageCount_)
        {
            v.count_ = 0;

            if (selected_.find(k) != selected_.end())
                v.state_ = State::Selected;
            else if (v.state_ != State::Squelched)
            {
                v.state_ = State::Squelched;
                v.expire_ = timeSelected_ + seconds(rand_int(MIN_UNSQUELCH_EXPIRE.count(),
                                                             MAX_UNSQUELCH_EXPIRE.count()));
                f(v.peer_, time_point_cast<seconds>(v.expire_).time_since_epoch().count());
            }
        }
        selected_.clear ();
    }
}

template<typename F>
void
Slot::deletePeer (Peer::id_t const& id, F&& f)
{
    if (messageCount_[id].state_ == State::Selected)
    {
        auto now = clock_type::now();
        for (auto &[k, v] : messageCount_)
        {
            if (v.state_ == State::Squelched)
                f(v.peer_);
            v.state_ = State::Counting;
            v.count_ = 0;
            v.expire_ = now;
        }

        selected_.clear();
    }

    messageCount_.erase(id);
}

inline
void
Slot::resetCounts()
{
   for (auto &[k, v] : messageCount_)
   {
       if (v.state_ != State::Squelched)
           v.count_ = 0;
   }
}

} // Squelch

} // ripple

#endif //RIPPLE_OVERLAY_SLOT_H_INCLUDED
