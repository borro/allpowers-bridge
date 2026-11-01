#pragma once
#include <stdint.h>
#include <string.h>

// Physical RGB565 row encoding shared by both panel transports. The optional
// shadow holds exactly the last wire image, so rotation changes need no special
// invalidation. The writer must consume each stripe synchronously.
template <unsigned Width, unsigned Height, bool LittleEndian> class DisplayFrame {
  bool valid = false;

public:
  static constexpr unsigned StripeRows = 8;
  template <class Writer>
  unsigned present(const uint16_t *pixels, unsigned logicalWidth, uint16_t *shadow, Writer write) {
    uint16_t stripe[Width * StripeRows];
    unsigned pending = 0, first = 0, transferred = 0;
    auto flush = [&] {
      if (!pending)
        return;
      write(first, pending, stripe);
      transferred += pending;
      pending = 0;
    };
    for (unsigned y = 0; y < Height; ++y) {
      auto *row = stripe + pending * Width;
      if (logicalWidth == Width && LittleEndian) {
        memcpy(row, pixels + y * Width, Width * sizeof(uint16_t));
      } else {
        for (unsigned x = 0; x < Width; ++x) {
          const uint16_t c =
              logicalWidth == Width ? pixels[y * Width + x] : pixels[(Width - 1 - x) * Height + y];
          row[x] = LittleEndian ? c : uint16_t(c << 8 | c >> 8);
        }
      }
      if (shadow && valid && !memcmp(row, shadow + y * Width, Width * sizeof(uint16_t))) {
        flush();
        continue;
      }
      if (!pending)
        first = y;
      if (shadow)
        memcpy(shadow + y * Width, row, Width * sizeof(uint16_t));
      if (++pending == StripeRows)
        flush();
    }
    flush();
    valid = shadow != nullptr;
    return transferred;
  }
};
