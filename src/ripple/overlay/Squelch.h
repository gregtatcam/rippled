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

#ifndef RIPPLE_OVERLAY_SQUELCH_H_INCLUDED
#define RIPPLE_OVERLAY_SQUELCH_H_INCLUDED

#include <ripple/basics/random.h>
#include <ripple/protocol/PublicKey.h>
#include <chrono>
#include <functional>

namespace ripple {

namespace Squelch {

using namespace std::chrono;
using duration_t = duration<std::uint32_t, std::milli>;

namespace config
{
static constexpr seconds MIN_UNSQUELCH_EXPIRE{300};
static constexpr seconds MAX_UNSQUELCH_EXPIRE{600};
static constexpr seconds SQUELCH_LATENCY{4};
}

/** Maintains squelching of relaying messages from validators */
template<typename clock_type>
class Squelch {
    using time_point    = typename clock_type::time_point;
public:
    Squelch () = default;
    virtual ~Squelch () = default;

    /** Squelch/Unsquelch relaying for the validator
     * @param validator The validator's public key
     * @param squelch Squelch/unsquelch flag
     * @param squelchDuration Squelch duration time if squelch is true
     */
    void
    squelch(PublicKey const &validator, bool squelch, uint64_t squelchDuration);

    /** Are the messages to this validator squelched
     * @param validator Validator's public key
     * @return true if squelched
     */
    bool
    isSquelched(PublicKey const &validator);

    /** Used in unit testing to "speed up" unsquelch */
    static
    void
    configSquelchDuration(const duration_t& minExpire,
                          const duration_t& maxExpire,
                          const duration_t& latency);

    /** Get random squelch duration between MIN_UNSQUELCH_EXPIRE and
     * MAX_UNSQUELCH_EXPIRE */
    static
    duration_t
    getSquelchDuration();

private:
    /** Maintains the list of squelched relaying to downstream peers.
     * Expiration time is included in the TMSquelch message. */
    hash_map <PublicKey, time_point> squelched_;
    inline static duration_t MIN_UNSQUELCH_EXPIRE = config::MIN_UNSQUELCH_EXPIRE;
    inline static duration_t MAX_UNSQUELCH_EXPIRE = config::MAX_UNSQUELCH_EXPIRE;
    inline static duration_t SQUELCH_LATENCY = config::SQUELCH_LATENCY;
};

template<typename clock_type>
void
Squelch<clock_type>::squelch (PublicKey const &validator, bool squelch,
                              uint64_t squelchDuration)
{
    if (squelch)
    {
        squelched_[validator] = [squelchDuration]() {
          duration_t duration = milliseconds(squelchDuration);
          return clock_type::now() +
              ((duration >= MIN_UNSQUELCH_EXPIRE &&
                    duration <= MAX_UNSQUELCH_EXPIRE)
              ? (duration + SQUELCH_LATENCY)
              : getSquelchDuration()); // TBD should we disconnect if invalid
                                       // duration?
        }();
    } else
        squelched_.erase(validator);
}

template<typename clock_type>
bool
Squelch<clock_type>::isSquelched (PublicKey const &validator)
{
    auto now = clock_type::now();
    
    if (squelched_.find(validator) == squelched_.end())
        return false;
    else if (squelched_[validator] < now)
        return true;
    
    squelched_.erase(validator);
    
    return false;
}

template<typename clock_type>
void
Squelch<clock_type>::configSquelchDuration(duration_t const& minExpire,
                                           duration_t const& maxExpire,
                                           duration_t const& latency)
{
    MIN_UNSQUELCH_EXPIRE = minExpire;
    MAX_UNSQUELCH_EXPIRE = maxExpire;
    SQUELCH_LATENCY = latency;
}

template<typename clock_type>
duration_t
Squelch<clock_type>::getSquelchDuration()
{
    auto d = milliseconds(ripple::rand_int(
        duration_cast<milliseconds>(MIN_UNSQUELCH_EXPIRE).count(),
        duration_cast<milliseconds>(MAX_UNSQUELCH_EXPIRE).count()));
    return d;
}

} // Squelch

} // ripple

#endif //RIPPLED_SQUELCH_H
