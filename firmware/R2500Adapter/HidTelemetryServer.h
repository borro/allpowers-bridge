#pragma once
#include <HidTelemetryWire.h>
#include "HistoryExport.h"

namespace HidTelemetry {
struct Provider {
  virtual size_t text(Op op, uint8_t *out, size_t capacity) = 0;
  virtual HistoryFormat::ReadState open(unsigned slot, HistoryFormat::ExportMetadata &meta,
                                         uint32_t &digest) = 0;
  virtual bool record(unsigned slot, uint32_t ordinal, HistoryFormat::Record &out) = 0;
  virtual void close(unsigned slot) = 0;
  virtual Result clock(uint32_t, uint32_t) { return Result::Invalid; }
  virtual ~Provider() = default;
};
inline void encodeRecord(const HistoryFormat::Record &r, uint8_t *p) {
  put32(p, r.magic); put32(p + 4, r.sequence); put32(p + 8, r.session);
  put32(p + 12, r.uptimeSeconds); put32(p + 16, r.epochSeconds);
  put16(p + 20, r.chargeWatts); put16(p + 22, r.outputWatts);
  put16(p + 24, r.runtimeMinutes); p[26] = r.soc; p[27] = r.flags; put32(p + 28, r.crc);
}
inline bool decodeRecord(const uint8_t *p, HistoryFormat::Record &r) {
  r.magic = get32(p); r.sequence = get32(p + 4); r.session = get32(p + 8);
  r.uptimeSeconds = get32(p + 12); r.epochSeconds = get32(p + 16);
  r.chargeWatts = get16(p + 20); r.outputWatts = get16(p + 22);
  r.runtimeMinutes = get16(p + 24); r.soc = p[26]; r.flags = p[27]; r.crc = get32(p + 28);
  return r.sequence && HistoryFormat::version3(r.magic) &&
         r.crc == crc32(p, 28) && (!(r.flags & HistoryFormat::Fresh) || r.soc <= 100);
}
// Invoked only by the firmware loop. USB callbacks enqueue into a separate
// bounded FreeRTOS queue; no storage, allocation or waiting in those callbacks.
class Server {
  Provider &provider;
  struct Slot {
    Id id; bool active = false; uint32_t started = 0, touched = 0, digest = 0;
    HistoryFormat::ExportMetadata meta{};
  } archives[2];
public:
  explicit Server(Provider &p) : provider(p) {}
  void expire(uint32_t now) {
    for (unsigned i = 0; i < 2; ++i)
      if (archives[i].active && (uint32_t(now - archives[i].touched) >= IdleMs ||
                              uint32_t(now - archives[i].started) >= LifetimeMs)) {
        provider.close(i); archives[i].active = false;
      }
  }
  void stop() {
    for (unsigned i = 0; i < 2; ++i) {
      if (archives[i].active) provider.close(i);
      archives[i].active = false;
    }
  }
  Result respond(const Request &r, uint32_t now, uint8_t *out, size_t &size) {
    expire(now); size = 1; out[0] = 0;
    if (r.op == Op::Clock) return provider.clock(r.epoch, r.uptime);
    if (r.op == Op::Metadata || r.op == Op::Diagnostics) {
      const size_t count = provider.text(r.op, out, MessageLimit);
      if (!count || count > MessageLimit) return Result::Error;
      size = count; return Result::Ok;
    }
    int found = -1, free = -1;
    for (int i = 0; i < 2; ++i) {
      if (archives[i].id == r.archive) found = i;
      if (!archives[i].active && archives[i].id != r.archive) free = i;
    }
    if (r.op == Op::Close) {
      if (found >= 0 && archives[found].active) {
        provider.close(unsigned(found)); archives[found].active = false;
      }
      return Result::Ok;
    }
    if (found >= 0 && !archives[found].active) return Result::Expired;
    if (found < 0) {
      if (r.op != Op::Open) return Result::Expired;
      if (free < 0) return Result::Busy;
      auto &s = archives[free];
      const auto status = provider.open(unsigned(free), s.meta, s.digest);
      if (status == HistoryFormat::ReadState::Busy) return Result::Busy;
      if (status != HistoryFormat::ReadState::Record) return Result::Error;
      s.id = r.archive; s.active = true; s.started = s.touched = now; found = free;
    }
    auto &s = archives[found]; s.touched = now;
    if (r.op == Op::Open) {
      size = 24;
      put32(out, s.meta.session); put32(out + 4, s.meta.generation); put32(out + 8, s.meta.count);
      put32(out + 12, s.meta.oldest); put32(out + 16, s.meta.latest); put32(out + 20, s.digest);
      return Result::Ok;
    }
    if (r.op != Op::Page || r.ordinal > s.meta.count) return Result::Invalid;
    const uint32_t count = s.meta.count - r.ordinal < 8 ? s.meta.count - r.ordinal : 8;
    put32(out, r.ordinal); size = 4;
    for (uint32_t i = 0; i < count; ++i) {
      HistoryFormat::Record record{};
      if (!provider.record(unsigned(found), r.ordinal + i, record)) { size = 1; return Result::Error; }
      encodeRecord(record, out + size); size += 32;
    }
    return Result::Ok;
  }
};
constexpr uint8_t Descriptor[] = {
  0x06,0x01,0xff,0x09,0x01,0xa1,0x01,0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,63,
  0x85,4,0x09,2,0xb1,2,
  0x85,5,0x09,3,0xb1,0x82,
  0x85,6,0x09,4,0x91,2,
  0x85,7,0x09,5,0x81,2,0xc0
};
}
