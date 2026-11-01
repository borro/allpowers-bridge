#pragma once

namespace GraphRendererTests {
struct Canvas {
  int width, height;
  std::vector<uint16_t> pixels;
  unsigned primitives = 0;
  Canvas(int w, int h) : width(w), height(h), pixels(size_t(w * h)) {}
  void rect(int x, int y, int w, int h, uint16_t color) {
    ++primitives;
    for (int j = std::max(0, y); j < std::min(height, y + h); ++j)
      for (int i = std::max(0, x); i < std::min(width, x + w); ++i)
        pixels[size_t(j * width + i)] = color;
  }
  // Frozen original line/rect path, independent of FrameRaster.
  void oldLine(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1,
        error = dx + dy;
    for (;;) {
      rect(x0, y0, 1, 1, color);
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
  void line(int x0, int y0, int x1, int y1, uint16_t color) {
    ++primitives;
    FrameRaster::line(pixels.data(), width, height, x0, y0, x1, y1, color);
  }
};
// Original two plot loops, including their different gap-marker ordering.
template <bool Compact>
void reference(Canvas &c, const GraphHistory::Point *p, size_t n, uint32_t maximum) {
  constexpr int top = Compact ? 23 : 40, bottom = Compact ? 61 : 144;
  auto gx = [](uint32_t age) {
    int x = GraphHistory::x(age);
    return Compact ? 22 + (x - 32) * 110 / 248 : x;
  };
  bool previous = false;
  int px = 0, ps = 0, po = 0, pc = 0;
  for (size_t i = 0; i < n; ++i) {
    const auto &v = p[i];
    const auto &r = v.sample;
    if (v.age > GraphHistory::kHorizon || !(r.flags & HistoryFormat::Fresh)) {
      previous = false;
      continue;
    }
    int x = gx(v.age), ys = bottom - int(uint32_t(r.soc) * (bottom - top) / 100),
        yo = bottom - int(uint32_t(r.outputWatts) * (bottom - top) / maximum),
        yc = bottom - int(uint32_t(r.chargeWatts) * (bottom - top) / maximum);
    if constexpr (!Compact)
      if (v.unknownBefore)
        for (int y = 41; y < 144; y += 5)
          c.oldLine(x, y, x, y + 1, 0xffe0);
    if (previous && v.joinPrevious) {
      c.oldLine(px, ps, x, ys, 0x07e0);
      c.oldLine(px, po, x, yo, 0xffff);
      c.oldLine(px, pc, x, yc, 0x07ff);
    } else if constexpr (Compact) {
      c.rect(x, ys, 1, 1, 0x07e0);
      c.rect(x, yo, 1, 1, 0xffff);
      c.rect(x, yc, 1, 1, 0x07ff);
    } else {
      c.rect(x - 1, ys - 1, 3, 3, 0x07e0);
      c.rect(x - 1, yo - 1, 3, 3, 0xffff);
      c.rect(x - 1, yc - 1, 3, 3, 0x07ff);
    }
    if constexpr (Compact)
      if (v.unknownBefore)
        for (int y = top; y < bottom; y += 4)
          c.rect(x, y, 1, 2, 0xffe0);
    px = x;
    ps = ys;
    po = yo;
    pc = yc;
    previous = true;
  }
  for (size_t i = 1; i < n; ++i) {
    if (p[i].age > GraphHistory::kHorizon)
      continue;
    auto change = GraphHistory::sourceChange(p[i - 1].sample, p[i].sample);
    if (change != GraphHistory::SourceChange::None) {
      int x = gx(p[i].age);
      c.oldLine(x, top, x, bottom,
                change == GraphHistory::SourceChange::ToBattery ? 0xf800 : 0x07e0);
    }
  }
}
template <bool Compact>
void compare(const GraphHistory::Projection &projection,
             const std::vector<GraphHistory::Point> &points, uint32_t maximum,
             size_t count = SIZE_MAX) {
  if (count == SIZE_MAX)
    count = points.size();
  Canvas old(Compact ? 160 : 320, Compact ? 80 : 172), fresh(old.width, old.height);
  for (size_t i = 0; i < old.pixels.size(); ++i)
    old.pixels[i] = fresh.pixels[i] = uint16_t(i * 11 + 7);
  reference<Compact>(old, points.data(), count, maximum);
  GraphHistory::Projection::Cursor cursor(projection);
  GraphRenderer::draw<Compact>(
      points.data(), count, maximum, {0x07e0, 0xffff, 0x07ff, 0xffe0, 0xf800},
      [&](uint32_t age) { return cursor.x(age); },
      [&](int x0, int y0, int x1, int y1, uint16_t color) { fresh.line(x0, y0, x1, y1, color); },
      [&](int x, int y, int w, int h, uint16_t color) { fresh.rect(x, y, w, h, color); });
  assert(fresh.pixels == old.pixels);
}
inline void run() {
  GraphHistory::Projection projection;
  assert(projection.x(60) == GraphHistory::x(60));
  GraphHistory::Projection::Cursor cold(projection);
  assert(cold.x(60) == GraphHistory::x(60));
  projection.begin();
  GraphHistory::Projection::Cursor ascending(projection), descending(projection),
      shuffled(projection);
  for (uint32_t age = 0; age <= GraphHistory::kHorizon + 1; ++age) {
    assert(projection.x(age) == GraphHistory::x(age));
    assert(ascending.x(age) == GraphHistory::x(age));
    assert(ascending.x(age) == GraphHistory::x(age));
    const uint32_t reverse = GraphHistory::kHorizon + 1 - age;
    assert(descending.x(reverse) == GraphHistory::x(reverse));
    const uint32_t other = uint32_t(uint64_t(age) * 7919 % (GraphHistory::kHorizon + 2));
    assert(shuffled.x(other) == GraphHistory::x(other));
  }
  assert(ascending.x(UINT32_MAX) == 32);
  assert(projection.x(UINT32_MAX) == 32);
  for (int x0 = -3; x0 <= 10; ++x0)
    for (int y0 = -3; y0 <= 12; ++y0)
      for (int x1 = -3; x1 <= 10; ++x1)
        for (int y1 = -3; y1 <= 12; ++y1) {
          Canvas old(7, 9), fresh(7, 9);
          old.oldLine(x0, y0, x1, y1, 0x1234);
          fresh.line(x0, y0, x1, y1, 0x1234);
          assert(old.pixels == fresh.pixels);
        }
  FrameRaster::line(nullptr, 1, 1, 0, 0, 1, 1, 0);
  std::vector<GraphHistory::Point> points;
  compare<false>(projection, points, 100);
  compare<true>(projection, points, 100);
  uint32_t random = 13579;
  for (unsigned run = 0; run < 32; ++run) {
    points.resize(run == 31 ? GraphHistory::kCapacity : 1 + run * 13);
    for (size_t i = 0; i < points.size(); ++i) {
      random = random * 1664525u + 1013904223u;
      auto &p = points[i];
      p.sample = HistoryFormat::example();
      p.sample.sequence = uint32_t(i + 1);
      p.sample.uptimeSeconds = uint32_t(i * 60);
      p.sample.session = 1 + unsigned(i > points.size() / 2);
      p.age = uint32_t(points.size() - 1 - i) * 15;
      p.joinPrevious = i % 19 != 0;
      p.unknownBefore = i % 23 == 0;
      p.sample.flags = uint16_t(HistoryFormat::Fresh |
                                (i % 4 < 2 ? HistoryFormat::Mains : HistoryFormat::Battery));
      if (run % 3 == 0) {
        p.sample.soc = 100;
        p.sample.outputWatts = 0;
        p.sample.chargeWatts = 0;
      } else if (i % 4 == 0 && i) {
        p.sample.soc = points[i - 1].sample.soc;
        p.sample.outputWatts = points[i - 1].sample.outputWatts;
        p.sample.chargeWatts = points[i - 1].sample.chargeWatts;
        p.age = points[i - 1].age;
      } else {
        p.sample.soc = uint8_t(random % 101);
        p.sample.outputWatts = uint16_t((random >> 8) % 1001);
        p.sample.chargeWatts = uint16_t((random >> 16) % 1001);
      }
      if (run % 5 == 0 && i % 31 == 0)
        p.sample.flags = 0;
      if (run % 7 == 0 && i % 29 == 0)
        p.age = GraphHistory::kHorizon + 1;
    }
    compare<false>(projection, points, 1200);
    compare<true>(projection, points, 1200);
  }
  // Repeated centers after overlapping 3x3 dots and after a compact gap marker
  // must repaint once before subsequent idempotent triples can be coalesced.
  points.assign(5, {});
  for (auto &p : points) {
    p.sample = HistoryFormat::example();
    p.age = 10000;
    p.joinPrevious = true;
    p.sample.soc = 50;
    p.sample.outputWatts = 49;
    p.sample.chargeWatts = 90;
  }
  points[0].unknownBefore = true;
  compare<false>(projection, points, 100);
  compare<true>(projection, points, 100);
  // Vary only one trace at the same x: these are never repeated triples.
  for (unsigned trace = 0; trace < 3; ++trace) {
    points.assign(3, {});
    for (unsigned i = 0; i < 3; ++i) {
      auto &p = points[i];
      p.sample = HistoryFormat::example();
      p.sample.soc = uint8_t(trace == 0 ? 30 + i * 10 : 80);
      p.sample.outputWatts = uint16_t(trace == 1 ? 30 + i * 10 : 20);
      p.sample.chargeWatts = uint16_t(trace == 2 ? 30 + i * 10 : 90);
      p.age = 10000;
      p.joinPrevious = true;
    }
    compare<false>(projection, points, 100);
    compare<true>(projection, points, 100);
  }
  points.assign(3, {});
  for (unsigned i = 0; i < 3; ++i) {
    auto &p = points[i];
    p.sample = HistoryFormat::example();
    p.sample.soc = 50;
    p.sample.flags = HistoryFormat::Fresh | HistoryFormat::Mains;
    p.sample.uptimeSeconds = i * 60;
    p.sample.sequence = i + 1;
    p.age = GraphHistory::kHorizon + 1 - i;
  }
  compare<false>(projection, points, 100, 2);
  compare<true>(projection, points, 100, 2);
  points[1].sample.flags = HistoryFormat::Fresh | HistoryFormat::Battery;
  compare<false>(projection, points, 100, 2);
  compare<true>(projection, points, 100, 2);
  // The backing array includes a valid third record excluded by count. Reading
  // it would add a visible source marker, without relying on an OOB crash.
  points[0].age = 1002;
  points[1].age = 1001;
  points[1].sample.flags = HistoryFormat::Fresh | HistoryFormat::Mains;
  points[2].age = 1000;
  points[2].sample.flags = HistoryFormat::Fresh | HistoryFormat::Battery;
  compare<false>(projection, points, 100, 2);
  compare<true>(projection, points, 100, 2);
}
} // namespace GraphRendererTests
