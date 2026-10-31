#pragma once
#include <stdint.h>

namespace HistoryFormat {
// One immutable UTC anchor per boot. Never carry uptime across a power cycle.
// A recent Feature snapshot binds the host time to this boot's seconds counter.
class ClockAnchor {
  uint32_t base = 0;
public:
  bool synced() const { return base != 0; }
  bool candidate(uint32_t epoch, uint32_t observedUptime, uint64_t now, uint32_t &value) const {
    if (epoch < 1577836800u || observedUptime > epoch || now < observedUptime ||
        now - observedUptime > 5 || uint64_t(epoch) + now - observedUptime > UINT32_MAX)
      return false;
    value = epoch - observedUptime;
    return value != 0;
  }
  void commit(uint32_t value) { if (!base) base = value; }
  uint32_t at(uint64_t uptime) const {
    return base && uptime <= UINT32_MAX - base ? base + uint32_t(uptime) : 0;
  }
};
}
