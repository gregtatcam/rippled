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

#include <ripple/protocol/BaseAmount.h>

namespace ripple {

namespace {

// Use a static inside a function to help prevent order-of-initialzation issues
LocalValue<bool>&
getStaticSTAmountCanonicalizeSwitchover()
{
    static LocalValue<bool> r{true};
    return r;
}
}  // namespace

bool
getSTAmountCanonicalizeSwitchover()
{
    return *getStaticSTAmountCanonicalizeSwitchover();
}

void
setSTAmountCanonicalizeSwitchover(bool v)
{
    *getStaticSTAmountCanonicalizeSwitchover() = v;
}

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

// Calculate (a * b) / c when all three values are 64-bit
// without loss of precision:

// This is the legacy version of canonicalizeRound.  It's been in use
// for years, so it is deeply embedded in the behavior of cross-currency
// transactions.
//
// However in 2022 it was noticed that the rounding characteristics were
// surprising.  When the code converts from IOU-like to XRP-like there may
// be a fraction of the IOU-like representation that is too small to be
// represented in drops.  `canonicalizeRound()` currently does some unusual
// rounding.
//
//  1. If the fractional part is greater than or equal to 0.1, then the
//     number of drops is rounded up.
//
//  2. However, if the fractional part is less than 0.1 (for example,
//     0.099999), then the number of drops is rounded down.
//
// The XRP Ledger has this rounding behavior baked in.  But there are
// situations where this rounding behavior led to undesirable outcomes.
// So an alternative rounding approach was introduced.  You'll see that
// alternative below.
namespace detail {

void
canonicalizeRound(bool nativeOrMPT, std::uint64_t& value, int& offset, bool)
{
    if (nativeOrMPT)
    {
        if (offset < 0)
        {
            int loops = 0;

            while (offset < -1)
            {
                value /= 10;
                ++offset;
                ++loops;
            }

            value += (loops >= 2) ? 9 : 10;  // add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > BaseAmountConst::cMaxValue)
    {
        while (value > (10 * BaseAmountConst::cMaxValue))
        {
            value /= 10;
            ++offset;
        }

        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

// The original canonicalizeRound did not allow the rounding direction to
// be specified.  It also ignored some of the bits that could contribute to
// rounding decisions.  canonicalizeRoundStrict() tracks all of the bits in
// the value being rounded.
void
canonicalizeRoundStrict(
    bool nativeOrMPT,
    std::uint64_t& value,
    int& offset,
    bool roundUp)
{
    if (nativeOrMPT)
    {
        if (offset < 0)
        {
            bool hadRemainder = false;

            while (offset < -1)
            {
                // It would be better to use std::lldiv than to separately
                // compute the remainder.  But std::lldiv does not support
                // unsigned arguments.
                std::uint64_t const newValue = value / 10;
                hadRemainder |= (value != (newValue * 10));
                value = newValue;
                ++offset;
            }
            value +=
                (hadRemainder && roundUp) ? 10 : 9;  // Add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > BaseAmountConst::cMaxValue)
    {
        while (value > (10 * BaseAmountConst::cMaxValue))
        {
            value /= 10;
            ++offset;
        }
        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

}  // namespace detail

}  // namespace ripple
