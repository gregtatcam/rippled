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

#include <ripple/peerfinder/impl/SlotImp.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/peerfinder/impl/Tuning.h>

namespace ripple {
namespace PeerFinder {

constexpr std::uint16_t SquelchImp::MAX_MESSAGES;
constexpr std::uint16_t SquelchImp::MAX_LAPSE;
template<typename T>
std::atomic_bool SquelchImp::Entry<T>::squelchUpstream_ = false;
template<typename T>
std::size_t SquelchImp::Entry<T>::timeLapseUpstream_ = 0;
template<typename T>
beast::IP::Endpoint const SquelchImp::Entry<T>::upstreamEndpoint_{};
template<typename T>
std::mutex SquelchImp::Entry<T>::mutex_;

SlotImp::SlotImp (beast::IP::Endpoint const& local_endpoint,
    beast::IP::Endpoint const& remote_endpoint, bool fixed,
        clock_type& clock)
    : recent (clock)
    , m_inbound (true)
    , m_fixed (fixed)
    , m_reserved (false)
    , m_state (accept)
    , m_remote_endpoint (remote_endpoint)
    , m_local_endpoint (local_endpoint)
    , m_listening_port (unknownPort)
    , m_squelch(remote_endpoint)
    , checked (false)
    , canAccept (false)
    , connectivityCheckInProgress (false)
{
}

SlotImp::SlotImp (beast::IP::Endpoint const& remote_endpoint,
    bool fixed, clock_type& clock)
    : recent (clock)
    , m_inbound (false)
    , m_fixed (fixed)
    , m_reserved (false)
    , m_state (connect)
    , m_remote_endpoint (remote_endpoint)
    , m_listening_port (unknownPort)
    , m_squelch(remote_endpoint)
    , checked (true)
    , canAccept (true)
    , connectivityCheckInProgress (false)
{
}

void
SlotImp::state (State state_)
{
    // Must go through activate() to set active state
    assert (state_ != active);

    // The state must be different
    assert (state_ != m_state);

    // You can't transition into the initial states
    assert (state_ != accept && state_ != connect);

    // Can only become connected from outbound connect state
    assert (state_ != connected || (! m_inbound && m_state == connect));

    // Can't gracefully close on an outbound connection attempt
    assert (state_ != closing || m_state != connect);

    m_state = state_;
}

void
SlotImp::activate (clock_type::time_point const& now)
{
    // Can only become active from the accept or connected state
    assert (m_state == accept || m_state == connected);

    m_state = active;
    whenAcceptEndpoints = now;
}

//------------------------------------------------------------------------------

Slot::~Slot() = default;

//------------------------------------------------------------------------------

SlotImp::recent_t::recent_t (clock_type& clock)
    : cache (clock)
{
}

void
SlotImp::recent_t::insert (beast::IP::Endpoint const& ep, int hops)
{
    auto const result (cache.emplace (ep, hops));
    if (! result.second)
    {
        // NOTE Other logic depends on this <= inequality.
        if (hops <= result.first->second)
        {
            result.first->second = hops;
            cache.touch (result.first);
        }
    }
}

bool
SlotImp::recent_t::filter (beast::IP::Endpoint const& ep, int hops)
{
    auto const iter (cache.find (ep));
    if (iter == cache.end())
        return false;
    // We avoid sending an endpoint if we heard it
    // from them recently at the same or lower hop count.
    // NOTE Other logic depends on this <= inequality.
    return iter->second <= hops;
}

void
SlotImp::recent_t::expire ()
{
    beast::expire (cache,
        Tuning::liveCacheSecondsToLive);
}

void
SquelchImp::EntryBase::expireDownstream ()
{
    if (squelchDownstream_ && ++timeLapseDownstream_ > MAX_LAPSE)
    {
        timeLapseDownstream_ = 0;
        squelchDownstream_ = false;
    }
}

void
SquelchImp::EntryBase::unSquelchDownstream ()
{
    squelchDownstream_ = false;
}

template<typename T>
bool
SquelchImp::Entry<T>::squelched (beast::IP::Endpoint const& endpoint)
{
    std::lock_guard l(mutex_);
    if (squelchUpstream_ && endpoint == upstreamEndpoint_)
    {
        squelchUpstream_ = false;
        const_cast<beast::IP::Endpoint&>(upstreamEndpoint_) = {};
        return true;
    }
    return false;
}

template<typename T>
void
SquelchImp::Entry<T>::squelchDownstream (beast::IP::Endpoint const& endpoint)
{
    squelchDownstream_ = true;
    timeLapseDownstream_ = 0;
}

template<typename T>
bool
SquelchImp::Entry<T>::checkUpstreamSquelch (beast::IP::Endpoint const& endpoint)
{
    if (!squelchUpstream_)
    {
        ++upstreamMessageCount_;
        if (upstreamMessageCount_ > MAX_MESSAGES)
        {
            std::lock_guard l(mutex_);
            if (!squelchUpstream_) // another peer may have already set it
            {
                squelchUpstream_ = true;
                const_cast<beast::IP::Endpoint&>(upstreamEndpoint_) = endpoint;
                return true;
            }
        }
    }

    return false;
}

template<typename T>
void
SquelchImp::Entry<T>::expireUpstream ()
{
    if (squelchUpstream_ && ++timeLapseUpstream_ > MAX_LAPSE)
    {
        std::lock_guard l(mutex_);
        timeLapseUpstream_ = 0;
        squelchUpstream_ = false;
        const_cast<beast::IP::Endpoint&>(upstreamEndpoint_) = {};
    }
}

bool
SquelchImp::squelched(SquelchType type, beast::IP::Endpoint const& endpoint)
{
    switch (type)
    {
        case SquelchType::Validation:
            return ValidationEntry::squelched(endpoint);
        case SquelchType::Propose:
            return ProposeEntry::squelched(endpoint);
        case SquelchType::Transaction:
            return TransactionEntry::squelched(endpoint);
    }
    assert(0);
}

void
SquelchImp::expireUpstream ()
{
    ValidationEntry::expireUpstream();
    ProposeEntry::expireUpstream();
    TransactionEntry::expireUpstream();
}

void
SquelchImp::expireDownstream ()
{
    validation_.expireDownstream();
    propose_.expireDownstream();
    transaction_.expireDownstream();
}

SquelchImp::EntryBase&
SquelchImp::getEntry(SquelchType type)
{
    switch (type)
    {
        case SquelchType::Validation:
            return validation_;
        case SquelchType::Propose:
            return propose_;
        case SquelchType::Transaction:
            return transaction_;
    }
    assert(0);
}

bool
SquelchImp::checkUpstreamSquelch (Squelch::SquelchType type)
{
    return getEntry(type).checkUpstreamSquelch(remoteEndpoint_);
}

void
SquelchImp::squelchDownstream (Squelch::SquelchType type, bool squelch)
{
    if (squelch)
        getEntry(type).squelchDownstream(remoteEndpoint_);
    else
        getEntry(type).unSquelchDownstream();
}

bool
SquelchImp::squelchedDownstream (Squelch::SquelchType type)
{
    switch (type)
    {
        case Squelch::SquelchType::Validation:
            return validation_.squelchDownstream_;
        case Squelch::SquelchType::Propose:
            return propose_.squelchDownstream_;
        case Squelch::SquelchType::Transaction:
            return transaction_.squelchDownstream_;
    }
    assert(0);
}

}
}
