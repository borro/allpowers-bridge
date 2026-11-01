#pragma once
#include <stdint.h>

// A completed report may wait for the USB host inside SendReport. Bound the
// batch between loop iterations; an individual driver's timeout is additional.
class HidPumpBudget {
  uint32_t started;
  unsigned reports = 0;
  bool stalled = false;

public:
  static constexpr unsigned MaxReports = 4;
  static constexpr uint32_t WorkMicros = 3000;
  explicit HidPumpBudget(uint32_t now) : started(now) {}
  bool canContinue(uint32_t now) const {
    return !stalled && reports < MaxReports && uint32_t(now - started) < WorkMicros;
  }
  void attempted(bool completed) {
    ++reports;
    stalled = !completed;
  }
  static constexpr uint32_t delayMs(bool active) {
    return active ? 1 : 5;
  }
};
