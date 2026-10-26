#include "connection_lease.h"
#include "i18n.h"
#include "notices.h"
#include "product-version.h"
#include "widget.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
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
  parser.addOptions({{"language", "Interface language for this run: system, ru, en", "language"},
                     {"demo", "Synthetic telemetry; no hardware or user settings"},
                     {"self-test", "Offline standalone test"},
                     {"report", "Write diagnostic JSON", "path"},
                     {"screenshot", "Save card screenshot", "path"},
                     {"test-seconds", "Exit after N seconds", "seconds"},
                     {"export-licenses", "Extract embedded notices", "directory"}});
  parser.process(app);
  if (parser.isSet("language") &&
      !QStringList{"system", "ru", "en"}.contains(parser.value("language")))
    return 2;
  if (parser.isSet("export-licenses"))
    return exportEmbeddedLicenses(parser.value("export-licenses")) ? 0 : 4;
  const bool offline = parser.isSet("demo") || parser.isSet("self-test");
  QTemporaryDir temporary;
  if (offline && !temporary.isValid())
    return 2;
  const QString directory =
      offline ? temporary.path()
              : QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                    .absolutePath();
  UiLanguage language(parser.isSet("language") ? parser.value("language")
                                               : savedUiLanguage(directory));
  if (!language.valid())
    return 3;
  ConnectionLease instance("ALLPOWERS.Widget.Instance.v1");
  if (!offline && !instance.acquire()) {
    QMessageBox::information(
        nullptr, QStringLiteral("ALLPOWERS Widget"),
        QCoreApplication::translate("Allpowers",
                                    "The widget is already running. Choose Quit in the old "
                                    "widget's menu near the clock, then open this file again."));
    return 0;
  }
  if (QGuiApplication::platformName() == "offscreen") {
    QDir fonts(qEnvironmentVariable("SystemRoot") + "/Fonts");
    QFontDatabase::addApplicationFont(fonts.filePath("segoeui.ttf"));
    QFontDatabase::addApplicationFont(fonts.filePath("segoeuib.ttf"));
  }
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
