#include "notices.h"
#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("AllpowersMonitor");
  app.setApplicationVersion(APP_VERSION);
  app.setStyle("Fusion");
  app.setWindowIcon(QIcon(":/assets/allpowers.ico"));
  // The offscreen Qt platform does not discover Windows fonts automatically.
  if (QGuiApplication::platformName() == "offscreen") {
    auto fonts = QDir(qEnvironmentVariable("SystemRoot") + "/Fonts");
    for (const QString &name : {QString("segoeui.ttf"), QString("segoeuib.ttf")})
      QFontDatabase::addApplicationFont(fonts.filePath(name));
  }
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOptions(
      {{"self-test", "Run an offline core/UI smoke test"},
       {"demo", "Show synthetic data without Bluetooth"},
       {"report", "Write diagnostic JSON", "path"},
       {"screenshot", "Save a UI screenshot", "path"},
       {"export-licenses", "Extract embedded notices and license texts", "directory"},
       {"verify-station", "Verify saved station telemetry, then disconnect", "seconds"},
       {"test-seconds", "Close automatically", "seconds"},
       {"scan-seconds", "Scan BLE without making a connection", "seconds"}});
  parser.process(app);
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
    BleTransport transport(&app);
    ap_snapshot latest{};
    QElapsedTimer clock;
    clock.start();
    qint64 firstFresh = -1;
    bool started = false, finished = false;
    auto finish = [&] {
      if (finished)
        return;
      finished = true;
      const bool ok = transport.connected() && latest.fresh && latest.valid_frames >= 3 &&
                      firstFresh >= 0 && clock.elapsed() - firstFresh >= 5000;
      auto report = transport.diagnostics();
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
      transport.stop();
      report["disconnected_after_test"] = !transport.active();
      QFile file(parser.value("report"));
      const bool written =
          file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0;
      app.exit(ok && written ? 0 : 5);
    };
    QObject::connect(&transport, &BleTransport::found, &app,
                     [&](QString found, QString, int, int hint) {
                       if (!started && found.compare(address, Qt::CaseInsensitive) == 0) {
                         started = true;
                         const int model = settings["profile"].toInt();
                         transport.start(found, model ? model : hint);
                       }
                     });
    QObject::connect(&transport, &BleTransport::snapshot, &app, [&](ap_snapshot data) {
      latest = data;
      if (data.fresh && firstFresh < 0)
        firstFresh = clock.elapsed();
      if (firstFresh >= 0 && clock.elapsed() - firstFresh >= 5000 && data.fresh)
        finish();
    });
    QTimer::singleShot(seconds * 1000, &app, finish);
    transport.scan();
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
  if (selfTest || demo)
    qputenv("ALLPOWERS_DATA_DIR", temporary.path().toUtf8());
  if (!ap_self_test())
    return 2;
  MonitorWindow window(selfTest || demo);
  window.show();
  if (demo || selfTest)
    window.loadDemo();
  auto finish = [&] {
    if (parser.isSet("screenshot"))
      window.grab().save(parser.value("screenshot"));
    auto info = window.diagnostics();
    const bool lgplNotice = embeddedLicenseFiles().contains("Qt/qtbase/LICENSES/LGPL-3.0-only.txt");
    bool ok = info["history_error"].toString().isEmpty() &&
              info["history"].toObject().contains("live") && lgplNotice;
    if (parser.isSet("report")) {
      QFile out(parser.value("report"));
      if (out.open(QIODevice::WriteOnly)) {
        info["status"] = ok ? "PASS" : "FAIL";
        info["embedded_notices"] = embeddedLicenseFiles().size();
        info["lgpl_notice"] = lgplNotice;
        info["bluetooth_used"] = false;
        out.write(QJsonDocument(info).toJson(QJsonDocument::Indented));
      } else
        ok = false;
    }
    app.exit(ok ? 0 : 3);
  };
  if (selfTest || parser.isSet("test-seconds"))
    QTimer::singleShot(selfTest ? 1500 : qMax(1, parser.value("test-seconds").toInt()) * 1000, &app,
                       finish);
  return app.exec();
}
