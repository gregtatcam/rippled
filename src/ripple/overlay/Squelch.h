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

#include <ripple/protocol/PublicKey.h>
#include <chrono>
#include <functional>

namespace ripple {

namespace Squelch {

namespace config
{
static constexpr std::chrono::seconds MIN_UNSQUELCH_EXPIRE{300};
static constexpr std::chrono::seconds MAX_UNSQUELCH_EXPIRE{600};
static constexpr std::chrono::seconds SQUELCH_LATENCY{10};
}

using namespace std::chrono;

/** Maintains squelching of relaying messages from validators */
class Squelch {
    using clock_type    = steady_clock;
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
    setConfig(seconds minExpire, seconds maxExpire, seconds latency);

    /** Get random squelch duration between MIN_UNSQUELCH_EXPIRE and
     * MAX_UNSQUELCH_EXPIRE */
    static
    seconds
    getSquelchDuration();

private:
    /** Maintains the list of squelched relaying to downstream peers.
     * Expiration time is included in the TMSquelch message. */
    hash_map <PublicKey, clock_type::time_point> squelched_;
    inline static seconds MIN_UNSQUELCH_EXPIRE = config::MIN_UNSQUELCH_EXPIRE;
    inline static seconds MAX_UNSQUELCH_EXPIRE = config::MAX_UNSQUELCH_EXPIRE;
    inline static seconds SQUELCH_LATENCY = config::SQUELCH_LATENCY;
};

} // Squelch

} // ripple

#endif //RIPPLED_SQUELCH_H
