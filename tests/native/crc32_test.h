#pragma once
#include "HidTelemetryWire.h"
#include "HistoryRing.h"
#include <array>
#include <cassert>

namespace Crc32Tests {
// Independent, original bitwise algorithm: neither the table nor product helper
// participates in generating the expected value.
inline uint32_t reference(const uint8_t *data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & uint32_t(-int32_t(crc & 1)));
  }
  return ~crc;
}

inline void compare(const uint8_t *data, size_t size) {
  const uint32_t expected = reference(data, size);
  assert(AllpowersCrc32::computePortable(data, size) == expected);
  assert(HistoryFormat::crc32(data, size) == expected);
  assert(HidTelemetry::crc32(data, size) == expected);
}

inline void run() {
  assert(HistoryFormat::crc32(nullptr, 0) == 0);
  assert(HidTelemetry::crc32(nullptr, 0) == 0);
  assert(HidTelemetry::crc32("123456789", 9) == 0xcbf43926u);
  std::array<uint8_t, 8192 + 8> data{};
  for (unsigned value = 0; value < 256; ++value) {
    data[value] = uint8_t(value);
    // Every possible one-byte input exercises all 256 lookup indices.
    compare(data.data() + value, 1);
  }
  assert(HistoryFormat::crc32(data.data(), 256) == 0x29058c73u);
  assert(HidTelemetry::crc32(data.data(), 1) == 0xd202ef8du);
  assert(HidTelemetry::crc32(data.data() + 255, 1) == 0xff000000u);

  uint32_t random = 0x6d2b79f5u;
  for (auto &byte : data) {
    random = random * 1664525u + 1013904223u;
    byte = uint8_t(random >> 24);
  }
  // All short lengths plus real record/report/message boundaries, including
  // unaligned addresses and buffers containing embedded zero/high-bit bytes.
  for (size_t offset = 0; offset < 8; ++offset) {
    for (size_t size = 0; size <= 512; ++size)
      compare(data.data() + offset, size);
    for (size_t size :
         {size_t(1023), size_t(1024), size_t(4095), size_t(4096), size_t(8191), size_t(8192)})
      compare(data.data() + offset, size);
  }
}
} // namespace Crc32Tests
