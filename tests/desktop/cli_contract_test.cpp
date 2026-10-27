#include <QtTest>
#include <QApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDialog>
#include <QListWidget>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QPushButton>
#include "../../apps/windows/window.h"
#include "../../apps/windows/usb_transport.h"
#include "../../apps/windows/battery_probe.h"
#include "../../apps/windows/notices.h"
#include "../../apps/windows/monitor_application.h"

namespace Fake {
QString scenario;
int scans = 0, starts = 0, stops = 0;
class Transport : public TelemetryTransport {
  bool running = false;
  QTimer timer;
public:
  explicit Transport(QObject *parent = nullptr) : TelemetryTransport(parent) {
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, [this] {
      ap_snapshot s{}; s.abi_version = 1; s.fresh = s.main_seen = s.settings_seen = 1;
      s.valid_frames = 4; s.soc = 72; s.source = 2; s.known = 513;
      if (scenario != "station-fail") emit snapshot(s);
    });
  }
  bool active() const override { return running; }
  bool connected() const override { return running && scenario != "usb-fail"; }
  bool scanning() const override { return !running && scans > 0; }
  QString address() const override { return "02:00:00:00:00:01"; }
  QString phase() const override { return running ? "connected" : "idle"; }
  QJsonObject diagnostics() const override {
    return {{"active", running}, {"connected", connected()}, {"usb_board", "waveshare-147"}};
  }
  void scan() override {
    ++scans;
    emit found("02:00:00:00:00:02", "Other fixture", -80, 1);
    emit found(address(), "Synthetic station", -42, 3);
  }
  void start(const QString &id, int model) override {
    Q_ASSERT(id == address()); Q_ASSERT(model == 3); ++starts; running = true; timer.start();
  }
  void resume(const QString &, const QString &, int) override {
    ++starts; running = true; timer.start(); emit historyImported({});
  }
  void stop() override { ++stops; running = false; timer.stop(); }
};
QList<UsbPort> ports() { return {{"R2500-000000000001", "COM17", "Synthetic USB"}}; }
int battery(const QString &report, int seconds) { return !report.isEmpty() && seconds == 2 ? 17 : 2; }
// Keep the real window/storage, but control when its asynchronous history result
// becomes observable to the CLI. This reproduces a slow worker without sleeps
// on the GUI thread or dependence on the CI machine's disk speed.
class Window : public MonitorWindow {
  QElapsedTimer age;
public:
  Window(bool offline, QWidget *parent, TelemetryTransport *transport)
      : MonitorWindow(offline, parent, transport) { age.start(); }
  QJsonObject diagnostics() const {
    auto info = MonitorWindow::diagnostics();
    if (scenario == "self-test-no-history" ||
        (scenario == "self-test-slow-history" && age.elapsed() < 2000))
      info["history"] = QJsonObject{};
    if (scenario == "self-test-history-error")
      info["history_error"] = "Injected history failure";
    return info;
  }
};
} // namespace Fake
// Argument parsing, result criteria, event loops, reports and the GUI are
// production code; external I/O and history-result timing are controlled here.
#define BleTransport Fake::Transport
#define UsbTransport Fake::Transport
#define allpowersUsbPorts Fake::ports
#define runBatteryProbe Fake::battery
#define MonitorWindow Fake::Window
#include "../../apps/windows/monitor_application.cpp"
#undef BleTransport
#undef UsbTransport
#undef allpowersUsbPorts
#undef runBatteryProbe
#undef MonitorWindow

class CliContract : public QObject {
  Q_OBJECT
private slots:
  void commands_data() {
    QTest::addColumn<QString>("scenario");
    for (auto *name : {"scan-usb", "scan-usb-unwritable", "scan-ble", "usb-pass", "usb-fail",
        "station-pass", "station-fail", "demo", "self-test", "self-test-slow-history",
        "self-test-no-history", "self-test-history-error", "report-unwritable", "licenses", "battery"})
      QTest::newRow(name) << QString(name);
  }
  void commands() {
    QFETCH(QString, scenario);
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM", "offscreen");
    process.setProcessEnvironment(env);
    process.start(QCoreApplication::applicationFilePath(), {"--contract-child", scenario});
    QVERIFY(process.waitForStarted()); QVERIFY(process.waitForFinished(25000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
  }
  void invalidArguments() {
    auto &app = *qobject_cast<QApplication *>(qApp);
    for (const QStringList options : {QStringList{"--language", "xx"}, {"--scan-usb"},
        {"--verify-usb", "4"}, {"--verify-usb", "121"},
        {"--verify-usb", "5", "--usb-device", "fixture", "--usb-board", "unknown", "--report", "unused"},
        {"--scan-seconds", "0"}, {"--scan-seconds", "31"}, {"--verify-station", "9"},
        {"--verify-station", "121"}, {"--verify-station", "10", "--report", "unused"}}) {
      QCOMPARE(runMonitorApplication(app, QStringList{"AllpowersMonitor"} + options), 2);
    }
    QCOMPARE(Fake::scans, 0); QCOMPARE(Fake::starts, 0);
  }
  void licensesAndDialog() {
    QTemporaryDir dir;
    const auto names = embeddedLicenseFiles(); QVERIFY(names.size() > 10);
    QVERIFY(exportEmbeddedLicenses(dir.path()));
    for (const auto &name : names) {
      QFile original(":/licenses/" + name), exported(dir.filePath(name));
      QVERIFY(original.open(QIODevice::ReadOnly)); QVERIFY(exported.open(QIODevice::ReadOnly));
      QCOMPARE(exported.readAll(), original.readAll());
    }
    QFile blocker(dir.filePath("blocker")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
    QVERIFY(!exportEmbeddedLicenses(blocker.fileName()));
    bool inspected = false;
    QTimer::singleShot(0, qApp, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dialog) return;
      // Always close the modal even when a verification below fails.
      QTimer::singleShot(0, dialog, &QDialog::reject);
      auto *list = dialog->findChild<QListWidget *>(); auto *text = dialog->findChild<QTextBrowser *>();
      QVERIFY(list && text); QCOMPARE(list->count(), names.size());
      QCOMPARE(list->currentItem()->text(), QString("THIRD_PARTY_NOTICES.md"));
      QVERIFY(text->toPlainText().contains("Qt"));
      const auto matches = list->findItems("Qt/qtbase/LICENSES/LGPL-3.0-only.txt", Qt::MatchExactly);
      QCOMPARE(matches.size(), 1);
      list->setCurrentItem(matches.first());
      QVERIFY(text->toPlainText().contains("GNU LESSER GENERAL PUBLIC LICENSE"));
      inspected = true;
    });
    showNotices(nullptr); QVERIFY(inspected);
  }
};

int runChild(QApplication &app, const QString &scenario) {
  Fake::scenario = scenario;
  app.setQuitOnLastWindowClosed(false);
  QTemporaryDir dir; if (!dir.isValid()) return 70;
  qputenv("ALLPOWERS_DATA_DIR", dir.path().toUtf8());
  QFile settings(dir.filePath("settings.json")); if (!settings.open(QIODevice::WriteOnly)) return 71;
  settings.write("{\"address\":\"02:00:00:00:00:01\",\"profile\":0}"); settings.close();
  const auto report = scenario.endsWith("unwritable") ? dir.path() : dir.filePath("report.json");
  QStringList args{"AllpowersMonitor", "--language", "en", "--report", report};
  int expected = 0;
  if (scenario.startsWith("scan-usb")) { args << "--scan-usb"; if (scenario.endsWith("unwritable")) expected = 3; }
  else if (scenario == "scan-ble") args << "--scan-seconds" << "1";
  else if (scenario.startsWith("usb-")) {
    args << "--verify-usb" << "5" << "--usb-device" << "R2500-000000000001" << "--usb-board" << "waveshare-147";
    if (scenario.endsWith("fail")) expected = 5;
  } else if (scenario.startsWith("station-")) {
    args << "--verify-station" << "10"; if (scenario.endsWith("fail")) expected = 5;
  } else if (scenario == "licenses") args << "--export-licenses" << dir.filePath("licenses");
  else if (scenario == "battery") { args << "--battery-probe" << "2"; expected = 17; }
  else if (scenario.startsWith("self-test")) {
    args << "--self-test" << "--screenshot" << dir.filePath("ui.png");
    if (scenario == "self-test-no-history" || scenario == "self-test-history-error") expected = 3;
  }
  else { args << "--demo" << "--test-seconds" << "1"; if (scenario.endsWith("unwritable")) expected = 3; }
  MonitorRuntime runtime;
  runtime.serviceInstalled = [] { return false; };
  runtime.leaseKey = "ALLPOWERS.Test.Cli." + QUuid::createUuid().toString();
  runtime.makeTransport = [](QObject *parent, bool) { return new Fake::Transport(parent); };
  QElapsedTimer elapsed; elapsed.start();
  const int result = runMonitorApplication(app, args, runtime);
  if (result != expected) { qCritical() << scenario << result << expected; return 72; }
  if (scenario.endsWith("unwritable") || scenario == "battery" || scenario == "licenses") return 0;
  QFile file(report); if (!file.open(QIODevice::ReadOnly)) return 73;
  const auto data = QJsonDocument::fromJson(file.readAll()).object();
  if (scenario == "scan-usb") {
    if (data["boards"].toArray().size() != 1 || Fake::starts || Fake::scans) return 74;
  } else if (scenario == "scan-ble") {
    if (data["devices"].toArray().size() != 2 || Fake::starts || data["connection_attempted"].toBool()) return 75;
  } else {
    if (data["status"].toString() != (expected ? "FAIL" : "PASS")) return 76;
    if (scenario.startsWith("usb-") && (!data["port_closed_after_test"].toBool() || data["samples"].toInt() < 2)) return 77;
    if (scenario.startsWith("station-") && (!data["disconnected_after_test"].toBool() || Fake::starts != 1)) return 78;
    if (scenario == "self-test" && !QFile::exists(dir.filePath("ui.png"))) return 79;
    if (scenario == "self-test-slow-history" &&
        (elapsed.elapsed() < 2000 || !data["history_ready"].toBool() || Fake::starts || Fake::scans)) return 80;
    if (scenario == "self-test-no-history" &&
        (elapsed.elapsed() < 10000 || data["history_ready"].toBool() ||
         data["self_test_error"].toString() != "Timed out waiting for history initialization")) return 81;
    if (scenario == "self-test-history-error" &&
        data["self_test_error"].toString() != "History initialization failed") return 82;
  }
  return 0;
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  if (argc > 2 && QByteArray(argv[1]) == "--contract-child") return runChild(app, QString::fromLocal8Bit(argv[2]));
  QTemporaryDir dir; qputenv("ALLPOWERS_DATA_DIR", dir.path().toUtf8());
  CliContract test; return QTest::qExec(&test, argc, argv);
}
#include "cli_contract_test.moc"
