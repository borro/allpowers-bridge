#pragma once
#include "HistoryStore.h"
#include "RuntimeProfile.h"

// Diagnostic-only reference/new comparison on one frozen pair of rings and a
// fixed timestamp. No flash writes, extra tasks or retained live-store pointers.
class HistorySnapshotBenchmark {
#if AP_FIRMWARE_PROFILING
  void *memory = nullptr;
  GraphSnapshot::Input input;
  bool frozen = false;
  static uint32_t digest(GraphSnapshot::View view) {
    uint32_t hash = 2166136261u;
    auto byte = [&](uint8_t value) { hash = (hash ^ value) * 16777619u; };
    for (size_t i = 0; i < view.count; ++i) {
      const auto &p = view.points[i];
      const auto *record = reinterpret_cast<const uint8_t *>(&p.sample);
      for (size_t j = 0; j < sizeof(p.sample); ++j)
        byte(record[j]);
      for (unsigned shift = 0; shift < 32; shift += 8)
        byte(uint8_t(p.age >> shift));
      byte(p.joinPrevious ? 1 : 0);
      byte(p.unknownBefore ? 1 : 0);
    }
    return hash;
  }

public:
  void run(HistoryStore &history) {
    if (RuntimeProfile::snapshotRuns >= 5 || RuntimeProfile::snapshotAllocationFailed ||
        RuntimeProfile::panelRuns < 5)
      return;
    constexpr size_t recordBytes = GraphHistory::kCapacity * sizeof(HistoryFormat::Record);
    if (!memory)
      memory = ps_malloc(recordBytes + GraphHistory::kCapacity * sizeof(GraphHistory::Point));
    if (!memory) {
      RuntimeProfile::snapshotAllocationFailed = true;
      return;
    }
    auto *archive = static_cast<HistoryFormat::Record *>(memory);
    auto *recent = archive + HistoryFormat::kKeep;
    auto *output = reinterpret_cast<GraphHistory::Point *>(recent + GraphHistory::kLiveKeep);
    if (!frozen) {
      if (!history.freezeGraphInput(archive, recent, input))
        return;
      frozen = true;
    }
    uint64_t elapsed[2]{};
    uint32_t hashes[2]{};
    size_t count[2]{};
    bool unknown[2]{};
    for (unsigned order = 0; order < 2; ++order) {
      const unsigned backend = (order + RuntimeProfile::snapshotRuns) % 2;
      const uint64_t started = uint64_t(esp_timer_get_time());
      const auto result = backend ? GraphSnapshot::build(output, GraphHistory::kCapacity, input)
                                  : GraphSnapshot::legacy(output, GraphHistory::kCapacity, input);
      elapsed[backend] = uint64_t(esp_timer_get_time()) - started;
      count[backend] = result.count;
      unknown[backend] = result.unknown;
      hashes[backend] = digest(result);
    }
    RuntimeProfile::snapshotCompared(
        uint32_t(count[0]), input.archive.count, input.live.count, elapsed[0], elapsed[1],
        count[0] == count[1] && unknown[0] == unknown[1] && hashes[0] == hashes[1]);
    if (RuntimeProfile::snapshotRuns >= 5) {
      free(memory);
      memory = nullptr;
      input = {};
    }
  }
#else
public:
  void run(HistoryStore &) {}
#endif
};
