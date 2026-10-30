#include "HidTelemetryServer.h"
#include <array>
#include <cassert>
using namespace HidTelemetry;
struct Source : Provider {
  std::array<bool, 2> active{};
  std::array<uint32_t, 2> generation{};
  uint32_t serial = 0;
  size_t text(Op, uint8_t *out, size_t capacity) override {
    assert(capacity); out[0] = 'x'; return 1;
  }
  HistoryFormat::ReadState open(unsigned slot, HistoryFormat::ExportMetadata &m,
                                 uint32_t &digest) override {
    assert(slot < 2 && !active[slot]);
    active[slot] = true; generation[slot] = ++serial;
    m = {1, serial, 17, 1, 17}; digest = 0;
    return HistoryFormat::ReadState::Record;
  }
  bool record(unsigned slot, uint32_t index, HistoryFormat::Record &out) override {
    assert(slot < 2 && active[slot] && index < 17);
    out = {}; out.magic = HistoryFormat::kMagic; out.sequence = index + 1;
    out.session = generation[slot]; out.crc = HistoryFormat::crc32(&out, 28);
    return true;
  }
  void close(unsigned slot) override {
    assert(slot < 2 && active[slot]); active[slot] = false;
  }
};
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  Source source; Server server(source);
  std::array<uint8_t, MessageLimit> first{}, second{};
  uint32_t now = UINT32_MAX - 100;
  for (size_t at = 0; at + 3 < size; at += 4) {
    Request r; r.boot = 1; r.operation.bytes[0] = 1;
    r.archive.bytes[0] = 1 + data[at] % 4;
    r.op = Op(1 + data[at + 1] % 5);
    r.ordinal = data[at + 2] % 20;
    now += uint32_t(data[at + 3]) * 1000;
    size_t n = 0, m = 0;
    const auto result = server.respond(r, now, first.data(), n);
    assert(n && n <= MessageLimit);
    const auto repeated = server.respond(r, now, second.data(), m);
    assert(result == repeated && n == m && !memcmp(first.data(), second.data(), n));
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
  server.expire(now + LifetimeMs);
  assert(!source.active[0] && !source.active[1]);
  server.stop();
  return 0;
}
