#include "AllpowersCore.h"
#include "HidTelemetryServer.h"
#include "HistorySample.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <vector>

// Keep workload entry points visible in sampling and Callgrind profiles while
// leaving the actual product functions optimized exactly as in a normal build.
#if defined(_MSC_VER)
#define AP_BENCH_NOINLINE __declspec(noinline)
#else
#define AP_BENCH_NOINLINE __attribute__((noinline))
#endif

namespace LogicBench {
using Clock = std::chrono::steady_clock;
using HistoryFormat::Record;
using namespace HidTelemetry;

struct Options {
  uint32_t iterations = 100, records = HistoryFormat::kKeep;
  std::string_view scenario = "all";
  bool csv = false, help = false;
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

uint32_t number(std::string_view value, uint32_t limit) {
  uint32_t result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result &&
              result <= limit,
          "number is outside the supported range");
  return result;
}

Options options(int argc, char **argv) {
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--csv")
      result.csv = true;
    else if (arg == "--help")
      result.help = true;
    else {
      require(arg == "--iterations" || arg == "--records" || arg == "--scenario",
              "unknown argument");
      require(i + 1 < argc, "argument needs a value");
      const std::string_view value = argv[++i];
      if (arg == "--iterations")
        result.iterations = number(value, 100000);
      else if (arg == "--records")
        result.records = number(value, HistoryFormat::kKeep);
      else {
        require(value == "all" || value == "telemetry" || value == "history" || value == "hid",
                "scenario must be all, telemetry, history or hid");
        result.scenario = value;
      }
    }
  }
  return result;
}

struct Frame {
  std::array<uint8_t, 16> main{0xa5, 0x65, 0xb1, 0, 1, 8, 1, 0};
  std::array<uint8_t, 14> settings{0xa5, 0x65, 0xb1, 0, 1, 6, 3, 0};
};

template <size_t N> void sealFrame(std::array<uint8_t, N> &frame) {
  frame.back() = 0;
  for (size_t i = 0; i + 1 < N; ++i)
    frame.back() ^= frame[i];
}

void bigEndian(uint8_t *p, uint16_t value) {
  p[0] = uint8_t(value >> 8);
  p[1] = uint8_t(value);
}

// Generated protocol packets only: no captures, addresses or device handles.
std::vector<Frame> frames(uint32_t count) {
  std::vector<Frame> result(count);
  for (uint32_t i = 0; i < count; ++i) {
    auto &f = result[i];
    f.main[7] = uint8_t(i % 4);
    f.main[8] = uint8_t(i % 101);
    bigEndian(f.main.data() + 9, uint16_t(i % 2401));
    bigEndian(f.main.data() + 11, uint16_t((i * 7) % 2501));
    bigEndian(f.main.data() + 13, uint16_t(60 + i % 1440));
    f.settings[7] = i % 2 ? 0x10 : 0;
    bigEndian(f.settings.data() + 9, i % 2 ? 0xffff : 0);
    sealFrame(f.main);
    sealFrame(f.settings);
  }
  return result;
}

std::vector<Record> samples(const std::vector<Frame> &input) {
  std::vector<Record> result;
  AllpowersCore::Session session(AllpowersModels::Model::R2500V2);
  for (uint32_t i = 0; i < input.size(); ++i) {
    const auto &f = input[i];
    const uint32_t now = i * 60000;
    require(session.receive(f.main.data(), f.main.size(), now) &&
                session.receive(f.settings.data(), f.settings.size(), now),
            "synthetic packet rejected");
    auto r = HistoryFormat::sample(session.telemetry, now);
    r.session = 1;
    r.uptimeSeconds = i * 60;
    r.sequence = i + 1;
    r.crc = HistoryFormat::crc32(&r, 28);
    result.push_back(r);
  }
  return result;
}

struct Measurement {
  const char *name;
  const char *unit;
  uint64_t operations;
  double milliseconds;
  uint64_t checksum;
};

template <typename Work>
Measurement measure(const char *name, const char *unit, uint64_t operations, Work work) {
  const auto start = Clock::now();
  const uint64_t checksum = work();
  const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  return {name, unit, operations, elapsed, checksum};
}

AP_BENCH_NOINLINE uint64_t runTelemetry(const Options &o, const std::vector<Frame> &input) {
  AllpowersCore::Session session(AllpowersModels::Model::R2500V2);
  uint64_t checksum = 0;
  for (uint32_t pass = 0; pass < o.iterations; ++pass) {
    for (uint32_t i = 0; i < input.size(); ++i) {
      const auto &f = input[i];
      const uint32_t now = i * 60000 + pass;
      require(session.receive(f.main.data(), f.main.size(), now) &&
                  session.receive(f.settings.data(), f.settings.size(), now),
              "telemetry decode failed");
      require(session.fresh(now) && session.telemetry.soc == i % 101 &&
                  session.telemetry.outputWatts == (i * 7) % 2501,
              "telemetry value mismatch");
      if (i % 17 == 0) {
        auto corrupt = f.main;
        corrupt.back() ^= 1;
        require(!session.receive(corrupt.data(), corrupt.size(), now), "bad checksum accepted");
      }
      const auto fresh = HistoryFormat::sample(session.telemetry, now);
      const auto stale = HistoryFormat::sample(session.telemetry, now + 15001);
      require((fresh.flags & HistoryFormat::Fresh) && !(stale.flags & HistoryFormat::Fresh) &&
                  stale.soc == 255,
              "freshness policy failed");
      checksum += fresh.outputWatts + fresh.soc + session.telemetry.validFrames +
                  session.telemetry.invalidFrames + stale.soc;
    }
  }
  return checksum;
}

AP_BENCH_NOINLINE uint64_t runHistoryAppend(const Options &o, HistoryFormat::Ring &ring,
                                            const std::vector<Record> &input) {
  uint64_t checksum = 0;
  for (uint32_t pass = 0; pass < o.iterations; ++pass) {
    for (const auto &sample : input) {
      auto r = sample;
      r.session = pass + 1;
      require(ring.append(r), "RAM history append failed");
      checksum += r.sequence + uint64_t(r.crc);
    }
  }
  return checksum;
}

AP_BENCH_NOINLINE uint64_t runHistoryRecover(const Options &o, HistoryFormat::MemoryFlash &flash,
                                             uint32_t expectedLatest) {
  uint64_t checksum = 0;
  for (uint32_t pass = 0; pass < o.iterations; ++pass) {
    HistoryFormat::Ring restored(flash);
    require(restored.recover() && restored.latestSequence() == expectedLatest,
            "history recovery failed");
    Record last{};
    require(restored.readSequence(expectedLatest, last), "recovered record missing");
    checksum += last.sequence + uint64_t(last.crc);
  }
  return checksum;
}

class ArchiveProvider final : public Provider {
  const std::vector<Record> &input;
  std::array<std::vector<Record>, 2> storage;
  std::array<HistoryFormat::FrozenArchive, 2> frozen;

public:
  explicit ArchiveProvider(const std::vector<Record> &records)
      : input(records),
        storage{std::vector<Record>(records.size()), std::vector<Record>(records.size())},
        frozen{HistoryFormat::FrozenArchive(storage[0].data(), uint32_t(records.size())),
               HistoryFormat::FrozenArchive(storage[1].data(), uint32_t(records.size()))} {}
  size_t text(Op, uint8_t *, size_t) override {
    return 0;
  }
  HistoryFormat::ReadState open(unsigned slot, HistoryFormat::ExportMetadata &meta,
                                uint32_t &digest) override {
    const auto count = uint32_t(input.size());
    const auto state = frozen.at(slot).freeze(input.data(), 0, count, count, 1, 1, meta);
    digest = HistoryFormat::kDigestSeed;
    for (const auto &r : input)
      digest = HistoryFormat::digestRecord(digest, r);
    return state;
  }
  bool record(unsigned slot, uint32_t ordinal, Record &out) override {
    return frozen.at(slot).read(ordinal, out) == HistoryFormat::ReadState::Record;
  }
  void close(unsigned) override {}
};

// Includes request encoding/validation, the real server, response fragmentation,
// receiver assembly and CRC validation. No USB scheduling or physical I/O.
void exchange(Server &server, const Request &request, uint32_t now, Assembly &received) {
  uint8_t requestWire[Payload], report[Payload], message[MessageLimit];
  Request decoded;
  encode(request, requestWire);
  require(decode(requestWire, Payload, decoded), "HID request decode failed");
  size_t size = 0;
  const auto result = server.respond(decoded, now, message, size);
  require(result == Result::Ok, "HID server rejected request");
  const auto digest = crc32(message, size);
  received.expect(request);
  for (size_t offset = 0; offset < size;) {
    const auto count = fragment(decoded, result, message, size, digest, offset, report);
    require(count != 0, "HID fragmentation stalled");
    offset += count;
    require(received.accept(report, Payload) == (offset == size ? 1 : 0), "HID reassembly failed");
  }
  require(received.size() == size && received.status() == Result::Ok, "HID response mismatch");
}

AP_BENCH_NOINLINE uint64_t runHidArchive(const Options &o, Server &server,
                                         const std::vector<Record> &input) {
  uint64_t checksum = 0;
  uint64_t operation = 0;
  Assembly received;
  for (uint32_t pass = 0; pass < o.iterations; ++pass) {
    Request request;
    request.boot = 1;
    put64(request.archive.bytes, uint64_t(pass) + 1);
    put64(request.operation.bytes, ++operation);
    request.op = Op::Open;
    exchange(server, request, pass, received);
    require(received.size() == 24 && get32(received.bytes + 8) == input.size(),
            "archive size mismatch");
    const auto expectedDigest = get32(received.bytes + 20);
    uint32_t digest = HistoryFormat::kDigestSeed;
    for (uint32_t first = 0; first < input.size(); first += 8) {
      request.op = Op::Page;
      request.ordinal = first;
      put64(request.operation.bytes, ++operation);
      exchange(server, request, pass, received);
      const auto count = std::min(uint32_t(8), uint32_t(input.size()) - first);
      require(received.size() == 4 + count * 32 && get32(received.bytes) == first,
              "archive page mismatch");
      for (uint32_t i = 0; i < count; ++i) {
        Record r{};
        require(decodeRecord(received.bytes + 4 + i * 32, r) &&
                    !memcmp(&r, &input[first + i], sizeof(r)),
                "archive record mismatch");
        digest = HistoryFormat::digestRecord(digest, r);
      }
    }
    require(digest == expectedDigest, "archive digest mismatch");
    checksum += digest;
    request.op = Op::Close;
    request.ordinal = 0;
    put64(request.operation.bytes, ++operation);
    exchange(server, request, pass, received);
  }
  server.stop();
  return checksum;
}

void print(const Measurement &m, const Options &o) {
  const double ns = m.milliseconds * 1000000.0 / double(m.operations);
  if (o.csv)
    std::printf("%s,%s,%u,%u,%llu,%.3f,%.1f,%llu\n", m.name, m.unit, o.iterations, o.records,
                static_cast<unsigned long long>(m.operations), m.milliseconds, ns,
                static_cast<unsigned long long>(m.checksum));
  else
    std::printf("%-17s %10llu %-7s %10.3f ms %12.1f ns/%s  checksum=%llu\n", m.name,
                static_cast<unsigned long long>(m.operations), m.unit, m.milliseconds, ns, m.unit,
                static_cast<unsigned long long>(m.checksum));
}

int run(const Options &o) {
  const auto input = frames(o.records);
  const auto records = samples(input);
  if (o.csv)
    std::puts("scenario,unit,iterations,records,operations,elapsed_ms,ns_per_operation,checksum");
  else {
    std::puts("ALLPOWERS portable logic: synthetic input, RAM storage, no device access.");
    std::puts("Optimized host CPU timings include validation; they are not ESP32 timings.");
  }
  const uint64_t operations = uint64_t(o.iterations) * o.records;
  if (o.scenario == "all" || o.scenario == "telemetry")
    print(measure("telemetry", "sample", operations, [&] { return runTelemetry(o, input); }), o);
  if (o.scenario == "all" || o.scenario == "history") {
    std::vector<uint8_t> memory(HistoryFormat::kBytes);
    HistoryFormat::MemoryFlash flash(memory.data());
    HistoryFormat::Ring ring(flash);
    require(ring.recover(), "empty history recovery failed");
    // Always exercise sector erasure and wrap, even in the short smoke run.
    for (uint32_t i = 0; i < HistoryFormat::kSlots; ++i) {
      auto r = records[i % records.size()];
      require(ring.append(r), "history warmup failed");
    }
    print(measure("history-append", "record", operations,
                  [&] { return runHistoryAppend(o, ring, records); }),
          o);
    print(measure("history-recover", "scan", o.iterations,
                  [&] { return runHistoryRecover(o, flash, ring.latestSequence()); }),
          o);
  }
  if (o.scenario == "all" || o.scenario == "hid") {
    ArchiveProvider provider(records);
    Server server(provider);
    print(measure("hid-archive", "archive", o.iterations,
                  [&] { return runHidArchive(o, server, records); }),
          o);
  }
  return 0;
}
} // namespace LogicBench

int main(int argc, char **argv) {
  try {
    const auto o = LogicBench::options(argc, argv);
    if (o.help) {
      std::puts(
          "Usage: allpowers_logic_bench [--scenario all|telemetry|history|hid]\n"
          "       [--iterations 1..100000] [--records 1..2880] [--csv]\n"
          "Defaults: all scenarios, 100 iterations, 2880 records. --csv writes CSV to stdout.\n"
          "Each sample decodes two R2500 V2 frames; every 17th also tests a corrupt frame.\n"
          "A history scan reads 4096 slots. One HID archive transfers --records records.\n"
          "Setup is outside the timers. All runs validate their results and print checksums.");
      return 0;
    }
    return LogicBench::run(o);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "logic benchmark: %s\n", error.what());
    return 1;
  }
}
