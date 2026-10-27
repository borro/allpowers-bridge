#include <cstddef>
#include <cstdint>

// Deliberate UB, used only to verify the fuzz runtime and fatal sanitizer policy.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size) {
    volatile int denominator = int(data[0]) - 0xa5;
    volatile int quotient = 1 / denominator;
    (void)quotient;
  }
  return 0;
}
