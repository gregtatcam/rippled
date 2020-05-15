//
// Created by gregt on 4/26/20.
//

#ifndef RIPPLE_OVERLAY_SQUELCHCOMMON_H_INCLUDED
#define RIPPLE_OVERLAY_SQUELCHCOMMON_H_INCLUDED

namespace ripple {
namespace Squelch {

using namespace std::chrono;
using duration_t = duration<std::uint32_t, std::milli>;

static constexpr duration_t MIN_UNSQUELCH_EXPIRE = seconds{300};
static constexpr duration_t MAX_UNSQUELCH_EXPIRE = seconds{600};
static constexpr duration_t SQUELCH_LATENCY = seconds{4};
static constexpr duration_t IDLED = seconds{4};
// Message count threshold to start selecting peers as the source
// of messages from the validator
static constexpr uint16_t MESSAGE_LOW_THRESHOLD = 20;
// Select peers from the pool of peers with the message
// count in {MESSAGE_LOW_THRESHOLD,
//           MESSAGE_UPPER_THRESHOLD}
static constexpr uint16_t MESSAGE_UPPER_THRESHOLD = 30;
// Max selected peers
static constexpr uint16_t MAX_SELECTED_PEERS = 3;

}  // namespace Squelch
}  // namespace ripple

#endif  // RIPPLED_SQUELCHCOMMON_H
