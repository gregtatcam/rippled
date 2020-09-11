//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_TXMETRICS_H_INCLUDED
#define RIPPLE_OVERLAY_TXMETRICS_H_INCLUDED

#include <ripple/protocol/messages.h>
#include "ripple/json/json_value.h"

#include <boost/circular_buffer.hpp>

#include <chrono>
#include <mutex>

namespace ripple {

namespace metrics {

using val_t = std::uint64_t;

struct SingleMetrics
{
    SingleMetrics(bool ptu = true) : perTimeUnit(ptu)
    {
    }
    using clock_type = std::chrono::steady_clock;
    clock_type::time_point intervalStart{clock_type::now()};
    val_t accum{0};
    val_t rollingAvg{0};
    std::uint64_t N{0};
    bool perTimeUnit{true};
    boost::circular_buffer<val_t> rollingAvgAggreg{30, 0ull};
    void
    addMetrics(val_t val);
};

struct MetricsPerMessage
{
    SingleMetrics cnt;
    SingleMetrics size;
    void
    addMetrics(val_t bytes);
};

struct TxMetrics
{
    mutable std::mutex mutex;
    MetricsPerMessage tx;
    MetricsPerMessage haveTx;
    MetricsPerMessage getLedger;
    MetricsPerMessage ledgerData;
    MetricsPerMessage getObjects;
    SingleMetrics selectedPeers{false};
    SingleMetrics suppressedPeers{false};
    SingleMetrics missingTx;
    void
    addMessage(protocol::MessageType type, val_t val);
    void
    addSelected(val_t selected, val_t suppressed);
    void
    addMissing(val_t missing);
    Json::Value
    json() const;
};

}  // namespace metrics

}  // namespace ripple

#endif