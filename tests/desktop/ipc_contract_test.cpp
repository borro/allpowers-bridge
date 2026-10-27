#include <QtTest>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include "service_client.h"
#include "service_host.h"
#include "ipc.h"

namespace {
QString uniquePipe() { return "AllpowersTest-" + QUuid::createUuid().toString(QUuid::Id128); }
QByteArray wire(QJsonObject message) { message["v"] = Ipc::Version; return Ipc::frame(message); }
QJsonObject state(QString address = "fixture") {
  ap_snapshot s{}; s.abi_version = 1;
  return {{"op", "state"}, {"data", Ipc::snapshot(s)},
          {"diagnostics", QJsonObject{{"address", address}, {"active", true}, {"connected", true}, {"phase", "ready"}}}};
}
HistoryPoint point(qint64 wall) {
  HistoryPoint p; p.address = "fixture"; p.session = "test"; p.wall = wall; p.tick = wall;
  p.stride = 1; p.data.abi_version = 1; p.data.soc = 42; return p;
}
}
class IpcContract : public QObject {
  Q_OBJECT
private slots:
  void clientHistoryAndEvents() {
    QLocalServer server; const auto name = uniquePipe(); QVERIFY(server.listen(name));
    ServiceTransport client(nullptr, name, false);
    QSignalSpy history(&client, &ServiceTransport::historyReady), snapshots(&client, &ServiceTransport::snapshot);
    QSignalSpy found(&client, &ServiceTransport::found), packets(&client, &ServiceTransport::packet);
    QSignalSpy logs(&client, &ServiceTransport::logReceived);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    QList<QJsonObject> requests; QByteArray input;
    connect(peer, &QLocalSocket::readyRead, this, [&] {
      input += peer->readAll(); QVERIFY(Ipc::consume(input, requests));
    });
    peer->write(wire(state())); QTRY_VERIFY(client.connected()); QCOMPARE(client.phase(), QString("ready"));
    QTimer keepalive; keepalive.setInterval(500);
    connect(&keepalive, &QTimer::timeout, peer, [peer] { peer->write(wire(state())); }); keepalive.start();
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(requests.begin(), requests.end(), [](auto o) { return o["op"] == "history"; }), 7000);
    const auto now = QDateTime::currentSecsSinceEpoch(); auto a = point(now-3), b = point(now-2), c = point(now-1);
    auto page = [](int offset, int total, QJsonArray rows) {
      return QJsonObject{{"op", "history"}, {"address", "fixture"}, {"offset", offset}, {"total", total}, {"points", rows}};
    };
    peer->write(wire(page(0, 2, {Ipc::point(a)})));
    peer->write(wire({{"op", "sample"}, {"point", Ipc::point(c)}}));
    peer->write(wire(page(1, 2, {Ipc::point(b)})));
    QTRY_VERIFY(!history.isEmpty() && qvariant_cast<HistoryPoints>(history.last()[0]).size() == 3);
    const auto merged = qvariant_cast<HistoryPoints>(history.last()[0]);
    QCOMPARE(merged[0].wall, a.wall); QCOMPARE(merged.last().wall, c.wall);
    peer->write(wire({{"op", "sample"}, {"point", Ipc::point(point(now-172801))}}));
    peer->write(wire({{"op", "sample"}, {"point", QJsonObject{}}}));
    auto other = c; other.address = "other";
    peer->write(wire({{"op", "sample"}, {"point", Ipc::point(other)}}));
    peer->write(wire({{"op", "found"}, {"address", "synthetic"}, {"name", "test"}, {"rssi", -42}, {"model", 3}}));
    peer->write(wire({{"op", "packet"}, {"hex", "0102"}, {"valid", true}}));
    peer->write(wire({{"op", "log"}, {"message", "event"}}));
    peer->write(wire({{"op", "error"}, {"message", "test failure"}}));
    QTRY_COMPARE(logs.count(), 1); QCOMPARE(found.count(), 1); QCOMPARE(packets.count(), 1);
    QCOMPARE(qvariant_cast<HistoryPoints>(history.last()[0]).size(), 3);
    QCOMPARE(client.diagnostics()["error"].toString(), QString("test failure"));
    client.scan(); client.start("fixture", 3); client.stop();
    QTRY_VERIFY(std::any_of(requests.begin(), requests.end(), [](auto o) { return o["op"] == "stop"; }));
    keepalive.stop(); peer->write(wire(state("new-fixture")));
    QTRY_COMPARE(client.address(), QString("new-fixture"));
    QVERIFY(qvariant_cast<HistoryPoints>(history.last()[0]).isEmpty());
    peer->abort(); QTRY_VERIFY(!client.available()); QVERIFY(!client.active());
    QCOMPARE(qvariant_cast<ap_snapshot>(snapshots.last()[0]).fresh, 0u);
    client.scan(); // disconnected writes are ignored
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2500);
  }
  void corruptReplies_data() {
    QTest::addColumn<QByteArray>("reply");
    QTest::newRow("version") << Ipc::frame({{"v", 99}});
    QTest::newRow("length") << QByteArray(4, '\0');
    QTest::newRow("snapshot") << wire({{"op", "state"}, {"data", QJsonObject{}}});
    auto page = [](QJsonArray rows, int total) {
      return wire({{"op", "history"}, {"address", "fixture"}, {"offset", 0}, {"total", total}, {"points", rows}});
    };
    QTest::newRow("empty-page") << page({}, 1);
    QTest::newRow("invalid-point") << page({QJsonObject{}}, 1);
    auto p = point(100); p.address = "foreign";
    QTest::newRow("foreign-point") << page({Ipc::point(p)}, 1);
    QJsonArray rows; for (int i=0; i<=Ipc::HistoryPage; ++i) rows.append(Ipc::point(point(i)));
    QTest::newRow("oversized-page") << page(rows, rows.size());
  }
  void corruptReplies() {
    QFETCH(QByteArray, reply);
    QLocalServer server; const auto name = uniquePipe(); QVERIFY(server.listen(name));
    ServiceTransport client(nullptr, name, false);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    peer->write(wire(state())); QTRY_COMPARE(client.address(), QString("fixture"));
    peer->write(reply); QTRY_VERIFY(!client.available());
    QVERIFY(!client.diagnostics()["error"].toString().isEmpty());
  }
  void inconsistentHistoryIsDiscarded() {
    QLocalServer server; const auto name = uniquePipe(); QVERIFY(server.listen(name));
    ServiceTransport client(nullptr, name, false); QSignalSpy history(&client, &ServiceTransport::historyReady);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    peer->write(wire(state())); QTRY_COMPARE(client.address(), QString("fixture")); history.clear();
    for (auto page : {QJsonObject{{"offset", 0}, {"total", -1}, {"address", "fixture"}},
        {{"offset", 0}, {"total", 7001}, {"address", "fixture"}},
        {{"offset", 1}, {"total", 1}, {"address", "fixture"}},
        {{"offset", 0}, {"total", 0}, {"address", "other"}}}) {
      page["op"] = "history"; peer->write(wire(page));
    }
    peer->write(wire({{"op", "log"}, {"message", "barrier"}}));
    QSignalSpy logs(&client, &ServiceTransport::logReceived); QTRY_COMPARE(logs.count(), 1);
    QVERIFY(client.available()); QVERIFY(history.isEmpty());
  }
  void rejectsUntrustedOwner() {
    QLocalServer server; const auto name = uniquePipe(); QVERIFY(server.listen(name));
    ServiceTransport client(nullptr, name, true);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    QTRY_COMPARE(peer->state(), QLocalSocket::UnconnectedState); QVERIFY(!client.available());
  }
  void serverValidationAndLimits() {
    QTemporaryDir dir; const auto name = uniquePipe();
    ServiceHost empty({{}, name, true, false}); QVERIFY(!empty.start());
    ap_snapshot s{}; s.abi_version = 1; QVERIFY(!empty.injectLabSnapshot(s));
    ServiceHost host({dir.path(), name, true, false}); QVERIFY(host.start()); QVERIFY(!host.start());
    s.soc = 101; QVERIFY(!host.injectLabSnapshot(s));
    QLocalSocket client; client.connectToServer(name); QTRY_COMPARE(client.state(), QLocalSocket::ConnectedState);
    QByteArray buffer; QList<QJsonObject> responses;
    connect(&client, &QLocalSocket::readyRead, this, [&] { buffer += client.readAll(); QVERIFY(Ipc::consume(buffer, responses)); });
    client.write(wire({{"op", "unknown"}}));
    client.write(wire({{"op", "select"}, {"address", "bad"}, {"profile", 9}}));
    client.write(wire({{"op", "history"}, {"offset", 0}}));
    client.write(wire({{"op", "scan"}}));
    QTRY_VERIFY(responses.size() >= 4);
    QVERIFY(std::any_of(responses.begin(), responses.end(), [](auto o) { return o["message"] == "Invalid station selection"; }));
    QVERIFY(std::any_of(responses.begin(), responses.end(), [](auto o) { return o["op"] == "history" && o["total"].toInt(-1) == 0; }));
    auto *ble = host.findChild<BleTransport *>(); QVERIFY(ble);
    emit ble->found("synthetic", "fixture", -30, 3); emit ble->packet("0102", true);
    QTRY_VERIFY(std::any_of(responses.begin(), responses.end(), [](auto o) { return o["op"] == "packet"; }));
    client.write(wire({{"op", "history"}, {"offset", -1}}));
    QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
    QLocalSocket flood; flood.connectToServer(name); QTRY_COMPARE(flood.state(), QLocalSocket::ConnectedState);
    for (int i=0; i<100; ++i) flood.write(wire({{"op", "state"}}));
    QTRY_COMPARE(flood.state(), QLocalSocket::UnconnectedState);
  }
};
QTEST_GUILESS_MAIN(IpcContract)
#include "ipc_contract_test.moc"
