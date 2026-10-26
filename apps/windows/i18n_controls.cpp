#include "i18n.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPushButton>

void initializeUiFonts() {
  // The offscreen platform has no Windows font discovery. Use the same system
  // fonts as the native UI, including both Chinese families for the selector.
  static bool registered = false;
  if (!registered && QGuiApplication::platformName() == "offscreen") {
    const QDir fonts(qEnvironmentVariable("SystemRoot") + "/Fonts");
    for (const auto &name :
         {"segoeui.ttf", "segoeuib.ttf", "msyh.ttc", "msyhbd.ttc", "msjh.ttc", "msjhbd.ttc"}) {
      const auto path = fonts.filePath(name);
      if (QFileInfo::exists(path))
        QFontDatabase::addApplicationFont(path);
    }
    registered = true;
  }
  const auto language = QCoreApplication::instance()->property("ui_language").toString();
  QStringList families;
  if (language == "zh-TW")
    families = {"Microsoft JhengHei UI", "Microsoft JhengHei"};
  else if (language == "zh-CN" || QGuiApplication::platformName() == "offscreen")
    families = {"Microsoft YaHei UI", "Microsoft YaHei"};
  QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Han, families);
}

void populateUiLanguages(QComboBox &box, const QString &selection) {
  box.setObjectName("uiLanguage");
  box.setAccessibleName(QCoreApplication::translate("Allpowers", "Interface language"));
  box.addItem(QCoreApplication::translate("Allpowers", "Windows language"), "system");
  box.addItem(QString::fromUtf8("Русский"), "ru");
  box.addItem("English", "en");
  box.addItem(QString::fromUtf8("简体中文"), "zh-CN");
  box.addItem(QString::fromUtf8("繁體中文"), "zh-TW");
  box.setCurrentIndex(box.findData(normalizedUiLanguage(selection)));
}
void translateDialogButtons(QDialogButtonBox &box) {
  for (const auto &entry :
       {qMakePair(QDialogButtonBox::Save, QT_TRANSLATE_NOOP("Allpowers", "Save")),
        qMakePair(QDialogButtonBox::Cancel, QT_TRANSLATE_NOOP("Allpowers", "Cancel")),
        qMakePair(QDialogButtonBox::Close, QT_TRANSLATE_NOOP("Allpowers", "Close"))})
    if (auto *button = box.button(entry.first))
      button->setText(QCoreApplication::translate("Allpowers", entry.second));
}
