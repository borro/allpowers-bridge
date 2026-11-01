#pragma once
#include <Crc32.h>
#include <stddef.h>
#include <stdint.h>

#ifndef AP_FIRMWARE_PROFILING
#define AP_FIRMWARE_PROFILING 0
#endif

#if AP_FIRMWARE_PROFILING
#include <Arduino.h>
#include <esp_cpu.h>
#include <esp_timer.h>
#include <stdarg.h>
#include <stdio.h>
#endif

namespace RuntimeProfile {
enum class Metric : unsigned {
  LoopWork,
  Draw,
  BleDecode,
  HistoryRecord,
  HidRespond,
  HidCrc,
  HidFragment,
  HidSend,
  DisplayPresent,
  DisplaySend,
  StackSample,
  GraphCopy,
  GraphPlot,
  GraphCopyLocked,
  Count
};
enum class StackTask : unsigned { Loop, BleWorker, History, BleCallback, UsbCallback, Count };
#if AP_FIRMWARE_PROFILING
struct Timing {
  uint64_t calls = 0, us = 0, cycles = 0;
  uint32_t maxUs = 0;
};
inline portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
inline Timing timings[unsigned(Metric::Count)];
inline uint32_t lastPump = 0, pumpGapMax = 0;
inline uint64_t pumpGaps = 0, pumpGapUs = 0;
inline uint64_t hidCompleted = 0, hidFailed = 0;
inline uint64_t displayFrames = 0, displayRows = 0, displaySkipped = 0;
inline uint64_t displayPixels = 0, displayRectangles = 0;
inline uint32_t panelRuns = 0;
inline uint64_t panelOldUs = 0, panelNewUs = 0, panelOldPixels = 0, panelNewPixels = 0,
                panelOldRects = 0, panelNewRects = 0;
inline bool panelPassed = true, panelAllocationFailed = false;
inline uint32_t snapshotRuns = 0, snapshotPoints = 0, snapshotArchive = 0, snapshotLive = 0;
inline uint64_t snapshotOldUs = 0, snapshotNewUs = 0;
inline bool snapshotPassed = true, snapshotAllocationFailed = false;
inline void snapshotCompared(uint32_t points, uint32_t archive, uint32_t live, uint64_t oldUs,
                             uint64_t newUs, bool equal) {
  ++snapshotRuns;
  snapshotPoints = points;
  snapshotArchive = archive;
  snapshotLive = live;
  snapshotOldUs += oldUs;
  snapshotNewUs += newUs;
  snapshotPassed = snapshotPassed && equal;
}
inline void panelCompared(uint64_t oldUs, uint64_t newUs, uint32_t oldPixels, uint32_t newPixels,
                          uint32_t oldRects, uint32_t newRects, bool equal) {
  ++panelRuns;
  panelOldUs += oldUs;
  panelNewUs += newUs;
  panelOldPixels += oldPixels;
  panelNewPixels += newPixels;
  panelOldRects += oldRects;
  panelNewRects += newRects;
  panelPassed = panelPassed && equal;
}
inline uint32_t graphRuns = 0, graphPointsMin = UINT32_MAX, graphPointsMax = 0;
inline uint64_t graphOldUs = 0, graphNewUs = 0;
inline bool graphPassed = true, graphAllocationFailed = false;
inline void graphCompared(uint32_t points, uint64_t oldUs, uint64_t newUs, bool equal) {
  ++graphRuns;
  graphOldUs += oldUs;
  graphNewUs += newUs;
  graphPassed = graphPassed && equal;
  if (points < graphPointsMin)
    graphPointsMin = points;
  if (points > graphPointsMax)
    graphPointsMax = points;
}
inline bool displayShadow = false;
inline void displayed(unsigned rows, unsigned height, bool buffered, unsigned pixels,
                      unsigned rectangles) {
  ++displayFrames;
  displayRows += rows;
  displaySkipped += height - rows;
  displayShadow = buffered;
  displayPixels += pixels;
  displayRectangles += rectangles;
}
inline void hidSent(bool completed) {
  if (completed)
    ++hidCompleted;
  else
    ++hidFailed;
}
inline void record(Metric metric, uint64_t us, uint32_t cycles) {
  portENTER_CRITICAL(&lock);
  auto &t = timings[unsigned(metric)];
  ++t.calls;
  t.us += us;
  t.cycles += cycles;
  if (us > t.maxUs)
    t.maxUs = uint32_t(us);
  portEXIT_CRITICAL(&lock);
}
class Scope {
  Metric metric;
  uint64_t started;
  uint32_t cycles;

public:
  explicit Scope(Metric m)
      : metric(m), started(uint64_t(esp_timer_get_time())), cycles(esp_cpu_get_cycle_count()) {}
  ~Scope() {
    const uint32_t elapsed = esp_cpu_get_cycle_count() - cycles;
    record(metric, uint64_t(esp_timer_get_time()) - started, elapsed);
  }
};
struct StackMinimum {
  uint32_t freeBytes = UINT32_MAX, samples = 0, lastAt = 0;
  bool started = false;
  char task[configMAX_TASK_NAME_LEN]{};
};
inline StackMinimum stackMinimums[unsigned(StackTask::Count)];
// ESP-IDF returns bytes, unlike upstream FreeRTOS's word-based API. The kernel
// watermark includes all earlier calls in this task, not just this sample.
// Scan the current task only, outside critical sections, at most once/second
// per role. No borrowed task handles can outlive a task or race its deletion.
inline void sampleStack(StackTask role) {
  const uint32_t now = millis();
  auto &entry = stackMinimums[unsigned(role)];
  portENTER_CRITICAL(&lock);
  if (entry.started && uint32_t(now - entry.lastAt) < 1000) {
    portEXIT_CRITICAL(&lock);
    return;
  }
  entry.started = true;
  entry.lastAt = now;
  portEXIT_CRITICAL(&lock);
  Scope profile(Metric::StackSample);
  const uint32_t freeBytes = uint32_t(uxTaskGetStackHighWaterMark(nullptr));
  const char *name = pcTaskGetName(nullptr);
  portENTER_CRITICAL(&lock);
  if (freeBytes < entry.freeBytes)
    entry.freeBytes = freeBytes;
  ++entry.samples;
  strlcpy(entry.task, name, sizeof(entry.task));
  portEXIT_CRITICAL(&lock);
}
class StackSample {
  StackTask role;

public:
  explicit StackSample(StackTask task) : role(task) {}
  ~StackSample() {
    sampleStack(role);
  }
};
inline void pump() {
  const uint32_t now = uint32_t(esp_timer_get_time());
  if (lastPump) {
    const uint32_t gap = now - lastPump;
    ++pumpGaps;
    pumpGapUs += gap;
    if (gap > pumpGapMax)
      pumpGapMax = gap;
  }
  lastPump = now;
}
struct CrcTiming {
  uint32_t count = 0, minCycles = UINT32_MAX, maxCycles = 0;
  uint64_t cycles = 0;
};
inline constexpr size_t lengths[] = {28, 59, 260, 8192};
inline CrcTiming crc[4][3];
inline uint8_t input[8192 + 1];
inline uint32_t benchmarkStep = 0, checksum = 0;
inline bool initialized = false, crcPassed = true;
inline uint32_t bitwise(const void *data, size_t size) {
  auto *p = static_cast<const uint8_t *>(data);
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    c ^= p[i];
    for (unsigned b = 0; b < 8; ++b)
      c = (c >> 1) ^ (0xedb88320u & uint32_t(-int32_t(c & 1)));
  }
  return ~c;
}
// A volatile dispatch and result keep all three implementations observable.
// One call per loop bounds interference; the benchmark stops after 100 samples
// per length/backend. Live workload measurements are taken after completion.
using CrcFunction = uint32_t (*)(const void *, size_t);
inline CrcFunction volatile functions[] = {AllpowersCrc32::compute, AllpowersCrc32::computePortable,
                                           bitwise};
inline void step() {
  if (benchmarkStep >= 1200)
    return;
  if (!initialized) {
    for (size_t i = 0; i < sizeof(input); ++i)
      input[i] = uint8_t(i * 37 + 11);
    crcPassed = AllpowersCrc32::compute("123456789", 9) == 0xcbf43926u &&
                AllpowersCrc32::compute(nullptr, 0) == 0;
    initialized = true;
  }
  const unsigned length = (benchmarkStep / 3) % 4, backend = benchmarkStep % 3;
  const auto function = functions[backend];
  const uint32_t started = esp_cpu_get_cycle_count();
  const uint32_t result = function(input + 1, lengths[length]);
  const uint32_t elapsed = esp_cpu_get_cycle_count() - started;
  auto &t = crc[length][backend];
  ++t.count;
  t.cycles += elapsed;
  if (elapsed < t.minCycles)
    t.minCycles = elapsed;
  if (elapsed > t.maxCycles)
    t.maxCycles = elapsed;
  if (result != bitwise(input + 1, lengths[length]))
    crcPassed = false;
  checksum = (checksum * 16777619u) ^ result;
  ++benchmarkStep;
}
inline size_t append(char *out, size_t capacity) {
  Timing snapshot[unsigned(Metric::Count)];
  StackMinimum stacks[unsigned(StackTask::Count)];
  portENTER_CRITICAL(&lock);
  memcpy(snapshot, timings, sizeof(snapshot));
  memcpy(stacks, stackMinimums, sizeof(stacks));
  portEXIT_CRITICAL(&lock);
  static constexpr const char *names[] = {
      "loop_work",    "draw",         "ble_decode", "history_record",   "hid_respond",
      "hid_crc",      "hid_fragment", "hid_send",   "display_present",  "display_send",
      "stack_sample", "graph_copy",   "graph_plot", "graph_copy_locked"};
  size_t used = 0;
  bool ok = true;
  auto put = [&](const char *format, ...) {
    if (!ok)
      return;
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(out + used, capacity - used, format, args);
    va_end(args);
    if (n < 0 || size_t(n) >= capacity - used)
      ok = false;
    else
      used += size_t(n);
  };
  put(",\"profile\":{\"schema\":1,\"crc_backend\":\"esp_rom_crc32_le\",\"cpu_mhz\":%u,\"crc_done\":"
      "%s,\"crc_passed\":%s,\"crc_checksum\":%lu,\"uptime_us\":%llu,\"pump_gap_count\":%llu,\"pump_"
      "gap_us\":%llu,\"pump_gap_max_us\":%lu,\"hid_send_completed\":%llu,\"hid_send_failed\":%llu,"
      "\"metrics\":[",
      unsigned(getCpuFrequencyMhz()), benchmarkStep >= 1200 ? "true" : "false",
      crcPassed ? "true" : "false", (unsigned long)checksum,
      (unsigned long long)esp_timer_get_time(), (unsigned long long)pumpGaps,
      (unsigned long long)pumpGapUs, (unsigned long)pumpGapMax, (unsigned long long)hidCompleted,
      (unsigned long long)hidFailed);
  // The frame counters are appended outside the metrics array below.
  for (unsigned i = 0; i < unsigned(Metric::Count); ++i) {
    const auto &t = snapshot[i];
    put("%s{\"name\":\"%s\",\"calls\":%llu,\"elapsed_us\":%llu,\"cycles\":%llu,\"max_us\":%lu}",
        i ? "," : "", names[i], (unsigned long long)t.calls, (unsigned long long)t.us,
        (unsigned long long)t.cycles, (unsigned long)t.maxUs);
  }
  put("],\"display_frames\":%llu,\"display_rows\":%llu,\"display_skipped_rows\":%llu,"
      "\"display_shadow\":%s,\"display_pixels\":%llu,\"display_rectangles\":%llu,\"crc\":[",
      (unsigned long long)displayFrames, (unsigned long long)displayRows,
      (unsigned long long)displaySkipped, displayShadow ? "true" : "false",
      (unsigned long long)displayPixels, (unsigned long long)displayRectangles);
  for (unsigned length = 0; length < 4; ++length)
    for (unsigned backend = 0; backend < 3; ++backend) {
      const auto &t = crc[length][backend];
      put("%s{\"bytes\":%u,\"backend\":\"%s\",\"calls\":%lu,\"cycles\":%llu,\"min_cycles\":%lu,"
          "\"max_cycles\":%lu}",
          length || backend ? "," : "", unsigned(lengths[length]),
          backend == 0   ? "rom"
          : backend == 1 ? "table"
                         : "bitwise",
          (unsigned long)t.count, (unsigned long long)t.cycles, (unsigned long)t.minCycles,
          (unsigned long)t.maxCycles);
    }
  put("],\"stack_minimums\":[");
  static constexpr const char *roles[] = {"loop", "ble_worker", "history", "ble_callback",
                                          "usb_callback"};
  bool comma = false;
  for (unsigned i = 0; i < unsigned(StackTask::Count); ++i) {
    const auto &s = stacks[i];
    if (!s.samples)
      continue;
    put("%s{\"role\":\"%s\",\"task\":\"%s\",\"min_free_bytes\":%lu,\"samples\":%lu}",
        comma ? "," : "", roles[i], s.task, (unsigned long)s.freeBytes, (unsigned long)s.samples);
    comma = true;
  }
  put("],\"graph_benchmark\":{\"runs\":%lu,\"passed\":%s,\"allocation_failed\":%s,"
      "\"points_min\":%lu,\"points_max\":%lu,\"old_plot_us\":%llu,\"new_plot_us\":%llu}",
      (unsigned long)graphRuns, graphPassed ? "true" : "false",
      graphAllocationFailed ? "true" : "false", (unsigned long)(graphRuns ? graphPointsMin : 0),
      (unsigned long)graphPointsMax, (unsigned long long)graphOldUs,
      (unsigned long long)graphNewUs);
  put(",\"panel_benchmark\":{\"runs\":%lu,\"passed\":%s,\"allocation_failed\":%s,"
      "\"old_us\":%llu,\"new_us\":%llu,\"old_pixels\":%llu,\"new_pixels\":%llu,"
      "\"old_rectangles\":%llu,\"new_rectangles\":%llu}",
      (unsigned long)panelRuns, panelPassed ? "true" : "false",
      panelAllocationFailed ? "true" : "false", (unsigned long long)panelOldUs,
      (unsigned long long)panelNewUs, (unsigned long long)panelOldPixels,
      (unsigned long long)panelNewPixels, (unsigned long long)panelOldRects,
      (unsigned long long)panelNewRects);
  put(",\"snapshot_benchmark\":{\"runs\":%lu,\"passed\":%s,\"allocation_failed\":%s,"
      "\"points\":%lu,\"archive_records\":%lu,\"live_records\":%lu,\"old_us\":%llu,\"new_us\":%llu}"
      ","
      "\"page_control\":true}",
      (unsigned long)snapshotRuns, snapshotPassed ? "true" : "false",
      snapshotAllocationFailed ? "true" : "false", (unsigned long)snapshotPoints,
      (unsigned long)snapshotArchive, (unsigned long)snapshotLive,
      (unsigned long long)snapshotOldUs, (unsigned long long)snapshotNewUs);
  return ok ? used : 0;
}
#else
class Scope {
public:
  explicit Scope(Metric) {}
};
inline void pump() {}
inline void hidSent(bool) {}
inline void displayed(unsigned, unsigned, bool, unsigned, unsigned) {}
inline void step() {}
inline void sampleStack(StackTask) {}
class StackSample {
public:
  explicit StackSample(StackTask) {}
};
inline size_t append(char *, size_t) {
  return 0;
}
#endif
} // namespace RuntimeProfile
