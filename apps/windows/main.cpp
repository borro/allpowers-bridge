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
  parser.addOptions({{"self-test", "Run an offline core/UI smoke test"},
                     {"demo", "Show synthetic data without Bluetooth"},
                     {"report", "Write diagnostic JSON", "path"},
                     {"screenshot", "Save a UI screenshot", "path"},
                     {"test-seconds", "Close automatically", "seconds"},
                     {"scan-seconds", "Scan BLE without making a connection", "seconds"}});
  parser.process(app);
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
    bool ok =
        info["history_error"].toString().isEmpty() && info["history"].toObject().contains("live");
    if (parser.isSet("report")) {
      QFile out(parser.value("report"));
      if (out.open(QIODevice::WriteOnly)) {
        info["status"] = ok ? "PASS" : "FAIL";
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
