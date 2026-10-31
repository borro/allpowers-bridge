#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Payloads exclude the report ID. Integers are explicitly little endian.
namespace HidTelemetry {
constexpr uint8_t Version = 2, CapsReport = 4, LiveReport = 5, RequestReport = 6,
                  ResponseReport = 7;
constexpr size_t Payload = 63, MessageLimit = 8192, FragmentBytes = 28;
constexpr uint16_t UsagePage = 0xff01, Usage = 1;
constexpr uint32_t IdleMs = 30000, LifetimeMs = 180000;
enum class Op : uint8_t { Metadata = 1, Diagnostics, Open, Page, Close, Packet, Clock };
enum class Result : uint8_t { Ok, Busy, Invalid, Expired, Error };
struct Id {
  uint8_t bytes[16]{};
  bool operator==(const Id &other) const { return memcmp(bytes, other.bytes, 16) == 0; }
  bool empty() const { return *this == Id{}; }
};
inline uint16_t get16(const uint8_t *p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
inline uint32_t get32(const uint8_t *p) {
  return uint32_t(get16(p)) | uint32_t(get16(p + 2)) << 16;
}
inline uint64_t get64(const uint8_t *p) {
  return uint64_t(get32(p)) | uint64_t(get32(p + 4)) << 32;
}
inline void put16(uint8_t *p, uint16_t n) { p[0] = uint8_t(n); p[1] = uint8_t(n >> 8); }
inline void put32(uint8_t *p, uint32_t n) { put16(p, uint16_t(n)); put16(p + 2, uint16_t(n >> 16)); }
inline void put64(uint8_t *p, uint64_t n) { put32(p, uint32_t(n)); put32(p + 4, uint32_t(n >> 32)); }
inline uint32_t crc32(const void *data, size_t size) {
  const auto *p = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & uint32_t(-int32_t(crc & 1)));
  }
  return ~crc;
}
inline void seal(uint8_t *p) { put32(p + 59, crc32(p, 59)); }
inline bool sealed(const uint8_t *p, size_t n) {
  return n == Payload && get32(p + 59) == crc32(p, 59);
}
struct Request {
  Op op = Op::Metadata;
  Id operation, archive;
  uint64_t boot = 0;
  uint32_t ordinal = 0;
  uint32_t epoch = 0, uptime = 0;
};
inline void encode(const Request &r, uint8_t *p) {
  memset(p, 0, Payload); p[0] = Version; p[1] = uint8_t(r.op);
  memcpy(p + 2, r.operation.bytes, 16); put64(p + 18, r.boot);
  memcpy(p + 26, r.archive.bytes, 16); put32(p + 42, r.ordinal);
  put32(p + 46, r.epoch); put32(p + 50, r.uptime); seal(p);
}
inline bool decode(const uint8_t *p, size_t n, Request &r) {
  if (!sealed(p, n) || p[0] != Version || p[1] < uint8_t(Op::Metadata) ||
      p[1] > uint8_t(Op::Clock) || p[1] == uint8_t(Op::Packet)) return false;
  r.op = Op(p[1]); memcpy(r.operation.bytes, p + 2, 16); r.boot = get64(p + 18);
  memcpy(r.archive.bytes, p + 26, 16); r.ordinal = get32(p + 42);
  r.epoch = get32(p + 46); r.uptime = get32(p + 50);
  if (r.op == Op::Clock)
    return !r.operation.empty() && r.boot && r.archive.empty() && !r.ordinal &&
           r.epoch >= 1577836800u && r.uptime <= r.epoch;
  return !r.operation.empty() && r.boot &&
         (r.op < Op::Open || !r.archive.empty()) && r.ordinal <= 2880;
}
inline void capabilities(uint8_t *p, uint64_t boot, bool clock = false) {
  memset(p, 0, Payload); memcpy(p, "APHID1", 6); put64(p + 6, boot);
  p[14] = Version; p[15] = 2; p[16] = 8; put16(p + 17, 2880);
  put16(p + 19, uint16_t(MessageLimit)); put32(p + 21, IdleMs);
  put32(p + 25, LifetimeMs); p[29] = clock ? 1 : 0; seal(p);
}
struct Live {
  uint64_t boot = 0;
  uint32_t uptime = 0, known = 0, runtime = 0, mainAge = UINT32_MAX,
           settingsAge = UINT32_MAX, valid = 0, invalid = 0;
  uint8_t address[6]{}, model = 0, flags = 0, soc = 0, source = 0, ups = 0,
          firmware = 0;
  uint16_t input = 0, output = 0, sourceRaw = 0;
  uint32_t uptimeSeconds = 0;
};
enum : uint8_t { Connected = 1, Fresh = 2, MainSeen = 4, SettingsSeen = 8,
                 AcOn = 16, UsbOn = 32, DcOn = 64, UpsEnabled = 128 };
inline void encode(const Live &v, uint8_t *p) {
  memset(p, 0, Payload); p[0] = Version; put64(p + 1, v.boot); put32(p + 9, v.uptime);
  memcpy(p + 13, v.address, 6); p[19] = v.model; put16(p + 20, uint16_t(v.known));
  p[22] = v.flags; p[23] = v.soc; put16(p + 24, v.input); put16(p + 26, v.output);
  put32(p + 28, v.runtime); p[32] = v.source; put16(p + 33, v.sourceRaw);
  put32(p + 35, v.mainAge); put32(p + 39, v.settingsAge);
  put32(p + 43, v.valid); put32(p + 47, v.invalid); p[51] = v.ups; p[52] = v.firmware;
  put32(p + 53, v.uptimeSeconds); seal(p);
}
inline bool decode(const uint8_t *p, size_t n, Live &v) {
  if (!sealed(p, n) || p[0] != Version || p[19] > 3 || get16(p + 20) > 4095 ||
      p[23] > 100 || p[32] > 2 || !get64(p + 1)) return false;
  v.boot = get64(p + 1); v.uptime = get32(p + 9); memcpy(v.address, p + 13, 6);
  v.model = p[19]; v.known = get16(p + 20); v.flags = p[22]; v.soc = p[23];
  v.input = get16(p + 24); v.output = get16(p + 26); v.runtime = get32(p + 28);
  v.source = p[32]; v.sourceRaw = get16(p + 33); v.mainAge = get32(p + 35);
  v.settingsAge = get32(p + 39); v.valid = get32(p + 43); v.invalid = get32(p + 47);
  v.ups = p[51]; v.firmware = p[52]; v.uptimeSeconds = get32(p + 53); return true;
}
// Compute digest once after preparing the immutable message, then reuse it for
// every fragment and retry. The receiver still verifies the complete message.
inline size_t fragment(const Request &r, Result result, const uint8_t *message,
                       size_t length, uint32_t digest, size_t offset, uint8_t *p) {
  if (!length || length > MessageLimit || offset >= length) return 0;
  memset(p, 0, Payload); p[0] = Version; p[1] = uint8_t(result);
  memcpy(p + 2, r.operation.bytes, 16); put64(p + 18, r.boot); p[26] = uint8_t(r.op);
  put16(p + 27, uint16_t(offset)); put16(p + 29, uint16_t(length));
  put32(p + 31, digest);
  const size_t count = length - offset < FragmentBytes ? length - offset : FragmentBytes;
  memcpy(p + 35, message + offset, count); return count;
}
class Assembly {
  Id operation; uint64_t boot = 0; Op op = Op::Metadata;
  size_t used = 0, total = 0; uint32_t digest = 0; Result result = Result::Ok;
public:
  uint8_t bytes[MessageLimit]{};
  void expect(const Request &r) { operation = r.operation; boot = r.boot; op = r.op; used = total = 0; }
  size_t size() const { return total; }
  Result status() const { return result; }
  // 0: foreign/incomplete, 1: complete, -1: corruption/gap (retry this request).
  int accept(const uint8_t *p, size_t n) {
    if (n != Payload || p[0] != Version || memcmp(p + 2, operation.bytes, 16) ||
        get64(p + 18) != boot || p[26] != uint8_t(op)) return 0;
    const size_t offset = get16(p + 27), length = get16(p + 29);
    if (!length || length > MessageLimit || offset >= length || p[1] > uint8_t(Result::Error)) return -1;
    if (!offset) { used = 0; total = length; digest = get32(p + 31); result = Result(p[1]); }
    if (length != total || get32(p + 31) != digest || Result(p[1]) != result) return -1;
    const size_t count = length - offset < FragmentBytes ? length - offset : FragmentBytes;
    if (offset < used) return offset + count <= used && !memcmp(bytes + offset, p + 35, count) ? 0 : -1;
    if (offset != used) return -1;
    memcpy(bytes + used, p + 35, count); used += count;
    return used == total ? (crc32(bytes, total) == digest ? 1 : -1) : 0;
  }
};
}
