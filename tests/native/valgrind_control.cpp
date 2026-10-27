#include <cstdio>
#include <cstdlib>

// Deliberate uninitialised read: only built for the Memcheck policy control,
// never linked into products or included among the ordinary passing tests.
int main() {
  auto *byte = static_cast<unsigned char *>(std::malloc(1));
  if (!byte) return 2;
  std::printf("%u\n", unsigned(*byte));
  std::free(byte);
  return 0;
}
