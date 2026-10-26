#include "i18n.h"
#include "ipc.h"
#include "service_client.h"
#include "widget.h"
#include "window.h"
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLocalServer>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QXmlStreamReader>

class LanguageTransport : public TelemetryTransport {
public:
  using TelemetryTransport::TelemetryTransport;
  int starts = 0, stops = 0;
  bool active() const override {
    return starts > stops;
  }
  bool connected() const override {
    return active();
  }
  bool scanning() const override {
    return false;
  }
  bool remote() const override {
    return true;
  }
  QString address() const override {
    return {};
  }
  QString phase() const override {
    return "synthetic";
  }
  QJsonObject diagnostics() const override {
    return {};
  }
  void scan() override {}
  void start(const QString &, int) override {
    ++starts;
  }
  void resume(const QString &, const QString &, int) override {
    ++starts;
  }
  void stop() override {
    ++stops;
  }
};

class LanguageTests : public QObject {
  Q_OBJECT
  static void writeSettings(const QString &directory, QJsonObject settings) {
    QDir().mkpath(directory);
    QFile file(directory + "/settings.json");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(settings).toJson());
  }
private slots:
  void initTestCase() {
    QApplication::setStyle("Fusion");
    QDir fonts(qEnvironmentVariable("SystemRoot") + "/Fonts");
    for (const auto &name : {"segoeui.ttf", "segoeuib.ttf"})
      QFontDatabase::addApplicationFont(fonts.filePath(name));
  }
  void localeResolution() {
    QCOMPARE(resolvedUiLanguage("system", QLocale("ru_RU")), "ru");
    QCOMPARE(resolvedUiLanguage("system", QLocale("ru_KZ")), "ru");
    QCOMPARE(resolvedUiLanguage("system", QLocale("en_US")), "en");
    QCOMPARE(resolvedUiLanguage("system", QLocale("de_DE")), "en");
    QCOMPARE(resolvedUiLanguage("ru", QLocale("en_US")), "ru");
    QCOMPARE(resolvedUiLanguage("en", QLocale("ru_RU")), "en");
    QCOMPARE(normalizedUiLanguage("invalid"), "system");
    QCOMPARE(normalizedUiLanguage(""), "system");
    QCOMPARE(resolvedUiLanguage("invalid", QLocale("ru_RU")), "ru");
  }
  void independentPreferences() {
    QTemporaryDir folder;
    const auto monitor = folder.filePath("monitor"), widget = folder.filePath("widget");
    QCOMPARE(savedUiLanguage(monitor), "system");
    writeSettings(monitor, {{"ui_language", "en"}, {"address", "synthetic"}});
    writeSettings(widget, {{"ui_language", "ru"}});
    QCOMPARE(savedUiLanguage(monitor), "en");
    QCOMPARE(savedUiLanguage(widget), "ru");
    writeSettings(widget, {{"ui_language", "invalid"}});
    QCOMPARE(savedUiLanguage(widget), "system");
    writeSettings(widget, {{"profile", 2}});
    QCOMPARE(savedUiLanguage(widget), "system");
  }
  void catalogsAndArguments() {
    QFile file(TRANSLATION_SOURCE);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QXmlStreamReader xml(&file);
    UiLanguage language("ru");
    QVERIFY(language.valid());
    int count = 0;
    QString source;
    while (!xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement())
        continue;
      if (xml.name() == u"source")
        source = xml.readElementText();
      else if (xml.name() == u"translation") {
        QVERIFY(xml.attributes().value("type") != u"unfinished");
        const auto translated = xml.readElementText();
        QVERIFY2(!translated.isEmpty(), qPrintable(source));
        QCOMPARE(QCoreApplication::translate("Allpowers", source.toUtf8().constData()), translated);
        for (int i = 1; i <= 9; ++i)
          QCOMPARE(translated.count("%" + QString::number(i)),
                   source.count("%" + QString::number(i)));
        ++count;
      }
    }
    QVERIFY(!xml.hasError());
    QVERIFY(count > 250);
    QCOMPARE(formatDuration(90061), QString::fromUtf8("1 д 1 ч"));
    QCOMPARE(formatDuration(61, true), QString::fromUtf8("1 мин"));
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel |
                             QDialogButtonBox::Close);
    translateDialogButtons(buttons);
    QCOMPARE(buttons.button(QDialogButtonBox::Cancel)->text(), QString::fromUtf8("Отмена"));
    {
      UiLanguage english("en");
      QVERIFY(english.valid());
      QCOMPARE(formatDuration(90061), "1 d 1 h");
      QCOMPARE(QCoreApplication::translate("Allpowers", "Disconnected"), "Disconnected");
    }
    QCOMPARE(QCoreApplication::translate("Allpowers", "Disconnected"),
             QString::fromUtf8("Отключено"));
  }
  void serviceMessages() {
    UiLanguage language("ru");
    const auto retry = uiMessage("ble.retry", {uiMessage("ble.gatt", {42}), 4});
    QCOMPARE(uiMessageText(retry, "fallback"),
             QString::fromUtf8("Ошибка GATT: 42 · повтор через 4 с"));
    QCOMPARE(uiMessageText(uiMessage("unknown"), "legacy"), "legacy");
    QCOMPARE(uiMessageText(uiMessage("ble.gatt", {}), "raw error"), "raw error");
    QLocalServer server;
    const auto name = "Allpowers.LanguageTest." + QUuid::createUuid().toString(QUuid::Id128);
    QVERIFY(server.listen(name));
    ServiceTransport client(nullptr, name, false);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *socket = server.nextPendingConnection();
    auto publish = [&](QJsonObject diagnostics) {
      ap_snapshot snapshot{};
      snapshot.abi_version = 1;
      socket->write(Ipc::frame({{"v", Ipc::Version},
                                {"op", "state"},
                                {"diagnostics", diagnostics},
                                {"data", Ipc::snapshot(snapshot)}}));
      socket->flush();
    };
    publish({{"phase", "Connecting…"},
             {"connected", true},
             {"driver_error", "Driver unavailable (5)"},
             {"ui_messages", QJsonObject{{"phase", uiMessage("ble.connecting")},
                                         {"driver_error", uiMessage("driver.unavailable", {5})}}}});
    QTRY_COMPARE(client.phase(), QString::fromUtf8("Подключение…"));
    QCOMPARE(client.diagnostics()["driver_error"].toString(),
             QString::fromUtf8("Драйвер недоступен (5)"));
    publish({{"phase", "Legacy status"}, {"error", "OS native error"}});
    QTRY_COMPARE(client.phase(), "Legacy status");
    QCOMPARE(client.diagnostics()["error"].toString(), "OS native error");
  }
  void monitorPersistenceAndLegacyPage() {
    QTemporaryDir folder;
    const auto previous = qgetenv("ALLPOWERS_DATA_DIR");
    const auto restore = qScopeGuard([&] { qputenv("ALLPOWERS_DATA_DIR", previous); });
    qputenv("ALLPOWERS_DATA_DIR", folder.path().toUtf8());
    writeSettings(folder.path(), {{"page", QString::fromUtf8("История")}, {"ui_language", "ru"}});
    {
      UiLanguage language(savedUiLanguage(folder.path()));
      MonitorWindow window(true);
      QCOMPARE(window.findChild<QStackedWidget *>()->currentIndex(), 1);
      auto *choice = window.findChild<QComboBox *>("uiLanguage");
      QVERIFY(choice);
      QTimer::singleShot(0, [] {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
          message->accept();
      });
      choice->setCurrentIndex(choice->findData("en"));
      QCOMPARE(qApp->property("ui_language").toString(), "ru");
      QTRY_COMPARE(savedUiLanguage(folder.path()), "en");
    }
    UiLanguage language(savedUiLanguage(folder.path()));
    MonitorWindow reopened(true);
    QCOMPARE(reopened.findChild<QStackedWidget *>()->currentIndex(), 1);
    QCOMPARE(qApp->property("ui_language").toString(), "en");
    QFile file(folder.filePath("settings.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll())["page"].toString(), "history");
  }
  void widgetLanguageWithoutDevice() {
    QTemporaryDir folder;
    UiLanguage language("en");
    LanguageTransport *transport = nullptr;
    WidgetController widget(folder.path(), false, false, [&](const QString &, QObject *parent) {
      auto *created = new LanguageTransport(parent);
      if (!transport)
        transport = created;
      return created;
    });
    widget.start();
    QVERIFY(transport);
    const int starts = transport->starts, stops = transport->stops;
    auto configure = [&](bool accept) {
      QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto *choice = dialog->findChild<QComboBox *>("uiLanguage");
        QVERIFY(choice);
        choice->setCurrentIndex(choice->findData(accept ? "ru" : "en"));
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        buttons->button(accept ? QDialogButtonBox::Save : QDialogButtonBox::Cancel)->click();
      });
      QVERIFY(QMetaObject::invokeMethod(&widget, "configure", Qt::DirectConnection));
    };
    configure(true);
    QCOMPARE(savedUiLanguage(folder.path()), "ru");
    QCOMPARE(qApp->property("ui_language").toString(), "en");
    QCOMPARE(transport->starts, starts);
    QCOMPARE(transport->stops, stops);
    configure(false);
    QCOMPARE(savedUiLanguage(folder.path()), "ru");
  }
  void renderLayouts() {
    const auto output = qEnvironmentVariable("ALLPOWERS_LANGUAGE_SCREENSHOTS");
    if (output.isEmpty())
      QSKIP("Set ALLPOWERS_LANGUAGE_SCREENSHOTS to capture layouts");
    QDir().mkpath(output);
    QTemporaryDir folder;
    qputenv("ALLPOWERS_DATA_DIR", folder.path().toUtf8());
    for (const QString &code : {QString("ru"), QString("en")}) {
      UiLanguage language(code);
      QVERIFY(language.valid());
      MonitorWindow window(true);
      window.loadDemo();
      window.show();
      for (int page = 0; page < 3; ++page) {
        window.findChild<QButtonGroup *>()->button(page)->click();
        QTest::qWait(100);
        QVERIFY(
            window.grab().save(output + "/monitor-" + code + "-" + QString::number(page) + ".png"));
      }
      WidgetController widget(folder.filePath(code), true, false);
      widget.loadDemo();
      QVERIFY(widget.card()->renderedImage().save(output + "/widget-" + code + ".png"));
      QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QVERIFY(dialog->grab().save(output + "/settings-" + code + ".png"));
        dialog->reject();
      });
      QVERIFY(QMetaObject::invokeMethod(&widget, "configure", Qt::DirectConnection));
    }
  }
};
QTEST_MAIN(LanguageTests)
#include "i18n_test.moc"
