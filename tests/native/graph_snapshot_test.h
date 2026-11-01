#pragma once

namespace GraphSnapshotTests {
inline std::vector<GraphHistory::Point> reference(const GraphSnapshot::Input &in, bool &unknown) {
  std::vector<GraphHistory::Point> points;
  for (uint32_t i = 0; i < in.archive.count; ++i) {
    const auto &r = in.archive.data[(in.archive.head + i) % in.archive.capacity];
    if (in.live.count) {
      const auto &first = in.live.data[in.live.head];
      if (r.session == first.session && r.uptimeSeconds >= first.uptimeSeconds)
        continue;
    }
    points.push_back({r, 0, false, false});
  }
  for (uint32_t i = 0; i < in.live.count; ++i)
    points.push_back({in.live.data[(in.live.head + i) % in.live.capacity], 0, false, false});
  unknown = GraphHistory::assignAges(points.data(), points.size(), in.now, in.session);
  return points;
}
inline void compare(const GraphSnapshot::Input &in, size_t slack = 7) {
  bool unknown = false;
  auto expected = reference(in, unknown);
  const size_t capacity = in.archive.count + in.live.count + slack;
  for (unsigned backend = 0; backend < 2; ++backend) {
    std::vector<GraphHistory::Point> memory(capacity + 2);
    for (auto &p : memory) {
      p.age = 0xdeadbeef;
      p.sample.magic = 0x12345678;
    }
    const auto got = backend ? GraphSnapshot::build(memory.data() + 1, capacity, in)
                             : GraphSnapshot::legacy(memory.data() + 1, capacity, in);
    assert(got.count == expected.size() && got.unknown == unknown);
    assert(memory.front().age == 0xdeadbeef && memory.back().age == 0xdeadbeef);
    if (got.count) {
      assert(got.points);
      assert(got.points >= memory.data() + 1 &&
             got.points + got.count <= memory.data() + capacity + 1);
      if (backend)
        assert(got.points == memory.data() + capacity + 1 - got.count);
    }
    for (size_t i = 0; i < got.count; ++i) {
      const auto &a = got.points[i], &b = expected[i];
      assert(!memcmp(&a.sample, &b.sample, sizeof(a.sample)) && a.age == b.age);
      assert(a.joinPrevious == b.joinPrevious && a.unknownBefore == b.unknownBefore);
    }
  }
}
inline void run() {
  compare({});
  // Equal-time boundaries and exact capacity must retain the complete view.
  std::array<HistoryFormat::Record, 2> equal{};
  for (auto &r : equal) {
    r = HistoryFormat::example();
    r.session = 1;
    r.uptimeSeconds = 123;
    r.epochSeconds = 456;
  }
  compare({{equal.data(), 0, 2, 2}, {}, 123, 1}, 0);
  compare({{}, {equal.data(), 0, 2, 2}, 123, 1}, 0);
  equal[0].flags |= HistoryFormat::Clock;
  equal[1].flags |= HistoryFormat::Clock;
  equal[1].session = 2;
  compare({{equal.data(), 0, 2, 2}, {}, 123, 2}, 0);
  equal[1].session = 1;
  equal[0].uptimeSeconds = 0;
  equal[1].uptimeSeconds = GraphHistory::kHorizon;
  compare({{equal.data(), 0, 2, 2}, {}, GraphHistory::kHorizon, 1}, 0);
  uint32_t random = 43217;
  for (unsigned test = 0; test < 240; ++test) {
    const unsigned ac = 1 + test % 31, lc = 1 + (test * 7) % 37;
    std::vector<HistoryFormat::Record> archive(ac), live(lc);
    GraphSnapshot::Input in{{archive.data(), test % ac, (test * 13) % (ac + 1), ac},
                            {live.data(), (test * 3) % lc, (test * 17) % (lc + 1), lc},
                            test % 4 ? 1000u : UINT32_MAX,
                            test % 5};
    auto fill = [&](GraphHistory::RingView view, bool recent) {
      for (unsigned i = 0; i < view.count; ++i) {
        random = random * 1664525u + 1013904223u;
        auto r = HistoryFormat::example();
        r.sequence = recent ? 0 : i + 1;
        r.session = 1 + (test + i / 7) % 4;
        r.uptimeSeconds = test % 3 ? i * (recent ? 1 : 60) : uint32_t(random % 500);
        r.epochSeconds = (test % 7 == 0 ? UINT32_MAX - 1000 : 10000) + r.uptimeSeconds;
        r.flags = uint8_t(random);
        r.soc = uint8_t(random % 101);
        r.outputWatts = uint16_t(random >> 16);
        (recent ? live : archive)[(view.head + i) % view.capacity] = r;
      }
    };
    fill(in.live, true);
    fill(in.archive, false);
    if (in.live.count)
      for (unsigned i = 0; i < in.archive.count; i += 3) {
        auto &r = archive[(in.archive.head + i) % ac];
        r.session = live[in.live.head].session;
        r.uptimeSeconds = live[in.live.head].uptimeSeconds + i;
      }
    compare(in);
    if (in.archive.count + in.live.count) {
      GraphHistory::Point dummy;
      assert(!GraphSnapshot::build(&dummy, in.archive.count + in.live.count - 1, in).count);
      assert(!GraphSnapshot::legacy(&dummy, in.archive.count + in.live.count - 1, in).count);
    }
  }
  std::vector<HistoryFormat::Record> archive(HistoryFormat::kKeep), live(GraphHistory::kLiveKeep);
  GraphSnapshot::Input full{
      {archive.data(), HistoryFormat::kKeep - 5, HistoryFormat::kKeep, HistoryFormat::kKeep},
      {live.data(), GraphHistory::kLiveKeep - 6, GraphHistory::kLiveKeep, GraphHistory::kLiveKeep},
      176600,
      1};
  for (unsigned i = 0; i < full.archive.count; ++i) {
    auto r = HistoryFormat::example();
    r.sequence = i + 1;
    r.uptimeSeconds = i * 60;
    archive[(full.archive.head + i) % archive.size()] = r;
  }
  for (unsigned i = 0; i < full.live.count; ++i) {
    auto r = HistoryFormat::example();
    r.sequence = 0;
    r.uptimeSeconds = 173000 + i;
    live[(full.live.head + i) % live.size()] = r;
  }
  compare(full);
  GraphHistory::Point dummy;
  assert(!GraphSnapshot::build(nullptr, 10, {}).count);
  for (auto bad :
       {GraphHistory::RingView{nullptr, 0, 1, 1}, GraphHistory::RingView{archive.data(), 1, 1, 1},
        GraphHistory::RingView{archive.data(), 0, 2, 1},
        GraphHistory::RingView{archive.data(), 0, 1, 0}}) {
    assert(!GraphSnapshot::build(&dummy, 1, {bad, {}, 0, 0}).count);
    assert(!GraphSnapshot::legacy(&dummy, 1, {{}, bad, 0, 0}).count);
    assert(!GraphSnapshot::freeze(bad, archive.data(), archive.size()));
  }
  assert(!GraphSnapshot::freeze(full.archive, archive.data(), 1));
  assert(!GraphSnapshot::freeze(full.archive, nullptr, archive.size()));
  assert(GraphSnapshot::freeze({}, nullptr, 0));
  std::vector<HistoryFormat::Record> frozen(archive.size());
  assert(GraphSnapshot::freeze(full.archive, frozen.data(), frozen.size()));
  for (unsigned i = 0; i < full.archive.count; ++i) {
    const unsigned index = (full.archive.head + i) % archive.size();
    assert(!memcmp(&archive[index], &frozen[index], sizeof(archive[index])));
  }
  GraphHistory::LiveRing ring(live.data());
  assert(ring.view().count == 0);
  ring.push(HistoryFormat::example());
  assert(ring.view().count == 1 && ring.view().capacity == GraphHistory::kLiveKeep);
}
} // namespace GraphSnapshotTests
