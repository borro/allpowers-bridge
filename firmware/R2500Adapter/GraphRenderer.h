#pragma once
#include "GraphHistory.h"

namespace GraphRenderer {
struct Colors {
  uint16_t soc, output, charge, unknown, battery;
};
// Preserve primitive order, gap markers and source-transition overlays. Only a
// complete repeated triple can be omitted: skipping a single trace could change
// the color where traces overlap. Unknown-boundary markers must still repaint.
template <bool Compact, bool Coalesce = true, bool CacheHeights = true, class Project, class Line,
          class Rect>
void draw(const GraphHistory::Point *points, size_t n, uint32_t maximum, const Colors &colors,
          Project project, Line line, Rect rect) {
  constexpr int top = Compact ? 23 : 40, bottom = Compact ? 61 : 144;
  auto gx = [&](uint32_t age) {
    const int x = project(age);
    return Compact ? 22 + (x - 32) * 110 / 248 : x;
  };
  bool previous = false, repeatedLast = false, previousMarker = false;
  int px = 0, ps = 0, po = 0, pc = 0;
  bool heightReady = false;
  uint16_t lastSoc = 0, lastOutput = 0, lastCharge = 0;
  int ys = 0, yo = 0, yc = 0;
  for (size_t i = 0; i < n; ++i) {
    const auto &p = points[i];
    const auto &r = p.sample;
    if (p.age > GraphHistory::kHorizon || !(r.flags & HistoryFormat::Fresh)) {
      previous = false;
      continue;
    }
    const int x = gx(p.age);
    // The scale is fixed for this draw. Reuse exact integer coordinates when
    // the corresponding value repeats, independently of gaps or source changes.
    if (!CacheHeights || !heightReady || r.soc != lastSoc)
      ys = bottom - int(uint32_t(r.soc) * (bottom - top) / 100);
    if (!CacheHeights || !heightReady || r.outputWatts != lastOutput)
      yo = bottom - int(uint32_t(r.outputWatts) * (bottom - top) / maximum);
    if (!CacheHeights || !heightReady || r.chargeWatts != lastCharge)
      yc = bottom - int(uint32_t(r.chargeWatts) * (bottom - top) / maximum);
    lastSoc = r.soc;
    lastOutput = r.outputWatts;
    lastCharge = r.chargeWatts;
    heightReady = true;
    if constexpr (!Compact) {
      if (p.unknownBefore)
        for (int y = 41; y < 144; y += 5)
          line(x, y, x, y + 1, colors.unknown);
    }
    const bool continuous = previous && p.joinPrevious;
    const bool repeated = continuous && px == x && ps == ys && po == yo && pc == yc;
    // The first repeated triple must repaint: earlier crossing lines or 3x3
    // dots may cover one of its centers. Only the next repetition is idempotent.
    if constexpr (Coalesce) {
      if (repeated && repeatedLast && !p.unknownBefore && !previousMarker)
        continue;
    }
    if (continuous) {
      line(px, ps, x, ys, colors.soc);
      line(px, po, x, yo, colors.output);
      line(px, pc, x, yc, colors.charge);
    } else if constexpr (Compact) {
      rect(x, ys, 1, 1, colors.soc);
      rect(x, yo, 1, 1, colors.output);
      rect(x, yc, 1, 1, colors.charge);
    } else {
      rect(x - 1, ys - 1, 3, 3, colors.soc);
      rect(x - 1, yo - 1, 3, 3, colors.output);
      rect(x - 1, yc - 1, 3, 3, colors.charge);
    }
    if constexpr (Compact) {
      if (p.unknownBefore)
        for (int y = top; y < bottom; y += 4)
          rect(x, y, 1, 2, colors.unknown);
    }
    px = x;
    ps = ys;
    po = yo;
    pc = yc;
    previous = true;
    repeatedLast = repeated;
    previousMarker = p.unknownBefore;
  }
  for (size_t i = 1; i < n; ++i) {
    const auto &p = points[i];
    if (p.age > GraphHistory::kHorizon)
      continue;
    const auto change = GraphHistory::sourceChange(points[i - 1].sample, p.sample);
    if (change != GraphHistory::SourceChange::None) {
      const int x = gx(p.age);
      line(x, top, x, bottom,
           change == GraphHistory::SourceChange::ToBattery ? colors.battery : colors.soc);
    }
  }
}
} // namespace GraphRenderer
