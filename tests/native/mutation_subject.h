#pragma once

// Deliberately simple subjects for verifying Mull itself, never product code.
__attribute__((noinline)) inline bool mutationBoundary(int value) {
  return value >= 10;
}
__attribute__((noinline)) inline int mutationWeak(int value) {
  return value + 1;
}
__attribute__((noinline)) inline void mutationCountdown(int value) {
  while (value > 0) {
    value = value - 1;
  }
}
