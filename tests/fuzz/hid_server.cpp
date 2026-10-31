#include "HidTelemetryServer.h"
#include <array>
#include <cassert>
using namespace HidTelemetry;
struct Source : Provider {
  std::array<bool, 2> active{};
  std::array<uint32_t, 2> generation{};
  std::array<uint32_t, 2> counts{};
  uint32_t serial = 0;
  uint32_t count = 17;
  unsigned textMode = 0, openMode = 0;
  bool failRecord = false;
  size_t text(Op, uint8_t *out, size_t capacity) override {
    assert(capacity);
    if (textMode == 1) return 0;
    if (textMode == 2) return capacity + 1;
    const size_t size = textMode == 3 ? capacity : 1;
    memset(out, 'x', size); return size;
  }
  HistoryFormat::ReadState open(unsigned slot, HistoryFormat::ExportMetadata &m,
                                 uint32_t &digest) override {
    assert(slot < 2 && !active[slot]);
    if (openMode == 1) return HistoryFormat::ReadState::Busy;
    if (openMode == 2) return HistoryFormat::ReadState::Error;
    if (openMode == 3) return HistoryFormat::ReadState::End;
    active[slot] = true; generation[slot] = ++serial;
    counts[slot] = count;
    m = {1, serial, count, count ? 1u : 0u, count}; digest = 0;
    return HistoryFormat::ReadState::Record;
  }
  bool record(unsigned slot, uint32_t index, HistoryFormat::Record &out) override {
    assert(slot < 2 && active[slot] && index < counts[slot]);
    if (failRecord) return false;
    out = {}; out.magic = HistoryFormat::kMagic; out.sequence = index + 1;
    out.session = generation[slot]; out.crc = HistoryFormat::crc32(&out, 28);
    return true;
  }
  void close(unsigned slot) override {
    assert(slot < 2 && active[slot]); active[slot] = false;
  }
  Result clock(uint32_t epoch, uint32_t uptime) override {
    return epoch >= 1577836800u && uptime < epoch ? Result::Ok : Result::Invalid;
  }
};
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  Source source; Server server(source);
  std::array<uint8_t, MessageLimit> first{}, second{};
  uint32_t now = UINT32_MAX - 100;
  for (size_t at = 0; at + 3 < size; at += 4) {
    Request r; r.boot = 1;
    for (unsigned i = 0; i < 16; ++i) r.operation.bytes[i] = uint8_t(data[at + i % 4] ^ i);
    r.archive.bytes[0] = 1 + data[at] % 4;
    r.op = Op(1 + data[at + 1] % 8);
    const uint32_t sizes[] = {17, 0, 1, 7, 8, 9, 2880};
    source.count = sizes[(data[at] >> 4) % 7];
    source.textMode = (data[at + 1] >> 3) & 3;
    source.openMode = data[at + 1] >> 5;
    source.failRecord = data[at + 2] & 128;
    const uint32_t ordinals[] = {0, 1, 7, 8, 9, 16, 17, 18, 2879, 2880, 2881, UINT32_MAX};
    r.ordinal = ordinals[(data[at + 2] & 127) % 12];
    r.epoch = data[at] & 128 ? 1800000000 : 0;
    r.uptime = data[at + 2];
    now += uint32_t(data[at + 3]) * 1000;
    size_t n = 0, m = 0;
    const auto result = server.respond(r, now, first.data(), n);
    assert(n && n <= MessageLimit);
    const auto repeated = server.respond(r, now, second.data(), m);
    assert(result == repeated && n == m && !memcmp(first.data(), second.data(), n));
    if (r.op == Op::Clock) assert(result == (r.epoch ? Result::Ok : Result::Invalid));
    if (r.op == Op::Metadata || r.op == Op::Diagnostics) {
      assert(result == (source.textMode == 1 || source.textMode == 2 ? Result::Error : Result::Ok));
      if (result == Result::Ok) assert(n == (source.textMode == 3 ? MessageLimit : 1));
    }
    assert(unsigned(source.active[0]) + unsigned(source.active[1]) <= 2);
    if (result == Result::Ok && r.op == Op::Page) {
      assert(n >= 4 && (n - 4) % 32 == 0 && n <= 260 && get32(first.data()) == r.ordinal);
      for (size_t pos = 4; pos < n; pos += 32) {
        HistoryFormat::Record row;
        assert(decodeRecord(first.data() + pos, row));
        assert(row.sequence == r.ordinal + 1 + (pos - 4) / 32);
      }
    }
  }
  if (size && data[0] & 8) server.expire(now + LifetimeMs);
  server.stop();
  assert(!source.active[0] && !source.active[1]);
  server.stop();
  return 0;
}
