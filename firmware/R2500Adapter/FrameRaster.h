#pragma once
#include <stdint.h>
#include <stdlib.h>

namespace FrameRaster {
inline void line(uint16_t *pixels, int width, int height, int x0, int y0, int x1, int y1,
                 uint16_t color) {
  if (!pixels)
    return;
  if (y0 == y1) {
    if (unsigned(y0) >= unsigned(height))
      return;
    int left = x0 < x1 ? x0 : x1, right = x0 > x1 ? x0 : x1;
    if (left < 0)
      left = 0;
    if (right >= width)
      right = width - 1;
    for (int x = left; x <= right; ++x)
      pixels[y0 * width + x] = color;
    return;
  }
  if (x0 == x1) {
    if (unsigned(x0) >= unsigned(width))
      return;
    int top = y0 < y1 ? y0 : y1, bottom = y0 > y1 ? y0 : y1;
    if (top < 0)
      top = 0;
    if (bottom >= height)
      bottom = height - 1;
    for (int y = top; y <= bottom; ++y)
      pixels[y * width + x0] = color;
    return;
  }
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1,
      error = dx + dy;
  for (;;) {
    if (unsigned(x0) < unsigned(width) && unsigned(y0) < unsigned(height))
      pixels[y0 * width + x0] = color;
    if (x0 == x1 && y0 == y1)
      break;
    int twice = 2 * error;
    if (twice >= dy) {
      error += dy;
      x0 += sx;
    }
    if (twice <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}
} // namespace FrameRaster
