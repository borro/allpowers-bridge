#include <limits>
int main() {
  volatile int maximum = std::numeric_limits<int>::max();
  volatile int overflow = maximum + 1; // Deliberate control, never product code.
  (void)overflow;
  return 0;
}
