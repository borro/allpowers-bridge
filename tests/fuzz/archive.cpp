#include "BootEpoch.h"
#include "HidHistoryWire.h"
#include "HistoryExport.h"
#include <array>
#include <cassert>
#include <cstring>
#include <optional>

using namespace HistoryFormat;
namespace {
struct Storage : EpochStorage {
  uint32_t value = 0;
  unsigned reads = 0, writes = 0;
  uint8_t faults = 0;
  bool read(uint32_t &out) override {
    ++reads;
    out = value + ((faults & 4) && reads == 2 ? 1u : 0u);
    return !((faults & 1) && reads == 1) && !((faults & 8) && reads == 2);
  }
  bool write(uint32_t in) override { ++writes; if (faults & 2) return false; value = in; return true; }
};
struct FlashWithFaults : MemoryFlash {
  bool failRead = false;
  bool failReadAfterWrite = false, corruptAfterWrite = false;
  explicit FlashWithFaults(uint8_t *memory) : MemoryFlash(memory) {}
  bool read(uint32_t off, void *out, size_t size) override {
    if (failRead) { failRead = false; return false; }
    return MemoryFlash::read(off, out, size);
  }
  bool write(uint32_t off, const void *in, size_t size) override {
    const bool result = MemoryFlash::write(off, in, size);
    if (result) {
      failRead = failReadAfterWrite;
      if (corruptAfterWrite && size) bytes[off] ^= 1;
    }
    return result;
  }
};
// Independent scan of persisted bytes, using the HID CRC implementation.
uint32_t persistedLatest(const std::array<uint8_t, kBytes> &memory) {
  uint32_t latest = 0;
  for (uint32_t i = 0; i < kSlots; ++i) {
    Record r;
    memcpy(&r, memory.data() + i * sizeof(r), sizeof(r));
    if ((r.magic & 0xffff0000u) != 0xa3000000u || !r.sequence ||
        (r.sequence - 1) % kSlots != i || ((r.flags & Fresh) && r.soc > 100) ||
        r.crc != HidTelemetry::crc32(memory.data() + i * sizeof(r), 28)) continue;
    if (!latest || (r.sequence != latest && uint32_t(r.sequence - latest) < 0x80000000u))
      latest = r.sequence;
  }
  return latest;
}
void recover(std::optional<Ring> &ring, FlashWithFaults &flash,
             const std::array<uint8_t, kBytes> &memory) {
  ring.emplace(flash);
  assert(ring->recover() && ring->ok());
  assert(ring->latestSequence() == persistedLatest(memory));
}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8) return 0;
  Storage epochs;
  epochs.value = HidTelemetry::get32(data);
  epochs.faults = data[4] & 15;
  uint32_t epoch = 0x12345678;
  const auto prior = epochs.value;
  const bool reserved = reserveEpoch(epochs, epoch);
  assert(reserved == (!epochs.faults && prior != UINT32_MAX));
  assert(epoch == (reserved ? prior + 1 : 0x12345678u));
  if ((epochs.faults & 1) || prior == UINT32_MAX) assert(epochs.writes == 0);

  std::array<uint8_t, kBytes> memory;
  FlashWithFaults flash(memory.data());
  // Start near sector, ring and sequence boundaries without thousands of appends.
  const uint32_t starts[] = {0, 1, 127, 128, kSlots - 1, kSlots, UINT32_MAX - 1, UINT32_MAX};
  const uint32_t start = starts[data[5] % 8];
  if (start) {
    Record r{}; r.magic = kMagic; r.sequence = start;
    r.crc = HidTelemetry::crc32(&r, 28);
    memcpy(memory.data() + ((start - 1) % kSlots) * sizeof(r), &r, sizeof(r));
  }
  std::optional<Ring> ring;
  recover(ring, flash, memory);
  for (size_t at = 8, steps = 0; at + 8 <= size && steps < 32; at += 8, ++steps) {
    const auto *p = data + at;
    switch (p[0] % 8) {
    case 0: case 1: case 2: {
      Record r{}; r.magic = header(HidTelemetry::get16(p + 2), p[1]);
      r.session = uint32_t(p[4]) + 1; r.soc = p[5] % 101;
      r.flags = p[6]; r.outputWatts = HidTelemetry::get16(p + 3);
      if (p[0] % 8 == 1) flash.partial = p[7] % 33;
      if (p[0] % 8 == 2) {
        flash.erasePrefix = p[7] % 2 ? 1 : 2048;
        flash.failErase = (p[1] & 1) != 0;
        flash.failRead = (p[1] & 2) != 0;
      }
      flash.failReadAfterWrite = p[1] & 4;
      flash.corruptAfterWrite = p[1] & 8;
      if (ring->append(r)) {
        assert(r.sequence && r.crc == HidTelemetry::crc32(&r, 28));
        Record stored{};
        assert(ring->readSequence(r.sequence, stored));
        assert(memcmp(&r, &stored, sizeof(r)) == 0);
      }
      flash.partial = flash.erasePrefix = -1; flash.failErase = flash.failRead = false;
      flash.failReadAfterWrite = flash.corruptAfterWrite = false;
      break;
    }
    case 3: recover(ring, flash, memory); break;
    case 4: {
      const uint32_t index = (p[4] & 1) && ring->latestSequence()
        ? ((ring->latestSequence() - 1) % kSlots) * 32 + p[5] % 32
        : HidTelemetry::get32(p + 1) % kBytes;
      memory[index] ^= p[3];
      recover(ring, flash, memory);
      break;
    }
    case 5: {
      Record r{};
      const uint32_t sequence = p[1] & 1 ? ring->latestSequence() : HidTelemetry::get32(p + 2);
      if (ring->readSequence(sequence, r)) {
        assert(sequence && r.sequence == sequence && r.crc == HidTelemetry::crc32(&r, 28));
      }
      break;
    }
    case 6:
      flash.failRead = true;
      assert(!ring->recover() && !ring->ok());
      recover(ring, flash, memory);
      break;
    case 7: {
      Record invalid{};
      assert(!ring->append(invalid));
      break;
    }
    }
  }
  recover(ring, flash, memory);

  // Exercise frozen snapshots against an independent copy and mutate the source.
  std::array<Record, 16> input{}, output{}, expected{};
  for (unsigned i = 0; i < input.size(); ++i) {
    input[i].magic = header(0xfff, 3); input[i].sequence = i + 1;
    input[i].session = epoch; input[i].soc = data[i % size] % 101;
    input[i].crc = HidTelemetry::crc32(&input[i], 28);
  }
  const uint32_t capacity = data[0] % 17, count = data[1] % 18;
  const uint32_t inputCapacity = data[2] % 17;
  const uint32_t head = inputCapacity ? data[3] % inputCapacity : 0;
  const bool nullInput = data[6] & 1, nullOutput = data[6] & 2;
  FrozenArchive frozen(nullOutput ? nullptr : output.data(), capacity);
  ExportMetadata meta;
  const bool validSize = count <= capacity && count <= inputCapacity &&
                         (!count || (!nullInput && !nullOutput));
  const auto result = frozen.freeze(nullInput ? nullptr : input.data(), head, count,
                                    inputCapacity, epoch, 7, meta);
  assert((result == ReadState::Record) == validSize);
  if (validSize) {
    for (uint32_t i = 0; i < count; ++i) expected[i] = input[(head + i) % inputCapacity];
    assert(meta.count == count && meta.session == epoch && meta.generation == 7);
    assert(meta.oldest == (count ? expected[0].sequence : 0));
    assert(meta.latest == (count ? expected[count - 1].sequence : 0));
    input.fill({});
    for (uint32_t i = 0; i < count; ++i) {
      Record r{}; assert(frozen.read(i, r) == ReadState::Record);
      assert(memcmp(&r, &expected[i], sizeof(r)) == 0);
      std::array<char, 514> text{}; text.fill('!');
      const size_t length = HidTelemetry::get16(data + 4) % 513;
      const int required = serialize(r, text.data(), length);
      assert(required > 0 && text[length] == '!');
      if (length) assert(text[size_t(required) < length ? size_t(required) : length - 1] == 0);
      Record found{};
      assert(atOrAfter(expected.data(), 0, count, 16, expected[0].sequence, found));
      assert(found.sequence == expected[0].sequence);
      uint32_t digest = kDigestSeed;
      uint8_t serialized[32]; HidTelemetry::encodeRecord(r, serialized);
      for (unsigned byte : {4u,5u,6u,7u,28u,29u,30u,31u})
        digest = (digest ^ serialized[byte]) * 16777619u;
      assert(digestRecord(kDigestSeed, r) == digest);
    }
    Record end{}; assert(frozen.read(count, end) == ReadState::End);
    assert(frozen.read(count + 1, end) == ReadState::Error);
  }
  char nonce[33]{};
  for (unsigned i = 0; i < 32; ++i) nonce[i] = "0123456789abcdef"[data[i % size] % 16];
  assert(validOperationNonce(nonce, 32));
  nonce[data[7] % 32] = 'G'; assert(!validOperationNonce(nonce, 32));
  assert(!validOperationNonce(nullptr, 32) && !validOperationNonce(nonce, data[7] % 32));
  const uint32_t first = HidTelemetry::get32(data) | 1, sequence = HidTelemetry::get32(data + 4) | 1;
  const uint64_t distance = sequence >= first ? uint64_t(sequence) - first : uint64_t(UINT32_MAX) - first + sequence;
  assert(sequenceDistance(first, sequence) == distance);
  assert(previous(first) == (first == 1 ? UINT32_MAX : first - 1));
  return 0;
}
