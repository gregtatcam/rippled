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

namespace ripple {

namespace Squelch {

/** Maintains Validation and Propose Set message count for specific validator from
 * the upstream peers.
 */
class Slot
{
    /** State of the upstream peer */
    enum class State : uint8_t {
        Squelched = 0x01, // squelched
        Counting = 0x02 // counting messages
    };
    inline static constexpr uint16_t COUNT_THRESHOLD = 10;
    inline static constexpr uint16_t MAX_PEERS = 3;
    using clock_type = std::chrono::steady_clock;
public:
    Slot () : squelchStart_(clock_type::now()), squelched_(false) {}
    Slot (Slot const&) = delete;
    Slot& operator = (Slot const&) = delete;
    Slot (Slot&&) = default;
    Slot& operator = (Slot &&) = default;

    /** Update message type count for the peer.
     * @param id Peer id
     * @param type  Message type (Validation and Propose Set only, others are ignored)
     * @param f Function is called for every squelched peer with peer id as the argument
     */
    template<typename F>
    void
    updateMessageCount (Peer::id_t const& id, protocol::MessageType type, F&& f);

    /** Peer disconnected. Delete the count for the specified peer.
     * @param id Peer id
     * @param f Function is called for every squelched peer with peer id as the argument
     */
    template<typename F>
    void
    deletePeer (Peer::id_t const& id, F&& f);

    /** Reset counts for all peers. */
    void
    resetCounts ();

private:
    std::unordered_map<Peer::id_t, std::pair<State, size_t>> messageCount_; // count of message, validation & propose pooled together
    std::unordered_map<Peer::id_t, bool> selected_; // peers selected as the source of the messages from the validator
    clock_type::time_point squelchStart_; // time changed to squelched state
    //std::mutex mutex_; // handle concurrency for the counters
    std::atomic_bool squelched_; // upstream is squelched
};

template<typename F>
void
Slot::updateMessageCount (Peer::id_t const& id, protocol::MessageType, F&& f)
{
    //std::lock_guard l(mutex_);

    // First time message from this peer
    if (messageCount_.find(id) == messageCount_.end())
    {
        messageCount_.emplace(std::make_pair(id, std::make_pair(State::Counting, 0)));
        squelched_ = false;
        selected_.clear();
    }
    // Message from a peer that was previously squelched and became
    // unsquelched because of the squelch time limit
    else if (messageCount_[id].first == State::Squelched &&
             clock_type::now() > (squelchStart_ + MIN_UNSQUELCH_EXPIRE))
    {
        messageCount_[id].first = State::Counting;
        messageCount_[id].second = 0;
        squelched_ = false;
        selected_.clear();
    }

    if (squelched_)
        return;

    if (++messageCount_[id].second > COUNT_THRESHOLD)
        selected_.emplace(std::make_pair(id, true));

    if (selected_.size() == MAX_PEERS)
    {
        squelched_ = true;
        squelchStart_ = clock_type::now();

        for (auto const &[k,v] : messageCount_)
        {
            if (v.first == State::Counting && selected_.find(k) == selected_.end())
            {
                messageCount_[k].first = State::Squelched;
                f(k);
            }
        }
    }
}

template<typename F>
void
Slot::deletePeer (Peer::id_t const& id, F&& f)
{
    //std::lock_guard l(mutex_);

    if (selected_.find(id) != selected_.end())
    {
        for (auto const &[k, v] : messageCount_)
        {
            if (v.first == State::Squelched)
                f(k);
        }

        resetCounts();
    }
}

} // Squelch

} // ripple

#endif //RIPPLE_OVERLAY_SLOT_H_INCLUDED
