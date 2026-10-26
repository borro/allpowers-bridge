#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QStringList>
#include <QTranslator>

class QComboBox;
class QDialogButtonBox;

QString normalizedUiLanguage(const QString &value);
QStringList supportedUiLanguages();
QString resolvedUiLanguage(const QString &value, const QLocale &system = QLocale::system());
QString savedUiLanguage(const QString &directory);
class UiLanguage {
  QTranslator translator;
  UiLanguage *previous = nullptr;
  QString previousLanguage;

public:
  explicit UiLanguage(const QString &selection);
  ~UiLanguage();
  bool valid() const;
};
void populateUiLanguages(QComboBox &, const QString &selection);
void initializeUiFonts();
void translateDialogButtons(QDialogButtonBox &);
QString formatDuration(quint32 seconds, bool compactMinutes = false);
// Stable message IDs and typed arguments are additive to the legacy IPC text.
QJsonObject uiMessage(const QString &id, QJsonArray args = {});
QString uiMessageText(const QJsonObject &, const QString &fallback);
