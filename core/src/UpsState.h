#pragma once
#include <stdint.h>

// No OS headers, allocation, Qt, or changes to the telemetry C ABI.
namespace AllpowersUps {
constexpr uint32_t Version = 1, MaxAgeMs = 15000;
enum : uint32_t { Present = 1, Mains = 2, Charging = 4, Discharging = 8, RuntimeKnown = 16 };
struct State {
  uint32_t size = sizeof(State), version = Version;
  uint64_t generation = 0, sequence = 0;
  uint32_t flags = 0, percent = 0, runtimeSeconds = 0, validForMs = 0;
};
static_assert(sizeof(State) == 40, "Driver bridge layout");
inline bool valid(const State &s) {
  return s.size == sizeof(State) && s.version == Version && s.generation && s.sequence &&
         !(s.flags & ~31u) && s.percent <= 100 && s.validForMs <= MaxAgeMs &&
         (!(s.flags & RuntimeKnown) || s.runtimeSeconds < 0x80000000u) &&
         (!(s.flags & Present) || s.validForMs > 0) &&
         !((s.flags & Charging) && (s.flags & Discharging)) &&
         (!(s.flags & Charging) || (s.flags & Mains)) &&
         (!(s.flags & Discharging) || !(s.flags & Mains)) && ((s.flags & Present) || s.flags == 0);
}
// Shared by the real kernel driver and host tests. Expiry uses the driver's
// monotonic clock, not a sender-supplied wall clock or repeating heartbeat.
struct Cache {
  State state{};
  uint64_t deadline = 0;
  uint32_t tag = 0, nextTag = 0;
  bool present(uint64_t now) const {
    return tag && now < deadline;
  }
  bool expire(uint64_t now) {
    if (tag && !present(now)) {
      tag = 0;
      return true;
    }
    return false;
  }
  bool accept(const State &s, uint64_t now) {
    if (!valid(s) || s.generation < state.generation ||
        (s.generation == state.generation && s.sequence <= state.sequence))
      return false;
    const bool replacement = s.generation != state.generation;
    expire(now);
    state = s;
    deadline = now + s.validForMs;
    if (!(s.flags & Present))
      tag = 0;
    else if (!tag || replacement) {
      if (!++nextTag)
        ++nextTag;
      tag = nextTag;
    }
    return true;
  }
  // A service handle is exclusive; closing it ends that publisher's epoch.
  void release() {
    state = State{};
    deadline = 0;
    tag = 0;
  }
};
} // namespace AllpowersUps
