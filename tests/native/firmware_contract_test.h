#pragma once
#include "history_operation_test.h"
#include <array>
#include <string>

namespace FirmwareContractTests {
inline void run() {
  using namespace HistoryFormat;
  {
    // A lying erase must not cause a second pass over the partition or writes
    // into occupied slots. Once exhausted, the ring stays unhealthy.
    struct OccupiedFlash : Flash {
      uint32_t reads = 0, erases = 0, writes = 0;
      bool read(uint32_t offset, void *data, size_t size) override {
        assert(offset < kBytes && size == sizeof(Record));
        ++reads; memset(data, 0, size); return true;
      }
      bool erase(uint32_t offset) override {
        assert(offset < kBytes && offset % kSector == 0);
        ++erases; return true;
      }
      bool write(uint32_t, const void *, size_t) override { ++writes; return false; }
    } flash;
    Ring ring(flash);
    assert(ring.recover());
    flash.reads = 0;
    auto record = example();
    assert(!ring.append(record) && !ring.ok() && ring.latestSequence() == 0);
    assert(flash.reads == kSlots && flash.erases == kBytes / kSector && flash.writes == 0);
    assert(!ring.append(record));
    assert(flash.reads == kSlots && flash.erases == kBytes / kSector && flash.writes == 0);
  }
  {
    // The bounded command probe must also accept an exact-sized, unterminated
    // buffer without looking for a sentinel outside the caller's storage.
    char command[kHistoryCommandCapacity];
    memset(command, '0', sizeof(command));
    assert(historyCommandLength(command) == sizeof(command));
    // Also expose the length regression deterministically without an invalid
    // read in the mutant: bytes beyond the declared limit must be ignored.
    char longer[kHistoryCommandCapacity + 1];
    memset(longer, '0', sizeof(longer));
    assert(historyCommandLength(longer) == kHistoryCommandCapacity);
  }
  {
    struct LongStatus : StatusProvider {
      std::string phase;
      bool capture(StatusSnapshot &snapshot, uint32_t) override {
        snapshot = {}; snapshot.state.phase = phase.c_str(); return true;
      }
    } status;
    StatusSnapshot snapshot;
    snapshot.state.phase = "";
    char scratch[4096];
    const int fixed = serializeStatus(snapshot, 2, scratch, sizeof(scratch));
    assert(fixed > 0 && fixed < 3071);
    for (size_t size : {3071u, 3072u, 3073u}) {
      status.phase.assign(size - size_t(fixed), 'x');
      OutputTests::Source source;
      OutputTests::Sink sink;
      OutputController output(source, sink, status);
      output.pump(0); output.pump(1);
      assert(!output.failed());
      const auto writes = sink.writes;
      output.pump(2);
      if (size == 3071) {
        assert(!output.failed() && sink.writes == writes + 1 && !sink.invalid && !sink.used);
      } else {
        assert(output.failed() && sink.writes == writes && !sink.used);
      }
    }
  }
  {
    BootGestures button;
    button.begin(false, 0);
    assert(button.update(true, 10) == BootGestures::Event::None);
    assert(button.update(true, 40) == BootGestures::Event::None);
    // No polling while held: recognize exactly HoldMs at raw release.
    assert(button.update(false, 840) == BootGestures::Event::None);
    assert(button.update(false, 870) == BootGestures::Event::Hold);
    assert(button.update(false, 900) == BootGestures::Event::None);
  }
  {
    char mac[18]{};
    assert(StationPicker::canonicalizeMAC("02:00:00:00:00:FF", mac));
    assert(!strcmp(mac, "02:00:00:00:00:ff"));
    StationPicker::Catalog catalog;
    std::array<StationPicker::Entry, 8> entries{};
    for (unsigned i = 0; i < entries.size(); ++i) {
      strcpy(entries[i].identity.name, "Synthetic station");
      snprintf(entries[i].identity.address, 18, "02:00:00:00:00:%02x", i);
      assert(catalog.upsert(entries[i], 0));
    }
    for (int i = 0; i < 17; ++i) {
      assert(catalog.next());
      assert(catalog.snapshot().selectedIndex == i % 8);
    }
    assert(catalog.selectMAC(entries[6].identity.address));
    assert(catalog.snapshot(0).selectedRow == -1); // Exactly beyond this page.
    assert(catalog.snapshot(1).selectedRow == 5);
    assert(catalog.snapshot(6).selectedRow == 0);
    assert(catalog.snapshot(7).selectedRow == -1);
    assert(catalog.snapshot(8).count == 0 && catalog.snapshot(8).selectedRow == -1);
    assert(catalog.selectMAC(entries[7].identity.address));
    assert(catalog.snapshot(6).selectedRow == 1);
  }
  {
    auto a = example(), b = a;
    a.sequence = 1; b.sequence = 2;
    b.uptimeSeconds = 90;
    assert(GraphHistory::join(a, b));
    b.uptimeSeconds = 91;
    assert(!GraphHistory::join(a, b));
    b.uptimeSeconds = 0;
    assert(GraphHistory::join(a, b));
    a.flags &= ~Fresh;
    assert(!GraphHistory::join(a, b));
    std::array<GraphHistory::Point, 4> points{};
    for (auto &p : points) {
      p.sample = example(); p.sample.uptimeSeconds = 10;
      p.joinPrevious = p.unknownBefore = true;
    }
    assert(!GraphHistory::assignAges(points.data(), 2, 10, 1));
    assert(points[0].age == 0 && points[1].age == 0);
    assert(!points[0].unknownBefore && !points[1].unknownBefore);
    assert(points[2].unknownBefore && points[2].joinPrevious); // Count is a bound.
    points[1].sample.session = 2;
    assert(GraphHistory::assignAges(points.data(), 2, 15, 1));
    assert(points[1].unknownBefore);
    for (auto &p : points) { p.sample.flags |= Clock; p.sample.epochSeconds = 100; }
    assert(!GraphHistory::assignAges(points.data(), 2, 15, 2));
    assert(points[0].age == 5 && points[1].age == 5);
    points[1].sample.epochSeconds = 200;
    for (unsigned missing : {0u, 1u}) {
      points[missing].sample.flags &= ~Clock;
      assert(GraphHistory::assignAges(points.data(), 2, 15, 2));
      assert(points[0].age == 15 && points[1].unknownBefore);
      points[missing].sample.flags |= Clock;
    }
    points[0].sample = points[1].sample = example();
    points[1].sample.uptimeSeconds = 10;
    assert(!GraphHistory::assignAges(points.data(), 2, GraphHistory::kHorizon, 1));
    assert(points[0].age == GraphHistory::kHorizon);
    points[0].sample.session = 2;
    assert(GraphHistory::assignAges(points.data(), 1, 15, 1));
    assert(points[0].unknownBefore && points[0].age == 15);
  }
  {
    auto r = example();
    r.magic = header(0xfff, 3); r.flags = 0x7f;
    r.chargeWatts = 12; r.outputWatts = 34; r.runtimeMinutes = 56;
    auto projected = legacyProjection(r);
    assert(projected.soc == 100 && projected.chargeWatts == 12 && projected.outputWatts == 34);
    assert(projected.runtimeMinutes == 56 && projected.flags == r.flags);
    for (uint32_t absent : {1u, 2u, 4u, 8u, 512u, 16u, 32u, 64u}) {
      r.magic = header(0xfff & ~absent, 3);
      projected = legacyProjection(r);
      if (absent == 1) assert(projected.soc == 255);
      if (absent == 2) assert(projected.chargeWatts == 0xffff);
      if (absent == 4) assert(projected.outputWatts == 0xffff);
      if (absent == 8) assert(projected.runtimeMinutes == 0xffff);
      if (absent == 512) assert(!(projected.flags & (Mains | Battery)));
      if (absent == 1 || absent == 16 || absent == 32 || absent == 64)
        assert(!(projected.flags & Fresh));
    }
    // FNV-1a of little-endian sequence 01020304 and CRC a1b2c3d4.
    r.sequence = 0x01020304; r.crc = 0xa1b2c3d4;
    assert(digestRecord(kDigestSeed, r) == 0x8e681945);
    assert(sequenceDistance(UINT32_MAX - 1, 2) == 3);
    assert(!newer(7, 7) && newer(1, UINT32_MAX));
    r = example(); r.magic = kMagic; r.sequence = 1; r.soc = 255; r.flags &= ~Fresh;
    r.crc = crc32(&r, offsetof(Record, crc));
    assert(valid(r, 0)); // A stale record may have unknown SOC.
    r.flags |= Fresh; r.crc = crc32(&r, offsetof(Record, crc));
    assert(!valid(r, 0));
    std::array<Record, 4> input{};
    input[0].sequence = 8; input[1].sequence = 9;
    input[2].sequence = 7; input[3].sequence = 100;
    Record result{};
    assert(atOrAfter(input.data(), 2, 2, 3, 8, result) && result.sequence == 8);
    input[2].sequence = 100; // A newer record exists physically, but outside count.
    assert(!atOrAfter(input.data(), 0, 2, 4, 100, result));
    input[2].sequence = 7;
    std::array<Record, 5> storage{};
    FrozenArchive archive(storage.data(), 3);
    ExportMetadata metadata;
    assert(archive.freeze(input.data(), 2, 3, 3, 10, 11, metadata) == ReadState::Record);
    assert(metadata.oldest == 7 && metadata.latest == 9 && metadata.count == 3);
    assert(archive.read(1, result) == ReadState::Record && result.sequence == 8);
    input[0].sequence = 99;
    assert(archive.read(1, result) == ReadState::Record && result.sequence == 8);
  }
  {
    StatusSnapshot snapshot;
    char json[3072];
    assert(serializeStatus(snapshot, 0, json, sizeof json) > 0);
    assert(strstr(json, "\"ac_output\":false") && strstr(json, "\"usb_output\":false"));
    assert(strstr(json, "\"dc_output\":false"));
  }
  {
    OutputTests::Source source;
    HistoryOperationTests::Sink sink; sink.zeroWrites = 0; sink.chunk = 4096;
    ExportSession session(source, sink);
    session.start(1, 0, 2, 1); // Inclusive first, exclusive end: only sequence 2.
    for (uint32_t now = 0; session.active() && now < 30; ++now) session.pump(now);
    assert(!session.active() && session.acknowledged() == 2);
    assert(sink.bytes.find("\"sequence\":2,") != std::string::npos);
    assert(sink.bytes.find("\"sequence\":1,") == std::string::npos);
    assert(sink.bytes.find("\"sequence\":3,") == std::string::npos);
    sink.bytes.clear(); sink.chunk = 1;
    session.start(2, 100); session.pump(100); session.pump(101); session.cancel();
    assert(session.framingPending() && !session.acknowledged());
    sink.chunk = 0;
    assert(!session.syncBoundary());
    sink.chunk = 1;
    assert(session.syncBoundary() && !session.framingPending());
    assert(sink.bytes.back() == '\n' && !session.acknowledged());
    const std::string longNonce(33, 'a');
    session.start(3, 200, 0, 0, longNonce.c_str());
    assert(!session.active()); // A direct caller must not silently truncate the nonce.
  }
  {
    OutputTests::Source source; source.busy = 100;
    HistoryOperationTests::Sink sink; sink.zeroWrites = 0; sink.chunk = 4096;
    ExportSession session(source, sink);
    session.start(1, 0); session.pump(60000); session.pump(60000);
    assert(session.active() && sink.bytes.empty());
    session.pump(60001); session.pump(60002);
    assert(!session.active() && sink.bytes.find("\"status\":\"error\"") != std::string::npos);
    sink.zeroWrites = 100;
    session.start(2, 100000); session.pump(160001); session.pump(162000);
    assert(session.active());
    session.pump(162001);
    assert(!session.active());
  }
  {
    struct ExcessSource : OutputTests::Source {
      ReadState open(ExportMetadata &metadata) override {
        auto result = OutputTests::Source::open(metadata);
        metadata.count = metadata.latest = 1;
        return result;
      }
    } source;
    HistoryOperationTests::Sink sink; sink.zeroWrites = 0; sink.chunk = 4096;
    ExportSession session(source, sink); session.start(1, 0);
    for (uint32_t now = 0; session.active() && now < 30; ++now) session.pump(now);
    assert(!session.active() && session.acknowledged() == 1);
    assert(sink.bytes.find("\"status\":\"error\"") != std::string::npos);
  }
  {
    OutputTests::Source source;
    OutputTests::Status status(source);
    HistoryOperationTests::Sink sink; sink.chunk = 7; sink.zeroWrites = 0;
    OutputController output(source, sink, status);
    const std::string optional = "{\"one\":1}\n{\"two\":2}\n";
    assert(output.queueOptional(optional.data(), optional.size()));
    assert(output.busy());
    for (int i = 0; i < 1000 && output.busy(); ++i) output.pump(0);
    assert(!output.busy() && !output.failed());
    assert(sink.bytes.ends_with(optional));
    const std::string full = "{\"x\":\"" + std::string(6135, 'x') + "\"}\n";
    assert(full.size() == 6144 && output.queueOptional(full.data(), full.size()));
    for (int i = 0; i < 2000 && output.busy(); ++i) output.pump(0);
    assert(!output.busy() && !output.failed() && sink.bytes.ends_with(full));
    for (const char *command : {"HISTORY-EXPORT 9", "HISTORY-EXPORT 09", "HISTORY-EXPORT 4294967295"})
      assert(output.command(command, 1));
    for (const char *command : {"HISTORY-EXPORT /", "HISTORY-EXPORT :", "HISTORY-EXPORT 4294967296",
                                "HISTORY-EXPORT 4294967297"})
      assert(!output.command(command, 1));
    const std::string overlong = "HISTORY-EXPORT " + std::string(64, '0') + "1";
    assert(overlong.size() == kHistoryCommandCapacity && !output.command(overlong.c_str(), 1));
  }
  {
    OutputTests::Source source;
    OutputTests::Status status(source);
    HistoryOperationTests::Sink sink; sink.chunk = 1; sink.zeroWrites = 0;
    OutputController output(source, sink, status);
    output.pump(0); output.pump(4000); output.pump(8000);
    assert(!output.failed() && !output.paused());
    output.pump(8001);
    assert(output.failed() && output.paused());
  }
  {
    OutputTests::Source source;
    OutputTests::Status status(source);
    HistoryOperationTests::Sink sink; sink.chunk = 4096; sink.zeroWrites = 0;
    OutputController output(source, sink, status);
    output.pump(0);
    assert(status.captures == 1);
    assert(output.command("STATUS", 1));
    for (uint32_t now = 1; now < kStatusPrimaryRows; ++now) {
      output.pump(now);
      assert(status.captures == 1); // An urgent request cannot split the primary batch.
    }
    output.pump(6);
    assert(status.captures == 2);
    for (uint32_t now = 7; now < 12; ++now) output.pump(now);
    assert(status.captures == 2);
    output.pump(1010);
    assert(status.captures == 2); // Not one millisecond before the interval.
    output.pump(1011); // Exactly 1000 ms since the primary batch finished.
    assert(status.captures == 3);
    for (uint32_t now = 1012; now < 1017; ++now) output.pump(now);
    output.pump(2016); // Also refresh exactly at a primary boundary, before any tail row.
    assert(status.captures == 4);
  }
  {
    OutputTests::Source source;
    OutputTests::Status status(source);
    HistoryOperationTests::Sink sink; sink.chunk = 4096; sink.zeroWrites = 0;
    OutputController output(source, sink, status);
    assert(output.command("HISTORY-EXPORT 1", 0));
    for (uint32_t now = 0; now < kStatusPrimaryRows; ++now) output.pump(now);
    assert(source.opens == 0);
    output.pump(kStatusPrimaryRows);
    assert(source.opens == 1); // Archive gets its turn before optional status rows.
  }
  {
    OutputTests::Source source; source.busy = 100;
    struct BusyStatus : StatusProvider {
      bool capture(StatusSnapshot &, uint32_t) override { return false; }
    } status;
    HistoryOperationTests::Sink sink; sink.chunk = 0; sink.zeroWrites = 0;
    OutputController output(source, sink, status);
    assert(output.command("HISTORY-EXPORT 1", 100000));
    output.pump(100000);
    assert(output.active() && !output.failed());
    output.pump(162000);
    assert(output.active() && !output.failed());
    output.pump(162001);
    assert(!output.active() && output.failed());
  }
  {
    OutputTests::Source source;
    OutputTests::Status status(source);
    HistoryOperationTests::Sink sink; sink.chunk = 1; sink.zeroWrites = 0;
    OutputController output(source, sink, status);
    output.pump(0);
    const auto prefix = sink.bytes;
    assert(prefix.size() == 1);
    sink.chunk = 0; output.pump(4001);
    assert(output.paused());
    assert(output.command("HISTORY-EXPORT 1", 5000));
    output.pump(5000); output.pump(9000);
    assert(!output.paused() && !output.failed() && sink.bytes == prefix);
    assert(source.opens == 0); // A zero-byte boundary write cannot grant ownership.
    sink.chunk = 4096; output.pump(9000);
    assert(sink.bytes == prefix + "\n");
    for (uint32_t now = 9001; output.active() && now < 9100; ++now) output.pump(now);
    assert(!output.active() && !output.failed() && output.acknowledged() == 3);
  }
}
}
