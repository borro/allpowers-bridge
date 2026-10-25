#pragma once
#include <stdint.h>

namespace AllpowersCore {
// A stream of only one required packet group must not keep a degraded link
// alive forever. Unsigned subtraction also handles the millisecond wraparound.
class TelemetryRecovery {
  uint32_t lastComplete = 0;
public:
  void begin(uint32_t now) { lastComplete = now; }
  void observe(uint32_t now, bool complete) { if (complete) lastComplete = now; }
  bool reconnectDue(uint32_t now) const { return uint32_t(now-lastComplete)>45000; }
};
}
