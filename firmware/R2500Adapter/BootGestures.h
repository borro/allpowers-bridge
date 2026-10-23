#pragma once
#include <stdint.h>

// Pure RAM state machine: no GPIO, delays, allocation, reset or flash operations.
// Pass pressed=true for BOOT/GPIO0 LOW. Poll regularly from the application loop.
class BootGestures {
public:
  enum class Event : uint8_t { None, Single, Double, Hold };
  static constexpr uint32_t DebounceMs = 30;
  static constexpr uint32_t DoubleMs = 350;
  static constexpr uint32_t HoldMs = 800;

  void begin(bool pressed, uint32_t now) {
    raw_ = stable_ = pressed;
    armed_ = !pressed;
    waiting_ = second_ = holdSent_ = false;
    changedAt_ = pressedAt_ = releasedAt_ = now;
  }

  // Single is delayed after the first debounced release. A second press whose
  // raw onset is within DoubleMs reserves the gesture until release or Hold.
  // Hold starts at the debounced press, emits once, and cancels both clicks.
  // Call begin() once. Unsigned subtraction supports millis() rollover; polling
  // must not stop for an entire uint32_t timer period (~49.7 days).
  Event update(bool pressed, uint32_t now) {
    Event event = Event::None;
    if (pressed != raw_) {
      raw_ = pressed;
      changedAt_ = now;
    }
    if (raw_ != stable_ && elapsed(now, changedAt_) >= DebounceMs) {
      stable_ = raw_;
      if (!armed_) {
        // Startup-held BOOT: ignore the whole gesture, including its release.
        if (!stable_) {
          armed_ = true;
          waiting_ = second_ = holdSent_ = false;
        }
        return Event::None;
      }
      if (stable_) {
        pressedAt_ = now;
        holdSent_ = false;
        second_ = waiting_ && elapsed(changedAt_, releasedAt_) <= DoubleMs;
        if (waiting_ && !second_) {
          // Expired first click plus a new independent press in the same poll.
          waiting_ = false;
          event = Event::Single;
        }
      } else {
        if (holdSent_) {
          second_ = false;
          return Event::None;
        }
        // Also recognize a long press if polling missed the threshold while
        // down. Use raw release time, excluding release debounce latency.
        if (elapsed(changedAt_, pressedAt_) >= HoldMs) {
          waiting_ = second_ = false;
          holdSent_ = true;
          return Event::Hold;
        }
        if (second_) {
          waiting_ = second_ = false;
          return Event::Double;
        }
        waiting_ = true;
        releasedAt_ = now;
      }
    }
    if (!armed_) return Event::None;
    if (stable_ && raw_ && !holdSent_ && elapsed(now, pressedAt_) >= HoldMs) {
      holdSent_ = true;
      waiting_ = second_ = false;
      return Event::Hold;
    }
    // A possible second press undergoing debounce must not leak a Single.
    const bool pendingSecond = raw_ && elapsed(changedAt_, releasedAt_) <= DoubleMs;
    if (waiting_ && !stable_ && !pendingSecond && elapsed(now, releasedAt_) >= DoubleMs) {
      waiting_ = false;
      return Event::Single;
    }
    return event;
  }

  // Runs entirely in RAM; suitable for the existing firmware self-test gate.
  // Every timing scenario is repeated across uint32_t millis() wraparound.
  static bool selfTest() {
    for (uint8_t pass = 0; pass < 2; ++pass) {
      const uint32_t base = pass ? UINT32_C(0xffffff00) : 0;
      BootGestures b;
      auto check = [&](bool pressed, uint32_t t, Event expected) {
        return b.update(pressed, base + t) == expected;
      };
      const Event N = Event::None, S = Event::Single;
      const Event D = Event::Double, H = Event::Hold;

      b.begin(true, base);
      if (!check(true, 1000, N) || !check(false, 1100, N) ||
          !check(false, 1130, N) || !check(false, 1600, N)) return false;
      if (!check(true, 1700, N) || !check(true, 1730, N) ||
          !check(false, 1800, N) || !check(false, 1830, N) ||
          !check(false, 2179, N) || !check(false, 2180, S) ||
          !check(false, 2500, N)) return false;

      b.begin(false, base);
      if (!check(true, 10, N) || !check(false, 20, N) ||
          !check(false, 50, N) || !check(false, 500, N)) return false;

      b.begin(false, base);
      if (!check(true, 100, N) || !check(true, 130, N) ||
          !check(false, 200, N) || !check(false, 230, N) ||
          !check(true, 400, N) || !check(true, 430, N) ||
          !check(true, 600, N) || !check(false, 700, N) ||
          !check(false, 730, D) || !check(false, 1200, N)) return false;

      b.begin(false, base);
      if (!check(true, 100, N) || !check(true, 130, N) ||
          !check(true, 929, N) || !check(true, 930, H) ||
          !check(true, 1200, N) || !check(false, 1300, N) ||
          !check(false, 1330, N) || !check(false, 1800, N)) return false;

      // A short click followed by a long second press produces Hold only.
      b.begin(false, base);
      if (!check(true, 100, N) || !check(true, 130, N) ||
          !check(false, 200, N) || !check(false, 230, N) ||
          !check(true, 400, N) || !check(true, 430, N) ||
          !check(true, 1230, H) || !check(false, 1300, N) ||
          !check(false, 1330, N) || !check(false, 1800, N)) return false;

      // Second raw onset exactly on the deadline survives debounce.
      b.begin(false, base);
      if (!check(true, 100, N) || !check(true, 130, N) ||
          !check(false, 200, N) || !check(false, 230, N) ||
          !check(true, 580, N) || !check(true, 610, N) ||
          !check(false, 650, N) || !check(false, 680, D)) return false;

      // Late second press: two independent singles, never a double.
      b.begin(false, base);
      if (!check(true, 100, N) || !check(true, 130, N) ||
          !check(false, 200, N) || !check(false, 230, N) ||
          !check(true, 581, S) || !check(true, 611, N) ||
          !check(false, 650, N) || !check(false, 680, N) ||
          !check(false, 1030, S) || !check(false, 1100, N)) return false;

      // Second-press bounce cannot swallow the pending single forever.
      b.begin(false, base);
      if (!check(true, 100, N) || !check(true, 130, N) ||
          !check(false, 200, N) || !check(false, 230, N) ||
          !check(true, 570, N) || !check(false, 585, S) ||
          !check(false, 620, N)) return false;

      // Release first observed after the hold threshold, without a hold poll.
      b.begin(false, base);
      if (!check(true, 100, N) || !check(true, 130, N) ||
          !check(false, 1000, N) || !check(false, 1030, H) ||
          !check(false, 1500, N)) return false;
    }
    return true;
  }

private:
  static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
  bool raw_ = false, stable_ = false, armed_ = true;
  bool waiting_ = false, second_ = false, holdSent_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0, releasedAt_ = 0;
};