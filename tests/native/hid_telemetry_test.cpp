#include "HidTelemetryServer.h"
#include <array>
#include <cassert>
#include <vector>
using namespace HidTelemetry;
struct Source : Provider {
  std::array<std::vector<HistoryFormat::Record>, 2> frozen;
  unsigned opens = 0, closes = 0;
  size_t textSize = 1;
  HistoryFormat::ReadState openState = HistoryFormat::ReadState::Record;
  bool recordFailure = false;
  size_t text(Op, uint8_t *out, size_t capacity) override {
    memset(out, 'x', textSize < capacity ? textSize : capacity); return textSize;
  }
  HistoryFormat::ReadState open(unsigned slot, HistoryFormat::ExportMetadata &m, uint32_t &digest) override {
    assert(slot < 2);
    if (openState != HistoryFormat::ReadState::Record) return openState;
    ++opens; frozen[slot].clear(); digest = HistoryFormat::kDigestSeed;
    for (unsigned i = 0; i < 17; ++i) {
      auto r = HistoryFormat::example(); r.magic = HistoryFormat::kMagic; r.sequence = i + 1;
      r.crc = HistoryFormat::crc32(&r, 28); frozen[slot].push_back(r);
      digest = HistoryFormat::digestRecord(digest, r);
    }
    m = {123, opens, 17, 1, 17}; return HistoryFormat::ReadState::Record;
  }
  bool record(unsigned slot, uint32_t at, HistoryFormat::Record &out) override {
    assert(slot < 2);
    if (recordFailure || at >= frozen[slot].size()) return false;
    out = frozen[slot][at]; return true;
  }
  void close(unsigned slot) override { assert(slot < 2); ++closes; frozen[slot].clear(); }
};
static void boundaryTests() {
  {
    Request time{Op::Clock, {{1}}, {}, 42, 0, 1800000000, 100}, restored;
    uint8_t bytes[Payload]; encode(time, bytes);
    assert(decode(bytes, Payload, restored) && restored.epoch==time.epoch && restored.uptime==100);
    time.epoch=1577836800;time.uptime=time.epoch;
    encode(time,bytes);assert(decode(bytes,Payload,restored));
    time.epoch=1577836799;encode(time,bytes);assert(!decode(bytes,Payload,restored));
    time.epoch=1800000000;time.uptime=100;
    for (unsigned mode=0;mode<4;++mode) {
      auto invalid=time;
      if(mode==0)invalid.epoch=1;
      if(mode==1)invalid.uptime=UINT32_MAX;
      if(mode==2)invalid.archive.bytes[0]=1;
      if(mode==3)invalid.ordinal=1;
      encode(invalid,bytes);assert(!decode(bytes,Payload,restored));
    }
  }
  uint8_t wire[Payload]{};
  Request r{Op::Metadata, {{1}}, {}, UINT64_C(0xfedcba9876543210), 2880}, copy;
  for (auto op : {Op::Metadata, Op::Diagnostics, Op::Open, Op::Page, Op::Close}) {
    r.op = op; r.archive = op < Op::Open ? Id{} : Id{{2}};
    encode(r, wire); assert(decode(wire, Payload, copy));
    assert(copy.boot == r.boot && copy.op == op && copy.ordinal == 2880);
  }
  for (auto op : {Op::Open, Op::Page, Op::Close}) {
    r.op = op; r.archive = {}; encode(r, wire); assert(!decode(wire, Payload, copy));
  }
  r.archive = Id{{2}}; r.ordinal = 2881; encode(r, wire); assert(!decode(wire, Payload, copy));
  r.ordinal = 0;
  for (unsigned op : {0u, 6u, 255u}) {
    r.op = Op(op); encode(r, wire); assert(!decode(wire, Payload, copy));
  }
  r.op = Op::Metadata; r.boot = 0; encode(r, wire); assert(!decode(wire, Payload, copy));
  r.boot = 1; r.operation = {}; encode(r, wire); assert(!decode(wire, Payload, copy));
  r.operation = Id{{1}};
  Live live, liveCopy; live.boot = UINT64_C(0x123456789abcdef0);
  live.model = 3; live.soc = 100; live.source = 2; live.known = 4095;
  encode(live, wire); assert(decode(wire, Payload, liveCopy) && liveCopy.boot == live.boot);
  for (unsigned field : {19u, 23u, 32u}) {
    encode(live, wire); ++wire[field]; seal(wire); assert(!decode(wire, Payload, liveCopy));
  }
  encode(live, wire); put16(wire + 20, 4096); seal(wire); assert(!decode(wire, Payload, liveCopy));
  uint8_t message[84]{};
  const auto digest = crc32(message, sizeof(message));
  assert(!fragment(r, Result::Ok, message, 0, 0, 0, wire));
  assert(!fragment(r, Result::Ok, message, MessageLimit + 1, 0, 0, wire));
  memset(wire, 0xa5, sizeof(wire));
  assert(!fragment(r, Result::Ok, message, 84, digest, 84, wire));
  for (auto byte : wire) assert(byte == 0xa5); // Invalid range must not publish a header.
  Assembly assembly; assembly.expect(r);
  fragment(r, Result::Error, message, 84, digest, 0, wire); assert(assembly.accept(wire, Payload) == 0);
  fragment(r, Result::Error, message, 84, digest, 28, wire); assert(assembly.accept(wire, Payload) == 0);
  fragment(r, Result::Error, message, 84, digest, 40, wire); assert(assembly.accept(wire, Payload) == -1);
  fragment(r, Result::Error, message, 84, digest, 56, wire); assert(assembly.accept(wire, Payload) == 1);
  assert(assembly.status() == Result::Error);
  put16(wire + 27, 84); assert(assembly.accept(wire, Payload) == -1);
  Source source; Server server(source); uint8_t response[MessageLimit]; size_t size;
  {
    Request clock{Op::Clock, {{1}}, {}, 42, 0, 1800000000, 100};
    assert(server.respond(clock,1,response,size)==Result::Invalid);
    assert(size==1 && response[0]==0 && source.opens==0 && source.closes==0);
  }
  for (auto op : {Op::Metadata, Op::Diagnostics}) {
    r.op = op;
    for (size_t count : {size_t(1), MessageLimit}) {
      source.textSize = count;
      assert(server.respond(r, 1, response, size) == Result::Ok && size == count);
      for (size_t i = 0; i < size; ++i) assert(response[i] == 'x');
    }
    for (size_t count : {size_t(0), MessageLimit + 1}) {
      source.textSize = count; assert(server.respond(r, 1, response, size) == Result::Error);
    }
  }
  r.op = Op::Open; r.archive = Id{{1}};
  source.openState = HistoryFormat::ReadState::Busy;
  assert(server.respond(r, 1, response, size) == Result::Busy);
  source.openState = HistoryFormat::ReadState::Error;
  assert(server.respond(r, 1, response, size) == Result::Error);
  source.openState = HistoryFormat::ReadState::Record;
  assert(server.respond(r, 1, response, size) == Result::Ok);
  r.archive = Id{{2}}; assert(server.respond(r, 2, response, size) == Result::Ok); // slot zero
  assert(server.respond(r, 3, response, size) == Result::Ok && source.opens == 2);
  r.op = Op::Page;
  for (unsigned ordinal : {0u, 8u, 9u, 16u, 17u}) {
    r.ordinal = ordinal;
    assert(server.respond(r, 4, response, size) == Result::Ok);
    const unsigned count = ordinal < 10 ? 8 : 17 - ordinal;
    assert(size == 4 + count * 32 && get32(response) == ordinal);
    for (unsigned i = 0; i < count; ++i) {
      HistoryFormat::Record row; assert(decodeRecord(response + 4 + i * 32, row));
      assert(row.sequence == ordinal + i + 1);
    }
  }
  r.ordinal = 18; assert(server.respond(r, 4, response, size) == Result::Invalid);
  r.ordinal = 0; source.recordFailure = true;
  assert(server.respond(r, 4, response, size) == Result::Error);
  r.op = Op::Close; assert(server.respond(r, 5, response, size) == Result::Ok && source.closes == 1);
  server.stop(); assert(source.closes == 2);
  auto row = HistoryFormat::example(); row.magic = HistoryFormat::kMagic; row.sequence = 1;
  row.flags = 0; row.soc = 255; row.crc = HistoryFormat::crc32(&row, 28);
  uint8_t record[32]; encodeRecord(row, record); assert(decodeRecord(record, row));
  row.flags = HistoryFormat::Fresh; row.crc = HistoryFormat::crc32(&row, 28);
  encodeRecord(row, record); assert(!decodeRecord(record, row));
  Source expiry; Server idle(expiry); r.op = Op::Open;
  assert(idle.respond(r, 1, response, size) == Result::Ok);
  idle.expire(IdleMs); assert(expiry.closes == 0);
  idle.expire(IdleMs + 1); assert(expiry.closes == 1);
  Source hard; Server lifetime(hard);
  assert(lifetime.respond(r, 90000, response, size) == Result::Ok);
  for (uint32_t now = 100000; now < 270000; now += 10000)
    assert(lifetime.respond(r, now, response, size) == Result::Ok && hard.opens == 1);
  lifetime.expire(270000); assert(hard.closes == 1);
  // Adjacent storage catches loop-bound mutations that touch a third slot.
  struct Guard { Server server; std::array<uint8_t, 128> tail; } guard{Server(source), {}};
  guard.tail.fill(255);
  guard.server.expire(IdleMs); guard.server.stop();
  for (auto value : guard.tail) assert(value == 255);
}
static void fragmentContractTests() {
  assert(crc32("123456789", 9) == 0xcbf43926u);
  Request request; request.operation.bytes[0] = 7; request.boot = 42;
  std::array<uint8_t, MessageLimit> message{};
  for (size_t i = 0; i < message.size(); ++i) message[i] = uint8_t(i * 17);
  for (const size_t length : {size_t(1), size_t(27), size_t(28), size_t(29), size_t(260), MessageLimit}) {
    const auto digest = HistoryFormat::crc32(message.data(), length);
    for (auto status : {Result::Ok, Result::Busy, Result::Invalid, Result::Expired, Result::Error}) {
      Assembly assembly; assembly.expect(request);
      for (size_t offset = 0; offset < length;) {
        std::array<uint8_t, Payload> report, retry;
        report.fill(0xa5);
        const auto count = fragment(request, status, message.data(), length, digest, offset, report.data());
        assert(count == (length - offset < FragmentBytes ? length - offset : FragmentBytes));
        assert(get32(report.data() + 31) == digest);
        assert(fragment(request, status, message.data(), length, digest, offset, retry.data()) == count);
        assert(retry == report);
        assert(assembly.accept(report.data(), Payload) == (offset + count == length ? 1 : 0));
        offset += count;
      }
      assert(assembly.size() == length && assembly.status() == status);
      assert(!memcmp(assembly.bytes, message.data(), length));
    }
  }
  // Last fragment of a 29-byte message, captured from the previous implementation.
  const uint8_t expected[Payload] = {
    0x02,0x04,0x07,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x2a,0,0,0,0,0,0,0,
    0x01,0x1c,0,0x1d,0,0xf5,0xe1,0xfb,0x31,0xdc
  };
  uint8_t golden[Payload];
  assert(fragment(request, Result::Error, message.data(), 29, crc32(message.data(), 29), 28, golden) == 1);
  assert(!memcmp(golden, expected, Payload));
  // A supplied bad digest must remain on the wire and be rejected by the receiver.
  uint8_t report[Payload]; Assembly bad; bad.expect(request);
  const auto wrong = crc32(message.data(), 1) ^ 1;
  assert(fragment(request, Result::Ok, message.data(), 1, wrong, 0, report) == 1);
  assert(get32(report + 31) == wrong && bad.accept(report, Payload) == -1);
}
int main() {
  fragmentContractTests();
  boundaryTests();
  Request a; a.boot = 42; a.operation.bytes[0] = 1; a.archive = a.operation; a.op = Op::Open;
  Request b = a; b.operation.bytes[0] = 2; b.archive = b.operation;
  uint8_t wire[Payload]; encode(a, wire); Request decoded;
  assert(decode(wire, sizeof(wire), decoded) && decoded.operation == a.operation);
  for (size_t i = 0; i < Payload; ++i) {
    wire[i] ^= 1; assert(!decode(wire, sizeof(wire), decoded)); wire[i] ^= 1;
  }
  for (size_t n = 0; n < Payload; ++n) assert(!decode(wire, n, decoded));
  Live live; live.boot = 42; live.soc = 87; live.input = 1500; live.mainAge = UINT32_MAX;
  live.sourceRaw = 65535; live.flags = Fresh | Connected; live.known = 4095;
  encode(live, wire); Live copy;
  assert(decode(wire, sizeof(wire), copy) && copy.soc == 87 && copy.input == 1500 &&
         copy.mainAge == UINT32_MAX && copy.sourceRaw == 65535 && copy.known == 4095);
  std::vector<uint8_t> message(8192);
  for (size_t i = 0; i < message.size(); ++i) message[i] = uint8_t(i);
  const auto digest = crc32(message.data(), message.size());
  Assembly one, two; one.expect(a); two.expect(b);
  for (size_t offset = 0; offset < message.size();) {
    size_t count = fragment(a, Result::Ok, message.data(), message.size(), digest, offset, wire);
    assert(count); assert(two.accept(wire, Payload) == 0);
    assert(one.accept(wire, Payload) == (offset + count == message.size() ? 1 : 0));
    if (offset) assert(one.accept(wire, Payload) == 0); // Duplicate does not append.
    offset += count;
  }
  assert(one.size() == message.size() && !memcmp(one.bytes, message.data(), message.size()));
  one.expect(a); fragment(a, Result::Ok, message.data(), message.size(), digest, FragmentBytes, wire);
  assert(one.accept(wire, Payload) == -1);
  one.expect(a); fragment(a, Result::Ok, message.data(), 5, crc32(message.data(), 5), 0, wire); wire[35] ^= 1;
  assert(one.accept(wire, Payload) == -1);
  Source source; Server server(source); uint8_t response[MessageLimit]; size_t size = 0;
  assert(server.respond(a, 1, response, size) == Result::Ok && size == 24);
  const auto generation = get32(response + 4);
  assert(server.respond(b, 2, response, size) == Result::Ok && source.opens == 2);
  assert(server.respond(a, 3, response, size) == Result::Ok && source.opens == 2 && get32(response + 4) == generation);
  Request third = b; third.archive.bytes[0] = 3;
  assert(server.respond(third, 4, response, size) == Result::Busy);
  a.op = Op::Page; a.ordinal = 8;
  assert(server.respond(a, 5, response, size) == Result::Ok && size == 260);
  std::vector<uint8_t> saved(response, response + size);
  assert(server.respond(a, 6, response, size) == Result::Ok && !memcmp(response, saved.data(), size));
  HistoryFormat::Record row;
  assert(decodeRecord(response + 4, row) && row.sequence == 9);
  response[6] ^= 1; assert(!decodeRecord(response + 4, row));
  a.op = Op::Close; assert(server.respond(a, 7, response, size) == Result::Ok && source.closes == 1);
  a.op = Op::Open; assert(server.respond(a, 8, response, size) == Result::Expired);
  assert(server.respond(third, 9, response, size) == Result::Ok);
  server.expire(IdleMs + 10); assert(source.closes == 3);
  assert(server.respond(b, IdleMs + 11, response, size) == Result::Expired);
  Source secondSource; Server second(secondSource);
  assert(second.respond(a, UINT32_MAX - 5, response, size) == Result::Ok);
  second.expire(IdleMs); assert(secondSource.closes == 1);
  Source lifetimeSource; Server lifetime(lifetimeSource);
  assert(lifetime.respond(a, 1, response, size) == Result::Ok);
  for (uint32_t now = 10000; now < LifetimeMs; now += 10000)
    assert(lifetime.respond(a, now, response, size) == Result::Ok);
  lifetime.expire(LifetimeMs + 1); assert(lifetimeSource.closes == 1);
  third.archive.bytes[0] = 4;
  assert(lifetime.respond(third, LifetimeMs + 2, response, size) == Result::Ok);
  lifetime.stop(); assert(lifetimeSource.closes == 2);
  lifetime.stop(); assert(lifetimeSource.closes == 2);
}
