#include <array>
#include <cassert>
#include <initializer_list>
#include "BootGestures.h"
#include "StationPicker.h"
#include "HistoryRing.h"
#include "GraphHistory.h"
#include "HistoryWire.h"
#include "TelemetryLabels.h"
#include "HistoryExport.h"

static void deviceContracts() {
  using namespace HistoryFormat;
  assert(!StationPicker::sameMAC(nullptr, nullptr));
  assert(!StationPicker::sameMAC("02:00:00:00:00:01", nullptr));
  assert(!StationPicker::sameMAC(nullptr, "02:00:00:00:00:01"));
  {
    // Reserve an on-time second raw press even when debounce is polled late;
    // a late raw press must finish the first Single immediately, exactly once.
    using Event = BootGestures::Event;
    for (uint32_t base : {uint32_t(0), UINT32_C(0xffffff00)}) {
      for (uint32_t onset : {579u, 580u, 581u, 900u}) {
        BootGestures button;
        button.begin(false, base);
        auto poll = [&](bool down, uint32_t at) { return button.update(down, base + at); };
        assert(poll(true, 100) == Event::None);
        assert(poll(true, 130) == Event::None);
        assert(poll(false, 200) == Event::None);
        assert(poll(false, 230) == Event::None);
        const bool second = onset <= 230 + BootGestures::DoubleMs;
        assert(poll(true, onset) == (second ? Event::None : Event::Single));
        assert(poll(true, onset + 100) == Event::None);
        assert(poll(false, onset + 150) == Event::None);
        assert(poll(false, onset + 180) == (second ? Event::Double : Event::None));
        assert(poll(false, onset + 530) == (second ? Event::None : Event::Single));
        assert(poll(false, onset + 600) == Event::None);
      }
    }
  }
  {
    BootGestures button;
    button.begin(false, 0);
    assert(button.update(true, 10) == BootGestures::Event::None);
    assert(button.update(true, 40) == BootGestures::Event::None);
    // No polling while held: recognize exactly HoldMs at raw release.
    assert(button.update(false, 840) == BootGestures::Event::None);
    assert(button.update(false, 870) == BootGestures::Event::Hold);
    assert(button.update(false, 900) == BootGestures::Event::None);
  }
  {
    char mac[18]{};
    assert(StationPicker::canonicalizeMAC("02:00:00:00:00:FF", mac));
    assert(!strcmp(mac, "02:00:00:00:00:ff"));
    StationPicker::Catalog catalog;
    std::array<StationPicker::Entry, 8> entries{};
    for (unsigned i = 0; i < entries.size(); ++i) {
      strcpy(entries[i].identity.name, "Synthetic station");
      snprintf(entries[i].identity.address, 18, "02:00:00:00:00:%02x", i);
      assert(catalog.upsert(entries[i], 0));
    }
    for (int i = 0; i < 17; ++i) {
      assert(catalog.next());
      assert(catalog.snapshot().selectedIndex == i % 8);
    }
    assert(catalog.selectMAC(entries[6].identity.address));
    assert(catalog.snapshot(0).selectedRow == -1); // Exactly beyond this page.
    assert(catalog.snapshot(1).selectedRow == 5);
    assert(catalog.snapshot(6).selectedRow == 0);
    assert(catalog.snapshot(7).selectedRow == -1);
    assert(catalog.snapshot(8).count == 0 && catalog.snapshot(8).selectedRow == -1);
    assert(catalog.selectMAC(entries[7].identity.address));
    assert(catalog.snapshot(6).selectedRow == 1);
  }
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
  assert(BootGestures::selfTest());
  assert(StationPicker::selfTest());
  assert(telemetryLabelsSelfTest());
  std::array<HistoryFormat::Record, GraphHistory::kCapacity> graph{};
  assert(GraphHistory::selfTest(graph.data()));
}
