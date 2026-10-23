#pragma once
// Caller owns synchronization and BOOT/restart guards.
// No tasks, allocation, discovery, connections, or implicit persistence.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <initializer_list> // HistoryRing's portable RAM self-test uses it.
#include "StationIdentity.h"
#include "HistoryRing.h"

namespace StationPicker {
constexpr uint8_t kCapacity = 24;
constexpr uint8_t kPageSize = 6;
constexpr uint32_t kTtlMs = 45000;

struct Target {
  StationIdentity identity{};
  uint8_t addressType = 0;
  uint8_t model = 0;
  bool connectable = true;
};
struct Entry {
  StationIdentity identity{};
  int8_t rssi = 0;
  uint8_t addressType = 0;
  uint32_t seenAt = 0;
  uint8_t model = 0;
  bool connectable = true;
};

inline bool lowerHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}
inline bool validMAC(const char *mac) {
  if (!mac) return false;
  for (unsigned i = 0; i < 17; ++i) {
    if (i % 3 == 2) { if (mac[i] != ':') return false; }
    else if (!lowerHex(mac[i])) return false;
  }
  return mac[17] == '\0';
}
// For BLE libraries returning uppercase. Persistence itself accepts lowercase only.
// out must have 18 bytes; a failed conversion leaves it unchanged.
inline bool canonicalizeMAC(const char *input, char *out) {
  if (!input || !out) return false;
  char temporary[18] = {};
  for (unsigned i = 0; i < 17; ++i) {
    char c = input[i];
    if (c >= 'A' && c <= 'F') c = char(c - 'A' + 'a');
    if (i % 3 == 2) { if (c != ':') return false; }
    else if (!lowerHex(c)) return false;
    temporary[i] = c;
  }
  if (input[17] != '\0') return false;
  memcpy(out, temporary, sizeof(temporary));
  return true;
}
inline bool validIdentity(const StationIdentity &identity) {
  return identity.name[0] != '\0' &&
         memchr(identity.name, '\0', sizeof(identity.name)) && validMAC(identity.address);
}
inline bool valid(const Target &target) {
  // Unknown model=0 is a legitimate connectable BLE target. Caller supplies
  // "UNNAMED" before insertion if the advertisement has no local name.
  return validIdentity(target.identity);
}
inline Target targetOf(const Entry &entry) {
  Target target;
  target.identity = entry.identity;
  target.addressType = entry.addressType;
  target.model = entry.model;
  target.connectable = entry.connectable;
  return target;
}
inline bool sameMAC(const char *left, const char *right) {
  return validMAC(left) && validMAC(right) && memcmp(left, right, 18) == 0;
}

struct Snapshot {
  Entry entries[kPageSize]{};
  uint8_t count = 0;
  uint8_t total = 0;
  uint8_t offset = 0;
  int8_t selectedIndex = -1; // Selection may have expired; do not silently retarget.
  int8_t selectedRow = -1; // -1 also when the selection is outside this page.
  char selectedMAC[18] = {};
};

class Catalog {
  Entry entries_[kCapacity]{};
  uint8_t count_ = 0;
  char selected_[18] = {};
public:
  uint8_t size() const { return count_; }
  int8_t find(const char *mac) const {
    if (!validMAC(mac)) return -1;
    for (uint8_t i = 0; i < count_; ++i)
      if (memcmp(entries_[i].identity.address, mac, 18) == 0) return int8_t(i);
    return -1;
  }
  // unsigned subtraction handles millis() wrap. Call regularly, not after a
  // full 2^32-ms gap. Relative order of surviving entries never changes.
  uint8_t prune(uint32_t now, const char *connectedMAC = nullptr) {
    uint8_t write = 0;
    for (uint8_t read = 0; read < count_; ++read) {
      const Entry &entry = entries_[read];
      if (uint32_t(now - entry.seenAt) < kTtlMs ||
          sameMAC(entry.identity.address, connectedMAC)) {
        if (write != read) entries_[write] = entry;
        ++write;
      }
    }
    const uint8_t removed = uint8_t(count_ - write);
    for (uint8_t i = write; i < count_; ++i) entries_[i] = Entry{};
    count_ = write;
    return removed;
  }
  // seenAt is always reception time `now`, never untrusted advertisement input.
  // A full fresh catalog rejects an extra MAC; it does not reshuffle/evict entries.
  bool upsert(const Entry &entry, uint32_t now, const char *connectedMAC = nullptr) {
    if (!valid(targetOf(entry))) return false;
    int8_t position = find(entry.identity.address);
    // Refresh before pruning so a just-seen existing station keeps its order.
    if (position >= 0) {
      entries_[position] = entry;
      entries_[position].seenAt = now;
      prune(now, connectedMAC);
      return true;
    }
    prune(now, connectedMAC);
    if (count_ == kCapacity) return false;
    entries_[count_] = entry;
    entries_[count_++].seenAt = now;
    return true;
  }
  bool selectMAC(const char *mac) {
    if (find(mac) < 0) return false;
    memcpy(selected_, mac, sizeof(selected_));
    return true;
  }
  void clearSelection() { memset(selected_, 0, sizeof(selected_)); }
  // Call only for an explicit user navigation gesture. BACK is caller-owned:
  // use snapshot/selectMAC instead when cycling a menu containing BACK.
  bool next() {
    if (!count_) return false;
    int8_t current = find(selected_);
    uint8_t nextIndex = current < 0 ? 0 : uint8_t((current + 1) % count_);
    return selectMAC(entries_[nextIndex].identity.address);
  }
  bool selectedTarget(Target &out) const {
    const int8_t index = find(selected_);
    if (index < 0) return false;
    out = targetOf(entries_[index]);
    return true;
  }
  Snapshot snapshot(uint8_t offset = 0) const {
    Snapshot out;
    out.total = count_;
    out.offset = offset > count_ ? count_ : offset;
    const uint8_t remaining = uint8_t(count_ - out.offset);
    out.count = remaining > kPageSize ? kPageSize : remaining;
    out.selectedIndex = find(selected_);
    if (out.selectedIndex >= out.offset && out.selectedIndex < out.offset + out.count)
      out.selectedRow = int8_t(out.selectedIndex - out.offset);
    memcpy(out.selectedMAC, selected_, sizeof(selected_));
    for (uint8_t i = 0; i < out.count; ++i) out.entries[i] = entries_[out.offset + i];
    return out;
  }
};

// One NVS blob, no compiler padding. Numeric fields follow ESP32 little endian.
// Name tail is zeroed by encode, so save-if-changed ignores unused source bytes.
constexpr uint32_t kMagic = 0x31545041; // APT1
constexpr uint16_t kVersion = 1;
struct __attribute__((packed)) Blob {
  uint32_t magic;
  uint16_t version, bytes;
  char name[32], address[18];
  uint8_t addressType, model;
  uint32_t crc;
};
static_assert(sizeof(Blob) == 64 && offsetof(Blob, crc) == 60, "Target blob format");

inline bool encode(const Target &target, Blob &out) {
  if (!valid(target)) return false;
  Blob blob{};
  blob.magic = kMagic;
  blob.version = kVersion;
  blob.bytes = sizeof(Blob);
  size_t length = 0;
  while (target.identity.name[length]) ++length; // bounded by valid().
  memcpy(blob.name, target.identity.name, length);
  memcpy(blob.address, target.identity.address, sizeof(blob.address));
  blob.addressType = target.addressType;
  blob.model = target.model;
  blob.crc = HistoryFormat::crc32(&blob, offsetof(Blob, crc));
  out = blob;
  return true;
}
inline bool decode(const void *data, size_t length, Target &out) {
  if (!data || length != sizeof(Blob)) return false;
  Blob blob;
  memcpy(&blob, data, sizeof(blob));
  if (blob.magic != kMagic || blob.version != kVersion || blob.bytes != sizeof(Blob) ||
      blob.crc != HistoryFormat::crc32(&blob, offsetof(Blob, crc))) return false;
  Target target;
  memcpy(target.identity.name, blob.name, sizeof(blob.name));
  memcpy(target.identity.address, blob.address, sizeof(blob.address));
  target.addressType = blob.addressType;
  target.model = blob.model;
  if (!valid(target)) return false;
  out = target; // Failed loads never modify caller's current target.
  return true;
}

// Caller provides a single-key NVS adapter. It must already be explicitly opened
// (Preferences.begin), and closed (Preferences.end) by caller. No ownership or
// caching of NVS state here. write() returns only after committing the whole key.
struct BlobStorage {
  virtual size_t length() = 0;
  virtual bool read(void *data, size_t length) = 0;
  virtual bool write(const void *data, size_t length) = 0;
  virtual ~BlobStorage() = default;
};
class TargetStorage {
  BlobStorage &storage_;
public:
  explicit TargetStorage(BlobStorage &storage): storage_(storage) {}
  bool load(Target &out) {
    Blob blob;
    return storage_.length() == sizeof(blob) && storage_.read(&blob, sizeof(blob)) &&
           decode(&blob, sizeof(blob), out);
  }
  // Invoke ONLY after a successful connection, never on highlight/selection.
  // Caller holds its NVS mutex and requestBoot guard throughout this call.
  bool save(const Target &target) {
    Blob desired, existing;
    if (!encode(target, desired)) return false;
    if (storage_.length() == sizeof(existing)) {
      // A read error must not be treated as a reason to overwrite unknown state.
      if (!storage_.read(&existing, sizeof(existing))) return false;
      Target decoded;
      Blob normalized;
      if (decode(&existing, sizeof(existing), decoded) && encode(decoded, normalized) &&
          memcmp(&normalized, &desired, sizeof(desired)) == 0) return true;
    }
    if (!storage_.write(&desired, sizeof(desired))) return false;
    Target verified;
    return storage_.length() == sizeof(existing) && storage_.read(&existing, sizeof(existing)) &&
           memcmp(&existing, &desired, sizeof(desired)) == 0 &&
           decode(&existing, sizeof(existing), verified);
  }
};

// Standalone RAM-only test; does not construct Preferences or touch hardware.
inline bool selfTest() {
  struct MemoryStorage: BlobStorage {
    uint8_t bytes[sizeof(Blob)]{};
    size_t size = 0;
    unsigned writes = 0;
    bool corruptWrite = false;
    size_t length() override { return size; }
    bool read(void *out, size_t n) override {
      if (n != size) return false;
      memcpy(out, bytes, n); return true;
    }
    bool write(const void *in, size_t n) override {
      if (n != sizeof(bytes)) return false;
      memcpy(bytes, in, n); size = n; ++writes;
      if (corruptWrite) bytes[12] ^= 1;
      return true;
    }
  } memory;
  Entry first;
  memcpy(first.identity.name, "AP R2500", 9);
  memcpy(first.identity.address, "01:23:45:67:89:ab", 18);
  first.model = 255; first.rssi = -50;
  Target target = targetOf(first), loaded;
  Blob blob;
  if (!encode(target, blob) || !decode(&blob, sizeof(blob), loaded) ||
      !sameMAC(target.identity.address, loaded.identity.address)) return false;
  // Every single-byte corruption is rejected by CRC/format validation.
  for (size_t i = 0; i < sizeof(blob); ++i) {
    Blob damaged = blob;
    reinterpret_cast<uint8_t*>(&damaged)[i] ^= 1;
    if (decode(&damaged, sizeof(damaged), loaded)) return false;
  }
  if (decode(&blob, sizeof(blob) - 1, loaded)) return false;
  Target invalid = target;
  invalid.identity.address[15] = 'A';
  if (valid(invalid)) return false;
  invalid = target; memset(invalid.identity.name, 'x', sizeof(invalid.identity.name));
  if (valid(invalid)) return false;
  invalid = target; invalid.model = 0;
  if (!valid(invalid) || !encode(invalid, blob) ||
      !decode(&blob, sizeof(blob), loaded) || loaded.model != 0) return false;
  char normalized[18] = {};
  if (!canonicalizeMAC("01:23:45:67:89:AB", normalized) ||
      !sameMAC(normalized, target.identity.address)) return false;
  TargetStorage store(memory);
  if (store.load(loaded) || !store.save(target) || !store.save(target) ||
      memory.writes != 1 || !store.load(loaded)) return false;
  target.model = 2; memory.corruptWrite = true;
  if (store.save(target) || store.load(loaded)) return false;

  Catalog catalog;
  for (uint8_t i = 0; i < kCapacity; ++i) {
    Entry entry = first;
    entry.identity.address[15] = "0123456789abcdef"[i >> 4];
    entry.identity.address[16] = "0123456789abcdef"[i & 15];
    if (!catalog.upsert(entry, 100)) return false;
  }
  if (catalog.upsert(first, 101) || !catalog.next()) return false;
  Snapshot before = catalog.snapshot();
  if (before.count != kPageSize || before.total != kCapacity ||
      catalog.snapshot(18).count != 6 || catalog.snapshot(24).count != 0) return false;
  Entry refresh = before.entries[0]; refresh.rssi = -20;
  if (!catalog.upsert(refresh, 102) || catalog.snapshot().selectedIndex != 0) return false;
  // Removal compacts survivors, but never replaces the chosen MAC.
  const char *connected = before.entries[1].identity.address;
  if (catalog.prune(45102, connected) != kCapacity - 1 || catalog.size() != 1 ||
      catalog.snapshot().selectedIndex != -1 || catalog.selectedTarget(loaded) ||
      !sameMAC(catalog.snapshot().selectedMAC, before.selectedMAC)) return false;
  if (!catalog.next() || !catalog.selectedTarget(loaded) ||
      !sameMAC(loaded.identity.address, connected)) return false;
  Catalog wrap;
  if (!wrap.upsert(first, 0xfffffff0u) || wrap.prune(0x10u) != 0 ||
      wrap.prune(uint32_t(0xfffffff0u + kTtlMs)) != 1) return false;
  return true;
}
} // namespace StationPicker
