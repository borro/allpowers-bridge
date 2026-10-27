#include "mutation_subject.h"
#include <cassert>
#include <cstring>

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "fail") == 0)
    return 1;
  const bool strong = argc > 1 && std::strcmp(argv[1], "strong") == 0;
  assert(mutationBoundary(strong ? 10 : 11));
  assert(mutationWeak(2) > 0); // Intentionally weak: both +1 and -1 pass.
  mutationCountdown(2);      // Replacing subtraction with addition must time out.
}
