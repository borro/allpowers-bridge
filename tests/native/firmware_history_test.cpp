#include <array>
#include <cassert>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include "HistoryRing.h"
#include "GraphHistory.h"

static void deviceContracts() {
  using namespace HistoryFormat;
  {
    auto a = example(), b = a;
    a.sequence = 1; b.sequence = 2;
    b.uptimeSeconds = 90;
    assert(GraphHistory::join(a, b));
    b.uptimeSeconds = 91;
    assert(!GraphHistory::join(a, b));
    b.uptimeSeconds = 0;
    assert(GraphHistory::join(a, b));
    a.flags &= ~Fresh;
    assert(!GraphHistory::join(a, b));
    std::array<GraphHistory::Point, 4> points{};
    for (auto &p : points) {
      p.sample = example(); p.sample.uptimeSeconds = 10;
      p.joinPrevious = p.unknownBefore = true;
    }
    assert(!GraphHistory::assignAges(points.data(), 2, 10, 1));
    assert(points[0].age == 0 && points[1].age == 0);
    assert(!points[0].unknownBefore && !points[1].unknownBefore);
    assert(points[2].unknownBefore && points[2].joinPrevious); // Count is a bound.
    points[1].sample.session = 2;
    assert(GraphHistory::assignAges(points.data(), 2, 15, 1));
    assert(points[1].unknownBefore);
    for (auto &p : points) { p.sample.flags |= Clock; p.sample.epochSeconds = 100; }
    assert(!GraphHistory::assignAges(points.data(), 2, 15, 2));
    assert(points[0].age == 5 && points[1].age == 5);
    points[1].sample.epochSeconds = 200;
    for (unsigned missing : {0u, 1u}) {
      points[missing].sample.flags &= ~Clock;
      assert(GraphHistory::assignAges(points.data(), 2, 15, 2));
      assert(points[0].age == 15 && points[1].unknownBefore);
      points[missing].sample.flags |= Clock;
    }
    points[0].sample = points[1].sample = example();
    points[1].sample.uptimeSeconds = 10;
    assert(!GraphHistory::assignAges(points.data(), 2, GraphHistory::kHorizon, 1));
    assert(points[0].age == GraphHistory::kHorizon);
    points[0].sample.session = 2;
    assert(GraphHistory::assignAges(points.data(), 1, 15, 1));
    assert(points[0].unknownBefore && points[0].age == 15);
  }
}

int main() {
  deviceContracts();
  std::array<HistoryFormat::Record, GraphHistory::kCapacity> graph{};
  assert(GraphHistory::selfTest(graph.data()));
}
