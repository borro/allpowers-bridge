#include "i18n.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QVariant>

QStringList supportedUiLanguages() {
  return {"system", "ru", "en", "zh-CN", "zh-TW"};
}
QString normalizedUiLanguage(const QString &value) {
  return supportedUiLanguages().contains(value) ? value : QString("system");
}
QString resolvedUiLanguage(const QString &value, const QLocale &system) {
  const auto normalized = normalizedUiLanguage(value);
  if (normalized != "system")
    return normalized;
  if (system.language() == QLocale::Russian)
    return "ru";
  if (system.language() == QLocale::Chinese)
    return system.script() == QLocale::TraditionalHanScript ? "zh-TW" : "zh-CN";
  return "en";
}
QString savedUiLanguage(const QString &directory) {
  QFile file(QDir(directory).filePath("settings.json"));
  if (!file.open(QIODevice::ReadOnly))
    return "system";
  return normalizedUiLanguage(
      QJsonDocument::fromJson(file.readAll()).object()["ui_language"].toString());
}
namespace {
UiLanguage *activeLanguage = nullptr;
}
UiLanguage::UiLanguage(const QString &selection) {
  previous = activeLanguage;
  previousLanguage = QCoreApplication::instance()->property("ui_language").toString();
  if (previous)
    QCoreApplication::removeTranslator(&previous->translator);
  activeLanguage = this;
  const auto language = resolvedUiLanguage(selection);
  auto catalog = language;
  catalog.replace('-', '_');
  if (language != "en" && translator.load(":/i18n/allpowers_" + catalog + ".qm"))
    QCoreApplication::installTranslator(&translator);
  QCoreApplication::instance()->setProperty("ui_language", language);
}
UiLanguage::~UiLanguage() {
  QCoreApplication::removeTranslator(&translator);
  activeLanguage = previous;
  if (previous && !previous->translator.isEmpty())
    QCoreApplication::installTranslator(&previous->translator);
  QCoreApplication::instance()->setProperty("ui_language", previousLanguage);
}
bool UiLanguage::valid() const {
  return QCoreApplication::instance()->property("ui_language").toString() == "en" ||
         !translator.isEmpty();
}
QString formatDuration(quint32 seconds, bool compactMinutes) {
  const auto minutes = seconds / 60, days = minutes / 1440, hours = minutes / 60 % 24;
  if (days)
    return QCoreApplication::translate("Allpowers", "%1 d %2 h").arg(days).arg(hours);
  if (compactMinutes && !hours)
    return QCoreApplication::translate("Allpowers", "%1 min").arg(minutes);
  return QCoreApplication::translate("Allpowers", "%1 h %2 min").arg(hours).arg(minutes % 60);
}
QJsonObject uiMessage(const QString &id, QJsonArray args) {
  return {{"id", id}, {"args", args}};
}
QString uiMessageText(const QJsonObject &message, const QString &fallback) {
  struct Entry {
    const char *id;
    const char *source;
    int count;
  };
  static const Entry entries[] = {
      {"ble.disconnected", QT_TRANSLATE_NOOP("Allpowers", "Disconnected"), 0},
      {"ble.offline", QT_TRANSLATE_NOOP("Allpowers", "Offline test: Bluetooth is not used"), 0},
      {"ble.scan_first", QT_TRANSLATE_NOOP("Allpowers", "Scan for a station first"), 0},
      {"ble.unavailable", QT_TRANSLATE_NOOP("Allpowers", "Station unavailable · scan in %1 s"), 1},
      {"ble.connecting", QT_TRANSLATE_NOOP("Allpowers", "Connecting…"), 0},
      {"ble.finding", QT_TRANSLATE_NOOP("Allpowers", "Finding the last station…"), 0},
      {"ble.verifying", QT_TRANSLATE_NOOP("Allpowers", "Verifying station…"), 0},
      {"ble.preparing", QT_TRANSLATE_NOOP("Allpowers", "Preparing telemetry…"), 0},
      {"ble.connected", QT_TRANSLATE_NOOP("Allpowers", "Connected · receiving data"), 0},
      {"ble.retry", QT_TRANSLATE_NOOP("Allpowers", "%1 · retry in %2 s"), 2},
      {"ble.timeout", QT_TRANSLATE_NOOP("Allpowers", "BLE timed out"), 0},
      {"ble.lost", QT_TRANSLATE_NOOP("Allpowers", "Connection lost"), 0},
      {"ble.no_telemetry",
       QT_TRANSLATE_NOOP("Allpowers", "Device does not provide ALLPOWERS telemetry"), 0},
      {"ble.gatt", QT_TRANSLATE_NOOP("Allpowers", "GATT error: %1"), 1},
      {"ble.no_notify", QT_TRANSLATE_NOOP("Allpowers", "Notification channel unavailable"), 0},
      {"ble.no_request", QT_TRANSLATE_NOOP("Allpowers", "Telemetry request channel unavailable"),
       0},
      {"ble.no_cccd", QT_TRANSLATE_NOOP("Allpowers", "CCCD subscription descriptor missing"), 0},
      {"ble.no_channel", QT_TRANSLATE_NOOP("Allpowers", "Request channel missing"), 0},
      {"ble.incomplete", QT_TRANSLATE_NOOP("Allpowers", "Full telemetry is not arriving"), 0},
      {"driver.unavailable", QT_TRANSLATE_NOOP("Allpowers", "Driver unavailable (%1)"), 1},
      {"driver.update_failed", QT_TRANSLATE_NOOP("Allpowers", "Driver update failed (%1)"), 1},
      {"ups.no_fresh_data",
       QT_TRANSLATE_NOOP("Allpowers", "No fresh confirmed charge and power source"), 0},
      {"service.invalid_selection", QT_TRANSLATE_NOOP("Allpowers", "Invalid station selection"), 0},
      {"service.save_failed", QT_TRANSLATE_NOOP("Allpowers", "Could not save station selection"),
       0},
      {"service.unknown_operation", QT_TRANSLATE_NOOP("Allpowers", "Unknown operation"), 0}};
  const auto id = message["id"].toString();
  const auto args = message["args"].toArray();
  for (const auto &entry : entries) {
    if (id != entry.id)
      continue;
    if (args.size() != entry.count)
      return fallback;
    QStringList values;
    for (const auto &arg : args) {
      if (arg.isObject() && id == "ble.retry") {
        const auto nested = arg.toObject();
        values.append(uiMessageText(nested, nested["fallback"].toString()));
      } else if (arg.isString())
        values.append(arg.toString());
      else if (arg.isDouble())
        values.append(QString::number(arg.toDouble(), 'g', 16));
      else
        return fallback;
    }
    const auto pattern = QCoreApplication::translate("Allpowers", entry.source);
    if (values.isEmpty())
      return pattern;
    return values.size() == 1 ? pattern.arg(values[0]) : pattern.arg(values[0], values[1]);
  }
  return fallback;
}
