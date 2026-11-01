#pragma once
#include <stdint.h>
#include <string.h>

// Physical RGB565 encoding with small dirty rectangles. The shadow is the last
// wire image. The writer consumes packed pixels synchronously. No allocation.
template <unsigned Width, unsigned Height, bool LittleEndian> class DisplayTiles {
  bool valid = false;
  struct Box {
    unsigned x0, y0, x1, y1;
    unsigned area() const {
      return (x1 - x0 + 1) * (y1 - y0 + 1);
    }
  };

public:
  static constexpr unsigned StripeRows = 8, TileColumns = 16;
  struct Result {
    unsigned rows = 0, pixels = 0, rectangles = 0;
  };
  bool initialized() const {
    return valid;
  }
  template <class Writer>
  Result present(const uint16_t *pixels, unsigned logicalWidth, uint16_t *shadow, Writer write) {
    alignas(uint32_t) uint16_t stripe[Width * StripeRows];
    Box boxes[(Width + TileColumns - 1) / TileColumns];
    Result result;
    for (unsigned first = 0; first < Height; first += StripeRows) {
      const unsigned count = Height - first < StripeRows ? Height - first : StripeRows;
      if (logicalWidth == Width) {
        for (unsigned y = 0; y < count; ++y) {
          if constexpr (LittleEndian)
            memcpy(stripe + y * Width, pixels + (first + y) * Width, Width * 2);
          else
            for (unsigned x = 0; x < Width; ++x) {
              const uint16_t c = pixels[(first + y) * Width + x];
              stripe[y * Width + x] = uint16_t(c << 8 | c >> 8);
            }
        }
      } else {
        // Read consecutive logical pixels while transposing a short stripe.
        // The old row-at-a-time rotation repeatedly missed the PSRAM cache.
        for (unsigned x = 0; x < Width; ++x) {
          const auto *source = pixels + (Width - 1 - x) * Height + first;
          for (unsigned y = 0; y < count; ++y) {
            const uint16_t c = source[y];
            stripe[y * Width + x] = LittleEndian ? c : uint16_t(c << 8 | c >> 8);
          }
        }
      }
      if (!shadow || !valid) {
        if (shadow)
          memcpy(shadow + first * Width, stripe, count * Width * 2);
        write(0, first, Width, count, stripe);
        result.rows += count;
        result.pixels += Width * count;
        ++result.rectangles;
        continue;
      }
      unsigned used = 0, dirtyRows = 0;
      for (unsigned left = 0; left < Width; left += TileColumns) {
        const unsigned right = left + TileColumns < Width ? left + TileColumns : Width;
        Box b{Width, count, 0, 0};
        for (unsigned y = 0; y < count; ++y) {
          const auto *current = stripe + y * Width;
          auto *previous = shadow + (first + y) * Width;
          if (!memcmp(current + left, previous + left, (right - left) * 2))
            continue;
          for (unsigned x = left; x < right; ++x)
            if (current[x] != previous[x]) {
              if (x < b.x0)
                b.x0 = x;
              if (x > b.x1)
                b.x1 = x;
              if (y < b.y0)
                b.y0 = y;
              if (y > b.y1)
                b.y1 = y;
              dirtyRows |= 1u << y;
            }
        }
        if (b.x0 == Width)
          continue;
        for (unsigned y = 0; y < count; ++y)
          memcpy(shadow + (first + y) * Width + left, stripe + y * Width + left,
                 (right - left) * 2);
        if (used) {
          const auto &a = boxes[used - 1];
          const Box merged{a.x0, a.y0 < b.y0 ? a.y0 : b.y0, b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
          // Saving CASET + RASET + RAMWR saves 11 SPI bytes. At RGB565 it
          // pays for at most five extra pixels; don't merge distant traces.
          if (merged.area() <= a.area() + b.area() + 5) {
            boxes[used - 1] = merged;
            continue;
          }
        }
        boxes[used++] = b;
      }
      unsigned tileCost = 0, rowCost = 0, writtenRows = 0;
      for (unsigned i = 0; i < used; ++i)
        tileCost += 2 * boxes[i].area() + 11;
      for (unsigned y = 0; y < count;) {
        if (!(dirtyRows & (1u << y))) {
          ++y;
          continue;
        }
        const unsigned start = y++;
        while (y < count && (dirtyRows & (1u << y)))
          ++y;
        rowCost += 2 * Width * (y - start) + 11;
      }
      auto send = [&](const Box &b) {
        const unsigned width = b.x1 - b.x0 + 1, height = b.y1 - b.y0 + 1;
        for (unsigned y = 0; y < height; ++y) {
          memcpy(stripe + y * width, shadow + (first + b.y0 + y) * Width + b.x0, width * 2);
          writtenRows |= 1u << (b.y0 + y);
        }
        write(b.x0, first + b.y0, width, height, stripe);
        result.pixels += width * height;
        ++result.rectangles;
      };
      if (rowCost < tileCost) {
        // Dense changes in separated rows can cost less as full-width runs.
        for (unsigned y = 0; y < count;) {
          if (!(dirtyRows & (1u << y))) {
            ++y;
            continue;
          }
          const unsigned start = y++;
          while (y < count && (dirtyRows & (1u << y)))
            ++y;
          send({0, start, Width - 1, y - 1});
        }
      } else
        for (unsigned i = 0; i < used; ++i)
          send(boxes[i]);
      for (unsigned y = 0; y < count; ++y)
        if (writtenRows & (1u << y))
          ++result.rows;
    }
    valid = shadow != nullptr;
    return result;
  }
};
