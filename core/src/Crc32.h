#pragma once
#include <array>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include <esp_rom_crc.h>
#endif

namespace AllpowersCrc32 {
namespace detail {
// CRC-32/ISO-HDLC, reflected polynomial. Generate the 1 KiB read-only table
// at compile time: no allocation, mutable state or first-call initialization.
consteval std::array<uint32_t, 256> makeTable() {
  std::array<uint32_t, 256> result{};
  for (uint32_t i = 0; i < result.size(); ++i) {
    uint32_t value = i;
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ (0xedb88320u & uint32_t(-int32_t(value & 1)));
    result[i] = value;
  }
  return result;
}
inline constexpr auto table = makeTable();
} // namespace detail

inline uint32_t computePortable(const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    uint8_t byte;
    memcpy(&byte, bytes + i, sizeof(byte));
    crc = detail::table[(crc ^ byte) & 0xffu] ^ (crc >> 8);
  }
  return ~crc;
}
inline uint32_t compute(const void *data, size_t size) {
#if defined(ESP_PLATFORM)
  static_assert(sizeof(size_t) == sizeof(uint32_t), "ROM CRC length must not truncate");
  // ROM complements the seed and result internally: zero selects ISO-HDLC's
  // initial/final 0xffffffff values and preserves the existing wire/archive CRC.
  return esp_rom_crc32_le(0, static_cast<const uint8_t *>(data), uint32_t(size));
#else
  return computePortable(data, size);
#endif
}
} // namespace AllpowersCrc32
