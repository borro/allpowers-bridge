#include "monitor_application.h"
#include "battery_probe.h"
#include "connection_lease.h"
#include "i18n.h"
#include "integration.h"
#include "notices.h"
#include "product-version.h"
#include "runtime_log.h"
#include "usb_hid.h"
#include "usb_transport.h"
#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

int runMonitorApplication(QApplication &app, const QStringList &arguments,
                          const MonitorRuntime &runtime) {
  app.setApplicationName("AllpowersMonitor");
  app.setApplicationVersion(APP_VERSION);
  app.setStyle("Fusion");
  app.setWindowIcon(QIcon(":/assets/allpowers.ico"));

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOptions(
      {{"language", "Interface language for this run: system, ru, en, zh-CN, zh-TW", "language"},
       {"self-test", "Run an offline core/UI smoke test"},
       {"demo", "Show synthetic data without Bluetooth"},
       {"report", "Write diagnostic JSON", "path"},
       {"screenshot", "Save a UI screenshot", "path"},
       {"export-licenses", "Extract embedded notices and license texts", "directory"},
       {"verify-station", "Verify saved station telemetry, then disconnect", "seconds"},
       {"test-seconds", "Close automatically", "seconds"},
       {"scan-seconds", "Scan BLE without making a connection", "seconds"},
       {"scan-usb", "List supported USB HID collections without telemetry requests"},
       {"verify-usb", "Read telemetry from an explicitly selected USB board", "seconds"},
       {"usb-device", "Exact USB serial for the manual USB verification", "serial"},
       {"usb-board", "Expected board profile for manual verification", "board"},
       {"battery-probe", "Read native battery IOCTLs/power events without BLE or mutations",
        "seconds"}});
  parser.process(arguments);
  if (parser.isSet("language") && !supportedUiLanguages().contains(parser.value("language")))
    return 2;
  if (parser.isSet("scan-usb")) {
    if (!parser.isSet("report"))
      return 2;
    QJsonArray rows;
    for (const auto &p : allpowersHidPorts())
      rows.append(
          QJsonObject{{"serial", p.serial}, {"port", p.port}, {"description", p.description}});
    QFile out(parser.value("report"));
    if (!out.open(QIODevice::WriteOnly))
      return 3;
    out.write(QJsonDocument(QJsonObject{{"boards", rows}, {"bluetooth_used", false}}).toJson());
    return 0;
  }
  if (parser.isSet("verify-usb")) {
    const int seconds = parser.value("verify-usb").toInt();
    const auto serial = parser.value("usb-device"), board = parser.value("usb-board");
    if (seconds < 5 || seconds > 120 || serial.isEmpty() || !parser.isSet("report") ||
        (board != "waveshare-147" && board != "pocket-096"))
      return 2;
    UsbTransport usb(&app);
    ap_snapshot last{};
    int samples = 0;
    HistoryPoints archive;
    QObject::connect(&usb, &TelemetryTransport::snapshot, &app, [&](ap_snapshot s) {
      last = s;
      if (s.fresh)
        samples++;
    });
    QObject::connect(&usb, &TelemetryTransport::historyImported, &app,
                     [&](HistoryPoints p) { archive = p; });
    usb.resume(serial, {}, 0);
    QTimer::singleShot(seconds * 1000, &app, [&] {
      auto report = usb.diagnostics();
      const bool ok = usb.connected() && last.fresh && samples >= 2 && report["usb_board"] == board;
      report["status"] = ok ? "PASS" : "FAIL";
      report["soc"] = int(last.soc);
      report["input_w"] = int(last.input_w);
      report["output_w"] = int(last.output_w);
      report["source"] = int(last.source);
      report["fresh"] = bool(last.fresh);
      report["samples"] = samples;
      report["archive_points"] = archive.size();
      report["bluetooth_used"] = false;
      usb.stop();
      report["port_closed_after_test"] = !usb.active();
      QFile file(parser.value("report"));
      const auto bytes = QJsonDocument(report).toJson();
      const bool saved = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
      app.exit(ok && saved ? 0 : 5);
    });
    return app.exec();
  }
  if (parser.isSet("battery-probe"))
    return runBatteryProbe(parser.value("report"), parser.value("battery-probe").toInt());
  if (parser.isSet("export-licenses"))
    return exportEmbeddedLicenses(parser.value("export-licenses")) ? 0 : 4;
  if (parser.isSet("verify-station")) {
    const int seconds = parser.value("verify-station").toInt();
    QFile settingsFile(QDir(monitorDataDirectory()).filePath("settings.json"));
    if (seconds < 10 || seconds > 120 || !parser.isSet("report") ||
        !settingsFile.open(QIODevice::ReadOnly))
      return 2;
    const auto settings = QJsonDocument::fromJson(settingsFile.readAll()).object();
    const auto address = settings["address"].toString();
    if (address.isEmpty())
      return 2;
    ConnectionLease lease(runtime.leaseKey);
    const bool installed =
        runtime.serviceInstalled ? runtime.serviceInstalled() : integrationInstalled();
    if (installed || !lease.acquire()) {
      QFile file(parser.value("report"));
      if (file.open(QIODevice::WriteOnly))
        file.write(
            QJsonDocument(
                QJsonObject{
                    {"status", "BUSY"},
                    {"error",
                     "Direct verification unavailable: installed service or another direct owner"},
                    {"bluetooth_used", false}})
                .toJson());
      return 5;
    }
    std::unique_ptr<TelemetryTransport> transport(
        runtime.makeTransport ? runtime.makeTransport(&app, false) : new BleTransport(&app));
    ap_snapshot latest{};
    QElapsedTimer clock;
    clock.start();
    qint64 firstFresh = -1;
    bool started = false, finished = false;
    auto finish = [&] {
      if (finished)
        return;
      finished = true;
      const bool ok = transport->connected() && latest.fresh && latest.valid_frames >= 3 &&
                      firstFresh >= 0 && clock.elapsed() - firstFresh >= 5000;
      auto report = transport->diagnostics();
      report["status"] = ok ? "PASS" : "FAIL";
      report["fresh"] = bool(latest.fresh);
      report["valid_frames"] = int(latest.valid_frames);
      report["invalid_frames"] = int(latest.invalid_frames);
      report["soc"] = int(latest.soc);
      report["input_w"] = int(latest.input_w);
      report["output_w"] = int(latest.output_w);
      report["runtime_s"] = int(latest.runtime_s);
      report["source"] = int(latest.source);
      report["source_raw"] = int(latest.source_raw);
      report["duration_ms"] = double(clock.elapsed());
      transport->stop();
      report["disconnected_after_test"] = !transport->active();
      QFile file(parser.value("report"));
      const bool written =
          file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0;
      app.exit(ok && written ? 0 : 5);
    };
    QObject::connect(transport.get(), &TelemetryTransport::found, &app,
                     [&](QString found, QString, int, int hint) {
                       if (!started && found.compare(address, Qt::CaseInsensitive) == 0) {
                         started = true;
                         const int model = settings["profile"].toInt();
                         transport->start(found, model ? model : hint);
                       }
                     });
    QObject::connect(transport.get(), &TelemetryTransport::snapshot, &app, [&](ap_snapshot data) {
      latest = data;
      if (data.fresh && firstFresh < 0)
        firstFresh = clock.elapsed();
      if (firstFresh >= 0 && clock.elapsed() - firstFresh >= 5000 && data.fresh)
        finish();
    });
    QTimer::singleShot(seconds * 1000, &app, finish);
    transport->scan();
    return app.exec();
  }
  if (parser.isSet("scan-seconds")) {
    int seconds = parser.value("scan-seconds").toInt();
    if (seconds < 1 || seconds > 30 || !parser.isSet("report"))
      return 2;
    BleTransport transport(&app);
    QJsonArray devices;
    QObject::connect(&transport, &BleTransport::found, &app,
                     [&](QString address, QString name, int rssi, int model) {
                       devices.append(QJsonObject{
                           {"address", address}, {"name", name}, {"rssi", rssi}, {"model", model}});
                     });
    transport.scan();
    QTimer::singleShot(seconds * 1000, &app, [&] {
      auto report = transport.diagnostics();
      report["devices"] = devices;
      report["connection_attempted"] = false;
      transport.stop();
      QFile file(parser.value("report"));
      if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
      app.exit(0);
    });
    return app.exec();
  }
  bool selfTest = parser.isSet("self-test"), demo = parser.isSet("demo");
  QTemporaryDir temporary;
  if ((selfTest || demo) && !temporary.isValid()) {
    qCritical("Cannot create an isolated data directory for demo/self-test");
    return 3;
  }
  if (selfTest || demo)
    qputenv("ALLPOWERS_DATA_DIR", temporary.path().toUtf8());
  RuntimeLog runtimeLog(monitorDataDirectory());
  const auto quitLog = QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                                        [] { RuntimeLog::record("runtime.quit_requested"); });
  const auto disconnectQuitLog = qScopeGuard([&] { QObject::disconnect(quitLog); });
  UiLanguage language(parser.isSet("language") ? parser.value("language")
                                               : savedUiLanguage(monitorDataDirectory()));
  if (!language.valid())
    return 3;
  initializeUiFonts();
  if (!ap_self_test())
    return 2;
  MonitorWindow window(selfTest || demo, nullptr,
                       runtime.makeTransport ? runtime.makeTransport(nullptr, selfTest || demo)
                                             : nullptr);
  window.show();
  if (demo || selfTest)
    window.loadDemo();
  auto finish = [&] {
    if (parser.isSet("screenshot"))
      window.grab().save(parser.value("screenshot"));
    auto info = window.diagnostics();
    const bool lgplNotice = embeddedLicenseFiles().contains("Qt/qtbase/LICENSES/LGPL-3.0-only.txt");
    const bool historyReady = info["history"].toObject().contains("live");
    bool ok = info["history_error"].toString().isEmpty() && historyReady && lgplNotice;
    if (selfTest) {
      info["history_ready"] = historyReady;
      if (!info["history_error"].toString().isEmpty())
        info["self_test_error"] = "History initialization failed";
      else if (!historyReady)
        info["self_test_error"] = "Timed out waiting for history initialization";
      else if (!lgplNotice)
        info["self_test_error"] = "Embedded LGPL notice is missing";
    }
    if (parser.isSet("report")) {
      QFile out(parser.value("report"));
      if (out.open(QIODevice::WriteOnly)) {
        info["status"] = ok ? "PASS" : "FAIL";
        info["embedded_notices"] = embeddedLicenseFiles().size();
        info["lgpl_notice"] = lgplNotice;
        info["qt_runtime"] = qVersion();
        info["requested_mode"] = selfTest ? "self-test" : demo ? "demo" : "normal";
        out.write(QJsonDocument(info).toJson(QJsonDocument::Indented));
      } else
        ok = false;
    }
    app.exit(ok ? 0 : 3);
  };
  QTimer selfTestPoll;
  QElapsedTimer selfTestDeadline;
  if (selfTest) {
    // SQLite opens on the history worker. A fixed delay can report failure on
    // a busy runner before its first result reaches the GUI thread.
    // The failure bound is below CTest's standalone process timeout (20 s).
    selfTestDeadline.start();
    QObject::connect(&selfTestPoll, &QTimer::timeout, &app, [&] {
      const auto info = window.diagnostics();
      if (info["history"].toObject().contains("live") ||
          !info["history_error"].toString().isEmpty() || selfTestDeadline.elapsed() >= 10000) {
        selfTestPoll.stop();
        finish();
      }
    });
    selfTestPoll.start(50);
  } else if (parser.isSet("test-seconds"))
    QTimer::singleShot(qMax(1, parser.value("test-seconds").toInt()) * 1000, &app, finish);
  return app.exec();
}
