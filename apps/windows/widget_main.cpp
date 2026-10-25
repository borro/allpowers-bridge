#include "connection_lease.h"
#include "notices.h"
#include "widget.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("AllpowersWidget");
  app.setApplicationVersion(APP_VERSION);
  app.setWindowIcon(QIcon(":/assets/allpowers.ico"));
  app.setStyle("Fusion");
  app.setQuitOnLastWindowClosed(false);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOptions({{"demo", "Synthetic telemetry; no hardware or user settings"},
                     {"self-test", "Offline standalone test"},
                     {"report", "Write diagnostic JSON", "path"},
                     {"screenshot", "Save card screenshot", "path"},
                     {"test-seconds", "Exit after N seconds", "seconds"},
                     {"export-licenses", "Extract embedded notices", "directory"}});
  parser.process(app);
  if (parser.isSet("export-licenses"))
    return exportEmbeddedLicenses(parser.value("export-licenses")) ? 0 : 4;
  const bool offline = parser.isSet("demo") || parser.isSet("self-test");
  ConnectionLease instance("ALLPOWERS.Widget.Instance.v1");
  if (!offline && !instance.acquire()) {
    QMessageBox::information(
        nullptr, QStringLiteral("ALLPOWERS Widget"),
        QStringLiteral("Виджет уже запущен. Для запуска этой версии выберите «Выйти» в меню "
                       "старого виджета рядом с часами и откройте этот файл снова."));
    return 0;
  }
  QTemporaryDir temporary;
  if (offline && !temporary.isValid())
    return 2;
  if (QGuiApplication::platformName() == "offscreen") {
    QDir fonts(qEnvironmentVariable("SystemRoot") + "/Fonts");
    QFontDatabase::addApplicationFont(fonts.filePath("segoeui.ttf"));
    QFontDatabase::addApplicationFont(fonts.filePath("segoeuib.ttf"));
  }
  const QString directory =
      offline ? temporary.path()
              : QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                    .absolutePath();
  WidgetController widget(directory, offline, QGuiApplication::platformName() != "offscreen");
  widget.start();
  if (offline)
    widget.loadDemo();
  if (parser.isSet("self-test") || parser.isSet("test-seconds")) {
    QTimer::singleShot(
        parser.isSet("self-test") ? 1500 : qMax(1, parser.value("test-seconds").toInt()) * 1000,
        &app, [&] {
          if (parser.isSet("screenshot"))
            widget.card()->renderedImage().save(parser.value("screenshot"));
          auto report = widget.diagnostics();
          const bool notice =
              embeddedLicenseFiles().contains("Qt/qtbase/LICENSES/LGPL-3.0-only.txt");
          bool ok = ap_self_test() && report["history_error"].toString().isEmpty() && notice &&
                    report["points"].toInt() > 0;
          if (QGuiApplication::platformName() != "offscreen")
            ok = ok && report["desktop_attached"].toBool() && report["native_child"].toBool() &&
                 report["native_parent_matches"].toBool() && report["native_visible"].toBool() &&
                 !report["native_popup"].toBool() && report["native_frame_published"].toBool();
          report["embedded_notices"] = embeddedLicenseFiles().size();
          report["lgpl_notice"] = notice;
          report["qt_runtime"] = qVersion();
          report["status"] = ok ? "PASS" : "FAIL";
          if (parser.isSet("report")) {
            QFile file(parser.value("report"));
            if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0)
              ok = false;
          }
          app.exit(ok ? 0 : 3);
        });
  }
  return app.exec();
}
