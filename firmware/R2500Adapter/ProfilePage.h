#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ProfilePage {
constexpr uint8_t None = 0xff;
// A diagnostic-build-only maintenance payload. This is distinct from BOOT,
// bounded by the board's pages, and never represents a button/menu gesture.
inline uint8_t decode(uint8_t id, const uint8_t *bytes, size_t n, uint8_t pages) {
  return id == 1 && bytes && n >= 13 && !memcmp(bytes, "PAGE-R2500!!", 12) && bytes[12] < pages
             ? bytes[12]
             : None;
}
} // namespace ProfilePage
