#pragma once
#include "HidTelemetryWire.h"
#include "HistoryWire.h"

namespace HidTelemetry {
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
}
