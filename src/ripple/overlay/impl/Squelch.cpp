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
#include <ripple/basics/random.h>
#include <ripple/overlay/Squelch.h>

namespace ripple {
namespace Squelch {

using namespace std::chrono;

void
Squelch::squelch (PublicKey const &validator, bool squelch,
                 uint64_t squelchDuration)
{
    if (squelch)
    {
        squelched_[validator] = [squelchDuration]() {
            auto now = clock_type::now();
            auto duration = time_point<clock_type>(seconds(squelchDuration));
            auto min = now + MIN_UNSQUELCH_EXPIRE;
            auto max = now + MAX_UNSQUELCH_EXPIRE + SQUELCH_LATENCY;
            return (duration >= min && duration <= max) ? duration : min;
        }();
    } else
        squelched_.erase(validator);
}

bool
Squelch::isSquelched (PublicKey const &validator)
{
    auto now = clock_type::now();

    if (squelched_.find(validator) == squelched_.end())
        return false;
    else if (squelched_[validator] < now)
        return true;

    squelched_.erase(validator);

    return false;
}

} // Squelch

} // ripple
