#include "mutation_subject.h"
#include <cassert>
#include <cstring>
#include <chrono>
#include <thread>

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "fail") == 0)
    return 1;
  const bool strong = argc > 1 && std::strcmp(argv[1], "strong") == 0;
  if (!mutationBoundary(strong ? 10 : 11)) {
    // A quick baseline must not shrink the mutant deadline below ordinary
    // failure-reporting latency. This fails as Timeout without our floor.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 1;
  }
  assert(mutationWeak(2) > 0); // Intentionally weak: both +1 and -1 pass.
  mutationCountdown(2);      // Replacing subtraction with addition must time out.
}
