#pragma once
#include "FrameRaster.h"
#include "GraphRenderer.h"
#include "RuntimeProfile.h"

// Diagnostic-only paired comparison on the same freshly copied point snapshot.
// Use a scratch canvas: neither historical records nor the visible frame change.
// At most one pair per graph refresh, five pairs per boot; release scratch RAM.
class GraphBenchmark {
#if AP_FIRMWARE_PROFILING
  uint16_t *pixels = nullptr;
  bool attempted = false;

public:
  template <bool Compact>
  void run(const GraphHistory::Point *points, size_t n, uint32_t maximum,
           const GraphHistory::Projection &projection, const GraphRenderer::Colors &colors) {
    if (RuntimeProfile::graphRuns >= 5 || RuntimeProfile::graphAllocationFailed ||
        RuntimeProfile::benchmarkStep < 1200)
      return;
    constexpr int width = Compact ? 160 : 320, height = Compact ? 80 : 172;
    if (!attempted) {
      pixels = static_cast<uint16_t *>(ps_malloc(width * height * sizeof(uint16_t)));
      attempted = true;
    }
    if (!pixels) {
      RuntimeProfile::graphAllocationFailed = true;
      return;
    }
    auto rect = [&](int x, int y, int w, int h, uint16_t color) {
      for (int j = y > 0 ? y : 0; j < (y + h < height ? y + h : height); ++j)
        for (int i = x > 0 ? x : 0; i < (x + w < width ? x + w : width); ++i)
          pixels[j * width + i] = color;
    };
    auto line = [&](int x0, int y0, int x1, int y1, uint16_t color) {
      FrameRaster::line(pixels, width, height, x0, y0, x1, y1, color);
    };
    uint64_t elapsed[2]{};
    uint32_t crc[2]{};
    for (unsigned order = 0; order < 2; ++order) {
      const unsigned backend = (order + RuntimeProfile::graphRuns) % 2;
      memset(pixels, 0, width * height * sizeof(uint16_t));
      const uint64_t started = uint64_t(esp_timer_get_time());
      if (!backend)
        GraphRenderer::draw<Compact, true, false>(
            points, n, maximum, colors, [&](uint32_t age) { return projection.x(age); }, line,
            rect);
      else {
        GraphHistory::Projection::Cursor cursor(projection);
        GraphRenderer::draw<Compact>(
            points, n, maximum, colors, [&](uint32_t age) { return cursor.x(age); }, line, rect);
      }
      elapsed[backend] = uint64_t(esp_timer_get_time()) - started;
      crc[backend] = AllpowersCrc32::compute(pixels, width * height * sizeof(uint16_t));
    }
    RuntimeProfile::graphCompared(uint32_t(n), elapsed[0], elapsed[1], crc[0] == crc[1]);
    if (RuntimeProfile::graphRuns >= 5) {
      free(pixels);
      pixels = nullptr;
    }
  }
#else
public:
  template <bool Compact>
  void run(const GraphHistory::Point *, size_t, uint32_t, const GraphHistory::Projection &,
           const GraphRenderer::Colors &) {}
#endif
};
