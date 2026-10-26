#include "i18n.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>

void populateUiLanguages(QComboBox &box, const QString &selection) {
  box.setObjectName("uiLanguage");
  box.setAccessibleName(QCoreApplication::translate("Allpowers", "Interface language"));
  box.addItem(QCoreApplication::translate("Allpowers", "Windows language"), "system");
  box.addItem(QString::fromUtf8("Русский"), "ru");
  box.addItem("English", "en");
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
