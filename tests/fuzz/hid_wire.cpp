#include "HidTelemetryServer.h"
#include "HistoryClock.h"
#include <array>
#include <cassert>
#include <vector>
using namespace HidTelemetry;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size >= 16) {
    HistoryFormat::ClockAnchor clock; uint32_t base = 0;
    const auto epoch = get32(data), observed = get32(data + 4);
    const auto now = get64(data + 8);
    if (clock.candidate(epoch, observed, now, base)) {
      clock.commit(base);
      assert(clock.at(now) == uint64_t(epoch) + now - observed);
      clock.commit(base + 1);
      assert(clock.at(now) == uint64_t(epoch) + now - observed);
    }
  }
  Request decoded;
  Live live;
  // Raw, truncated and overlong reports must be safe before any CRC is trusted.
  decode(data, size, decoded);
  decode(data, size, live);
  std::array<uint8_t, Payload> report{}, encoded{};
  memcpy(report.data(), data, size < Payload ? size : Payload);
  seal(report.data()); // Also reach semantic validation beyond the checksum.
  if (decode(report.data(), Payload, decoded)) {
    encode(decoded, encoded.data());
    Request again;
    assert(decode(encoded.data(), Payload, again));
    assert(again.operation == decoded.operation && again.archive == decoded.archive &&
           again.boot == decoded.boot && again.op == decoded.op && again.ordinal == decoded.ordinal);
  }
  if (decode(report.data(), Payload, live)) {
    encode(live, encoded.data());
    Live again;
    assert(decode(encoded.data(), Payload, again));
    encode(again, report.data());
    assert(report == encoded);
  }
  if (size >= 32) {
    HistoryFormat::Record record;
    decodeRecord(data, record);
  }
  Request request; request.boot = 1; request.operation.bytes[0] = 1;
  Assembly raw; raw.expect(request);
  for (size_t at = 0; at < size;) {
    const size_t n = size - at < Payload ? size - at : Payload;
    raw.accept(data + at, n);
    at += n;
  }
  if (!size) return 0;
  // A valid stream plus foreign, duplicate, missing and corrupted fragments.
  const size_t length = 1 + (size * (1 + data[0])) % MessageLimit;
  std::vector<uint8_t> message(length);
  for (size_t i = 0; i < length; ++i) message[i] = data[i % size];
  Assembly complete, damaged;
  complete.expect(request); damaged.expect(request);
  for (size_t at = 0, index = 0; at < length; ++index) {
    const auto n = fragment(request, Result::Ok, message.data(), length, at, report.data());
    assert(n && n <= FragmentBytes);
    const int result = complete.accept(report.data(), Payload);
    assert(result == (at + n == length ? 1 : 0));
    encoded = report;
    const uint8_t action = data[index % size];
    if (action & 1) encoded[action % Payload] ^= uint8_t(1u << (action % 8));
    if (!(action & 2)) {
      const auto accepted = damaged.accept(encoded.data(), Payload);
      if (accepted == 1) {
        assert(damaged.size() <= MessageLimit);
        assert(crc32(damaged.bytes, damaged.size()) == get32(encoded.data() + 31));
      }
      if (action & 4) damaged.accept(encoded.data(), Payload);
    }
    at += n;
  }
  assert(complete.size() == length && !memcmp(complete.bytes, message.data(), length));
  return 0;
}
