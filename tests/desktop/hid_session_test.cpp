#include "usb_hid.h"
#include "usb_transport.h"
#include "transport_router.h"
#include "../../core/src/HidTelemetryServer.h"
#include "../../core/src/StatusWire.h"
#include <QJsonDocument>
#include <QTest>
#include <QUuid>
#include <deque>
#include <vector>
using namespace HidTelemetry;
namespace {
struct Source : Provider {
  std::vector<HistoryFormat::Record> records;
  int opens = 0, closes = 0;
  Source() {
    for (uint32_t i = 1; i <= 17; ++i) {
      auto r = HistoryFormat::example(); r.magic = HistoryFormat::header(15, 3);
      r.sequence = i; r.crc = HistoryFormat::crc32(&r, 28); records.push_back(r);
    }
  }
  size_t text(Op op, uint8_t *out, size_t) override {
    const QByteArray data = op == Op::Metadata
      ? "{\"station_name\":\"test\",\"station_address\":\"02:00:00:00:00:01\"}\n"
      : "{\"diagnostic\":true,\"rows\":[]}\n";
    memcpy(out, data.constData(), size_t(data.size())); return size_t(data.size());
  }
  HistoryFormat::ReadState open(unsigned, HistoryFormat::ExportMetadata &meta, uint32_t &digest) override {
    ++opens; meta = {1, 2, uint32_t(records.size()), 1, uint32_t(records.size())};
    digest = HistoryFormat::kDigestSeed;
    for (const auto &r : records) digest = HistoryFormat::digestRecord(digest, r);
    return HistoryFormat::ReadState::Record;
  }
  bool record(unsigned, uint32_t at, HistoryFormat::Record &out) override {
    if (at >= records.size()) return false; out = records[at]; return true;
  }
  void close(unsigned) override { ++closes; }
};
QByteArray caps() {
  uint8_t buffer[Payload]; capabilities(buffer, 42);
  return QByteArray(reinterpret_cast<char *>(buffer), sizeof(buffer));
}
struct BridgeChannel : UsbChannel {
  std::function<void(const QByteArray &)> send;
  void open(const QString &) override { emit opened(); }
  void close() override {}
  void write(const QByteArray &command) override { if (send) send(command); }
  bool hid() const override { return true; }
  void feed(const QByteArray &data) { emit received(data); }
};
}
class HidSessionTests : public QObject {
  Q_OBJECT
private slots:
  void productionMetadataPreservesLive_data() {
    QTest::addColumn<int>("model");
    QTest::newRow("Old10") << 1;
    QTest::newRow("Legacy16") << 2;
    QTest::newRow("R2500") << 3;
  }
  void productionMetadataPreservesLive() {
    QFETCH(int, model);
    qint64 now = 0;
    const QString serial = "R2500-000000000001";
    auto *channel = new BridgeChannel;
    UsbTransport transport(nullptr, false, channel,
      [&] { return QList<UsbPort>{{serial, "synthetic", "fixture"}}; }, [&] { return now; });
    QList<QByteArray> requests; QList<ap_snapshot> snapshots; QString error;
    unsigned entropy = 0;
    HidSession session({[&](QByteArray b) { requests.append(b); },
      [&](QByteArray b) { channel->feed(b); }, [&](QString e) { error = e; },
      [&] { QByteArray id(16, 0); id[0] = char(++entropy); return id; }});
    channel->send = [&](const QByteArray &command) { session.command(command, now); };
    connect(&transport, &TelemetryTransport::snapshot, this,
      [&](const ap_snapshot &snapshot) { snapshots.append(snapshot); });
    transport.start(serial, model);
    QVERIFY(session.start(caps(), now));
    HistoryFormat::StatusSnapshot frame;
    frame.nowMs = 100; frame.state.connected = true;
    strcpy(frame.state.identity.address, "02:00:00:00:00:fF");
    strcpy(frame.state.identity.name, "synthetic station");
    auto &telemetry = frame.state.telemetry;
    telemetry.protocol = uint8_t(model); telemetry.known = 0x4ff; telemetry.soc = 88;
    telemetry.mainSeen = true; telemetry.mainAt = 100; telemetry.validFrames = 4;
    telemetry.minutes = 1089;
    frame.productVersion = "test-v1";
    const auto metadata = [&] {
      QVERIFY(!requests.isEmpty());
      const auto raw = requests.takeFirst(); Request request;
      QVERIFY(decode(reinterpret_cast<const uint8_t *>(raw.constData()) + 1, 63, request));
      QVERIFY(request.op == Op::Metadata);
      QByteArray body;
      for (uint8_t row = 0; row < HistoryFormat::kStatusRows; ++row) {
        char text[2048]; const int n = HistoryFormat::serializeStatus(frame, row, text, sizeof(text));
        QVERIFY(n > 0 && n < int(sizeof(text))); body.append(text, n);
      }
      const auto digest = crc32(body.constData(), size_t(body.size()));
      for (size_t offset = 0; offset < size_t(body.size());) {
        uint8_t report[64]{}; report[0] = ResponseReport;
        offset += fragment(request, Result::Ok, reinterpret_cast<const uint8_t *>(body.constData()),
                           size_t(body.size()), digest, offset, report + 1);
        session.input(QByteArray(reinterpret_cast<char *>(report), 64), now);
      }
    };
    metadata(); QVERIFY(transport.connected());
    Live live; live.boot = 42; live.model = uint8_t(model); live.known = 0x4ff;
    live.flags = Connected | Fresh | MainSeen; live.firmware = 'R';
    const uint8_t address[] = {2, 0, 0, 0, 0, 0xff};
    memcpy(live.address, address, sizeof(address)); live.runtime = 65340;
    live.uptimeSeconds = 12345; live.soc = 88; live.valid = 4; live.mainAge = 31; live.settingsAge = 8700;
    const auto deliverLive = [&] {
      uint8_t report[Payload]; encode(live, report);
      session.live(QByteArray(reinterpret_cast<char *>(report), sizeof(report)));
    };
    now = 9990; deliverLive(); QVERIFY(!snapshots.isEmpty());
    QCOMPARE(transport.diagnostics()["board_uptime_s"].toInteger(),qint64(12345));
    QCOMPARE(transport.diagnostics()["name"].toString(), QString("synthetic station"));
    QCOMPARE(snapshots.last().runtime_s, quint32(65340));
    QCOMPARE(snapshots.last().known, quint32(0x4ff));
    const int published = snapshots.size();
    now = 10000; frame.productVersion = "test-v2";
    // This metadata is deliberately older than the latest Feature snapshot.
    frame.state.telemetry.known = 1; frame.state.telemetry.validFrames = 3;
    session.tick(now); metadata();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(snapshots.size(), published); // Metadata must not publish legacy telemetry.
    QCOMPARE(transport.diagnostics()["product_version"].toString(), QString("test-v2"));
    QVERIFY(transport.connected());
    now = 10020; transport.refresh();
    const auto latest = snapshots.last();
    QCOMPARE(latest.runtime_s, quint32(65340)); QCOMPARE(latest.known, quint32(0x4ff));
    QCOMPARE(latest.main_age_ms, quint32(61)); QCOMPARE(latest.settings_age_ms, quint32(8730));
    QVERIFY(latest.main_seen); QVERIFY(!latest.settings_seen); QVERIFY(latest.fresh);
    deliverLive();
    QCOMPARE(transport.diagnostics()["name"].toString(), QString("synthetic station"));
    // Live remains authoritative for an actual station change after stale metadata.
    live.address[5] = 2; live.runtime = 120; live.valid = 5; deliverLive();
    QCOMPARE(transport.address(), QString("02:00:00:00:00:02"));
    QVERIFY(transport.diagnostics()["name"].toString().isEmpty());
    QCOMPARE(snapshots.last().runtime_s, quint32(120));
    channel->send = {};
  }
  void rejectedCapabilitiesAndEntropy() {
    for (int mode = 0; mode < 4; ++mode) {
      QList<QByteArray> output; QString error;
      HidSession session({[&](QByteArray b) { output.append(b); }, [](QByteArray) {},
        [&](QString e) { error = e; }, [=] { return mode == 3 ? QByteArray{} : QByteArray(16, 'a'); }});
      auto report = caps();
      if (mode == 0) report.chop(1);
      if (mode == 1) report[59] = char(report[59] ^ 1);
      if (mode == 2) {
        report[14] = char(Version + 1); seal(reinterpret_cast<uint8_t *>(report.data()));
      }
      QVERIFY(!session.start(report, 0)); QVERIFY(!error.isEmpty()); QVERIFY(output.isEmpty());
      session.command("DIAG\n", 1); session.tick(40000); session.live({});
      QVERIFY(output.isEmpty());
    }
  }
  void boundedRetriesAndMalformedMetadata() {
    for (int mode = 0; mode < 4; ++mode) {
      QList<QByteArray> output; QString error; QByteArray received;
      HidSession session({[&](QByteArray b) { output.append(b); }, [&](QByteArray b) { received += b; },
        [&](QString e) { error = e; }, [] { return QByteArray(16, 'a'); }});
      QVERIFY(session.start(caps(), 0));
      Request request; QVERIFY(decode(reinterpret_cast<const uint8_t *>(output[0].constData()) + 1, 63, request));
      if (mode == 0) {
        for (int retry = 1; retry <= 3; ++retry) {
          session.tick(retry * 10000); QCOMPARE(output.size(), retry + 1); QCOMPARE(output.last(), output.first());
        }
        session.tick(40000); QVERIFY(!error.isEmpty()); QCOMPARE(output.size(), 4);
      } else {
        const QByteArray message = mode == 1 ? "[1,2]" : "x";
        uint8_t report[64]{}; report[0] = ResponseReport;
        const auto result = mode == 1 ? Result::Ok : mode == 2 ? Result::Error : Result::Busy;
        const auto digest = crc32(message.constData(), size_t(message.size()));
        fragment(request, result, reinterpret_cast<const uint8_t *>(message.constData()),
                 size_t(message.size()), digest, 0, report + 1);
        session.input(QByteArray(reinterpret_cast<char *>(report), 64), 1);
        if (mode == 3) {
          QVERIFY(error.isEmpty()); session.tick(10001); QCOMPARE(output.size(), 2);
          QCOMPARE(output.first(), output.last());
        } else QVERIFY(!error.isEmpty());
      }
      QVERIFY(received.isEmpty());
    }
  }
  void independentArchivesAndLive() {
    Source source; Server server(source); std::deque<QByteArray> commands;
    QByteArray first, second; QString error; unsigned random = 0;
    const auto entropy = [&] { QByteArray id(16, 0); id[0] = char(++random); return id; };
    HidSession one({[&](QByteArray b) { commands.push_back(b); },
                     [&](QByteArray b) { first += b; }, [&](QString e) { error = e; }, entropy});
    HidSession two({[&](QByteArray b) { commands.push_back(b); },
                     [&](QByteArray b) { second += b; }, [&](QString e) { error = e; }, entropy});
    QVERIFY(one.start(caps(), 0)); QVERIFY(two.start(caps(), 0));
    qint64 now = 0;
    const auto drain = [&] {
      int limit = 100;
      while (!commands.empty() && --limit > 0) {
        const auto raw = commands.front(); commands.pop_front();
        Request request;
        QVERIFY(raw.size() == 64 && uint8_t(raw[0]) == RequestReport);
        QVERIFY(decode(reinterpret_cast<const uint8_t *>(raw.constData()) + 1, 63, request));
        uint8_t body[MessageLimit]; size_t size = 0;
        const auto result = server.respond(request, uint32_t(++now), body, size);
        const auto digest = crc32(body, size);
        for (size_t offset = 0; offset < size;) {
          uint8_t report[64]{}; report[0] = ResponseReport;
          offset += fragment(request, result, body, size, digest, offset, report + 1);
          const QByteArray input(reinterpret_cast<char *>(report), 64);
          one.input(input, now); two.input(input, now);
        }
      }
      QVERIFY(limit > 0);
    };
    drain(); first.clear(); second.clear();
    one.command("HISTORY-EXPORT 1 01000000000000000000000000000000\n", now);
    two.command("HISTORY-EXPORT 2 02000000000000000000000000000000\n", now);
    one.command("DIAG\n", now); two.command("DIAG\n", now);
    drain(); QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(source.opens, 2); QCOMPARE(source.closes, 2);
    for (const auto &result : {first, second}) {
      QCOMPARE(result.count("\"history_record\":true"), 17);
      QCOMPARE(result.count("\"history_export_begin\":true"), 1);
      QCOMPARE(result.count("\"status\":\"complete\""), 1);
      QCOMPARE(result.count("\"diagnostic\":true"), 1);
    }
    QVERIFY(!first.contains("02000000000000000000000000000000"));
    QVERIFY(!second.contains("01000000000000000000000000000000"));
    Live live; live.boot = 42; live.address[0] = 2; live.address[5] = 1;
    live.soc = 88; live.model = 3; live.known = 1023; live.flags = Fresh | Connected | MainSeen;
    live.mainAge = 31; live.settingsAge = 87;
    uint8_t encoded[Payload]; encode(live, encoded);
    first.clear(); second.clear();
    QByteArray report(reinterpret_cast<char *>(encoded), sizeof(encoded));
    one.live(report); two.live(report); QCOMPARE(first, second);
    QVERIFY(first.contains("\"soc\":88")); QVERIFY(first.contains("\"main_age_ms\":31"));
    live.boot = 43; encode(live, encoded); one.live(QByteArray(reinterpret_cast<char *>(encoded), sizeof(encoded)));
    QVERIFY(!error.isEmpty());
  }
  void retryAndForeignReports() {
    QList<QByteArray> commands; QByteArray data; QString error;
    HidSession session({[&](QByteArray b) { commands.append(b); }, [&](QByteArray b) { data += b; },
                         [&](QString e) { error = e; }, [] { return QByteArray(16, 'a'); }});
    QVERIFY(session.start(caps(), 0)); QCOMPARE(commands.size(), 1);
    Request request;
    QVERIFY(decode(reinterpret_cast<const uint8_t *>(commands[0].constData()) + 1, 63, request));
    const QByteArray body = "{\"station_name\":\"test\",\"station_address\":\"02:00:00:00:00:01\"}\n";
    const auto digest = crc32(body.constData(), size_t(body.size()));
    uint8_t report[64]{}; report[0] = ResponseReport;
    fragment(request, Result::Ok, reinterpret_cast<const uint8_t *>(body.constData()), size_t(body.size()), digest, 28, report + 1);
    session.input(QByteArray(reinterpret_cast<char *>(report), 64), 1); QVERIFY(data.isEmpty());
    session.tick(10000); QCOMPARE(commands.size(), 2); QCOMPARE(commands[0], commands[1]);
    for (size_t offset = 0; offset < size_t(body.size());) {
      offset += fragment(request, Result::Ok, reinterpret_cast<const uint8_t *>(body.constData()), size_t(body.size()), digest, offset, report + 1);
      session.input(QByteArray(reinterpret_cast<char *>(report), 64), 10001);
    }
    QCOMPARE(data, body); QVERIFY(error.isEmpty());
    auto bad = caps(); bad[0] = 'x'; QVERIFY(!session.start(bad, 20000)); QVERIFY(!error.isEmpty());
  }
  void routersDoNotLeaseHid() {
    const auto key = "ALLPOWERS.HidTest." + QUuid::createUuid().toString();
    const auto factory = [](const QString &, QObject *parent) -> TelemetryTransport * {
      return new UsbTransport(parent, true);
    };
    TransportRouter first(nullptr, true, false, "usb", factory, key);
    TransportRouter second(nullptr, true, false, "usb", factory, key);
    first.start("R2500-000000000001", 0); second.start("R2500-000000000001", 0);
    QVERIFY(first.active()); QVERIFY(second.active());
    first.stop(); QVERIFY(second.active());
  }
  void cancelReleasesOnlyOwnSnapshotAndAllowsImmediateRetry() {
    QList<QByteArray> output; unsigned entropy = 1;
    HidSession session({[&](QByteArray b) { output.append(b); }, [](QByteArray) {}, [](QString) {},
      [&] { QByteArray id(16, 0); id[0] = char(++entropy); return id; }});
    QVERIFY(session.start(caps(), 0));
    session.command("HISTORY-EXPORT 1 01000000000000000000000000000000\n", 1);
    session.stop();
    Request close;
    QVERIFY(decode(reinterpret_cast<const uint8_t *>(output.last().constData()) + 1, 63, close));
    QVERIFY(close.op == Op::Close); QCOMPARE(close.archive.bytes[0], uint8_t(1));
    session.command("HISTORY-EXPORT 2 02000000000000000000000000000000\n", 2);
    Request next;
    QVERIFY(decode(reinterpret_cast<const uint8_t *>(output.last().constData()) + 1, 63, next));
    QVERIFY(next.op == Op::Open); QCOMPARE(next.archive.bytes[0], uint8_t(2));
    QVERIFY(!(next.operation == close.operation));
  }
};
QTEST_GUILESS_MAIN(HidSessionTests)
#include "hid_session_test.moc"
