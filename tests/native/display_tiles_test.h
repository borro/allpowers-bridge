#pragma once

namespace DisplayTilesTests {
template <unsigned W, unsigned H, bool Little> void run() {
  DisplayTiles<W, H, Little> frame;
  std::vector<uint16_t> pixels(W * H), shadow(W * H, 0xdead), panel(W * H, 0xbeef), expected(W * H);
  for (unsigned i = 0; i < W * H; ++i)
    pixels[i] = uint16_t(i * 37 + 11);
  auto present = [&](bool rotated, bool buffered = true) {
    const unsigned logicalWidth = rotated ? H : W, logicalHeight = rotated ? W : H;
    for (unsigned y = 0; y < logicalHeight; ++y)
      for (unsigned x = 0; x < logicalWidth; ++x) {
        uint16_t c = pixels[y * logicalWidth + x];
        if (!Little)
          c = uint16_t(c << 8 | c >> 8);
        expected[rotated ? x * W + (W - 1 - y) : y * W + x] = c;
      }
    unsigned sentPixels = 0, rectangles = 0;
    std::vector<bool> rows(H);
    auto result = frame.present(
        pixels.data(), logicalWidth, buffered ? shadow.data() : nullptr,
        [&](unsigned x, unsigned y, unsigned width, unsigned height, const uint16_t *data) {
          assert(width && height && height <= 8 && x + width <= W && y + height <= H);
          assert(reinterpret_cast<uintptr_t>(data) % alignof(uint32_t) == 0);
          assert(((width * height * 2 + 3) & ~3u) <=
                 W * 8 * 2); // SPI's last rounded 32-bit read fits.
          unsigned command = 0;
          constexpr unsigned offsetX = W == 172 ? 34 : W == 80 ? 26 : 3, offsetY = W == 80 ? 1 : 7;
          PanelWindow::address(
              x, y, width, height, offsetX, offsetY,
              [&](uint8_t c, const uint8_t *bytes, size_t n) {
                const uint8_t codes[] = {0x2a, 0x2b, 0x2c};
                assert(command < 3 && c == codes[command]);
                if (command < 2) {
                  assert(n == 4 && reinterpret_cast<uintptr_t>(bytes) % alignof(uint32_t) == 0);
                  unsigned first = unsigned(bytes[0]) * 256 + bytes[1],
                           last = unsigned(bytes[2]) * 256 + bytes[3];
                  assert(first == (command ? offsetY + y : offsetX + x));
                  assert(last == first + (command ? height : width) - 1);
                } else
                  assert(n == 0 && bytes == nullptr);
                ++command;
              });
          assert(command == 3);
          for (unsigned row = 0; row < height; ++row) {
            rows[y + row] = true;
            for (unsigned col = 0; col < width; ++col) {
              assert(data[row * width + col] == expected[(y + row) * W + x + col]);
              panel[(y + row) * W + x + col] = data[row * width + col];
            }
          }
          sentPixels += width * height;
          ++rectangles;
        });
    assert(panel == expected);
    if (buffered)
      assert(shadow == expected);
    assert(result.rows == unsigned(std::count(rows.begin(), rows.end(), true)));
    assert(result.pixels == sentPixels && result.rectangles == rectangles);
    return result;
  };
  auto full = present(false);
  assert(full.pixels == W * H && full.rows == H);
  assert(frame.initialized());
  assert(present(false).pixels == 0);
  pixels[0] ^= 0x07e0;
  auto single = present(false);
  assert(single.pixels == 1 && single.rows == 1 && single.rectangles == 1);
  pixels[W * H - 1] ^= 0x1234;
  assert(present(false).pixels == 1);
  if constexpr (W > 32) {
    pixels[15] ^= 1;
    pixels[16] ^= 1;
    auto near = present(false);
    assert(near.pixels == 2 && near.rectangles == 1);
    pixels[0] ^= 1;
    pixels[31] ^= 1;
    auto far = present(false);
    assert(far.pixels == 2 && far.rectangles == 2);
    for (unsigned y : {0u, 7u}) {
      pixels[y * W + 14] ^= 1;
      pixels[y * W + 20] ^= 1;
      auto edge = present(false);
      // Five unchanged pixels cost 10 bytes; one saved window saves 11.
      assert(edge.pixels == 7 && edge.rectangles == 1);
    }
  }
  std::fill(pixels.begin(), pixels.end(), 0);
  present(false);
  for (unsigned y = 0; y < 8; ++y)
    pixels[y * W + 2] = 1;
  auto vertical = present(false);
  assert(vertical.pixels == 8 && vertical.rectangles == 1);
  std::fill(pixels.begin(), pixels.end(), 0);
  present(false);
  for (unsigned y : {1u, 2u, 5u})
    for (unsigned x = 0; x < W; ++x)
      pixels[y * W + x] = 1;
  auto runs = present(false);
  assert(runs.pixels == W * 3 && runs.rectangles == 2);
  std::fill(pixels.begin(), pixels.end(), 0);
  present(false);
  unsigned changedRows = 0;
  for (unsigned first = 0; first < H; first += 8) {
    unsigned last = std::min(first + 7, H - 1);
    ++changedRows;
    if (last != first)
      ++changedRows;
    for (unsigned x = 0; x < W; ++x) {
      pixels[first * W + x] = 1;
      pixels[last * W + x] = 1;
    }
  }
  auto dense = present(false);
  // Pick the cheaper wire encoding, including 11 address bytes per window.
  // On a five-pixel test panel one extra row can cost less than another window.
  assert(2 * dense.pixels + 11 * dense.rectangles <= 2 * W * changedRows + 11 * changedRows);
  if constexpr (W >= 16)
    assert(dense.pixels == W * changedRows);
  std::fill(pixels.begin(), pixels.end(), 0xffff);
  assert(present(false).pixels == W * H);
  present(true);
  assert(present(true).pixels == 0);
  pixels[0] ^= 1;
  assert(present(true).pixels == 1);
  pixels[W * H - 1] ^= 1;
  assert(present(true).pixels == 1);
  assert(present(true, false).pixels == W * H);
  assert(!frame.initialized());
  assert(present(true, false).pixels == W * H);
  assert(present(true).pixels == W * H);
  uint32_t random = 24681;
  for (unsigned run = 0; run < 64; ++run) {
    for (unsigned i = 0; i < run % 29; ++i) {
      random = random * 1664525u + 1013904223u;
      pixels[random % (W * H)] = uint16_t(random >> 16);
    }
    present(run % 3 == 0);
    assert(present(run % 3 == 0).pixels == 0);
  }
}
inline void highAddresses() {
  unsigned step = 0;
  PanelWindow::address(299, 600, 16, 8, 300, 400,
                       [&](uint8_t command, const uint8_t *data, size_t n) {
                         if (step < 2) {
                           assert(n == 4 && command == (step ? 0x2b : 0x2a));
                           const unsigned first = unsigned(data[0]) * 256 + data[1];
                           const unsigned last = unsigned(data[2]) * 256 + data[3];
                           assert(first == (step ? 1000u : 599u));
                           assert(last == (step ? 1007u : 614u));
                         } else
                           assert(command == 0x2c && !data && !n);
                         ++step;
                       });
  assert(step == 3);
}
} // namespace DisplayTilesTests
