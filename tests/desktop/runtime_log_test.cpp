#include "bluetooth.h"
#include "runtime_log.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <cstdlib>
#include <thread>

class RuntimeLogTests : public QObject {
  Q_OBJECT
  static QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      return {};
    return file.readAll();
  }
private slots:
  void survivesAbruptExit() {
    QTemporaryDir dir;
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--unclean-probe", dir.path()});
    QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitCode(), 23);
    const auto bytes = read(dir.filePath("diagnostics.jsonl"));
    QVERIFY(bytes.contains("runtime.start"));
    QVERIFY(bytes.contains("probe.before_exit"));
    QVERIFY(!bytes.contains("runtime.end"));
    for (const auto &line : bytes.trimmed().split('\n'))
      QVERIFY(QJsonDocument::fromJson(line).isObject());
  }
  void boundedRotationAndPrivacy() {
    QTemporaryDir dir;
    const auto path = dir.filePath("diagnostics.jsonl");
    {
      QFile old(path);
      QVERIFY(old.open(QIODevice::WriteOnly));
      QCOMPARE(old.write(QByteArray(2 * 1024 * 1024, 'x')), qint64(2 * 1024 * 1024));
    }
    {
      RuntimeLog log(dir.path());
      RuntimeLog::record("probe.private",
                         {{"address", "02:00:00:00:00:03"},
                          {"nested", QJsonObject{{"usb_serial", "R2500-000000000001"}}},
                          {"message", "peer=02:00:00:00:00:03 R2500-000000000001"}});
      std::thread worker([] { qWarning("probe.worker_warning"); });
      worker.join();
    }
    QVERIFY(QFile::exists(path + ".1"));
    QVERIFY(!QFile::exists(path + ".3"));
    const auto bytes = read(path);
    QVERIFY(bytes.contains("runtime.end"));
    QVERIFY(bytes.contains("probe.worker_warning"));
    QVERIFY(!bytes.contains("02:00:00:00:00:03"));
    QVERIFY(!bytes.contains("R2500-000000000001"));
  }
  void recordsFreshnessWithoutLoggingEverySample() {
    QTemporaryDir dir;
    RuntimeLog log(dir.path());
    BleTransport transport(nullptr, true, false);
    TransportTrace trace(&transport, &transport);
    ap_snapshot value{};
    value.abi_version = 1;
    value.fresh = 1;
    value.known = 1;
    for (int i = 0; i < 100; ++i) {
      value.valid_frames = quint32(i);
      emit transport.snapshot(value);
    }
    value.fresh = 0;
    emit transport.snapshot(value);
    for (int i = 0; i < 100; ++i)
      emit transport.stateChanged();
    const auto bytes = read(RuntimeLog::path());
    QCOMPARE(bytes.count("telemetry.state"), 2);
    QCOMPARE(bytes.count("transport.state"), 1);
    QVERIFY(bytes.contains("\"snapshot_count\":101"));
  }
  void unavailableDirectoryDoesNotStopApplication() {
    QTemporaryDir dir;
    QFile file(dir.filePath("file"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    RuntimeLog log(file.fileName());
    RuntimeLog::record("probe.unwritable");
    QVERIFY(!QFile::exists(file.fileName() + "/diagnostics.jsonl"));
  }
};

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--unclean-probe")) {
    RuntimeLog log(app.arguments().last());
    RuntimeLog::record("probe.before_exit");
    std::_Exit(23);
  }
  RuntimeLogTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "runtime_log_test.moc"
