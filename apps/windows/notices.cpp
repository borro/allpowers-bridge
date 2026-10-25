#include "notices.h"
#include "product-version.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSaveFile>
#include <QTextBrowser>
#include <QVBoxLayout>

QStringList embeddedLicenseFiles() {
  QStringList files;
  QDirIterator it(":/licenses", QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext())
    files.append(it.next().mid(QString(":/licenses/").size()));
  files.sort();
  return files;
}
bool exportEmbeddedLicenses(const QString &directory) {
  const auto files = embeddedLicenseFiles();
  if (files.isEmpty())
    return false;
  for (const auto &name : files) {
    QFile source(":/licenses/" + name);
    auto path = QDir(directory).filePath(name);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !source.open(QIODevice::ReadOnly))
      return false;
    QSaveFile output(path);
    const auto bytes = source.readAll();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
      return false;
  }
  return true;
}
void showNotices(QWidget *parent) {
  QDialog dialog(parent);
  dialog.setWindowTitle("О программе · ALLPOWERS Monitor");
  dialog.resize(900, 620);
  auto *layout = new QVBoxLayout(&dialog);
  auto *title = new QLabel(QString("ALLPOWERS Monitor %1 · C++23\n"
                                   "Использует Qt %2, статическая линковка, LGPLv3.\n"
                                   "Qt © The Qt Company Ltd. and other contributors.\n"
                                   "Исходники и инструкции замены Qt доступны в пакете sources.")
                               .arg(APP_VERSION, qVersion()));
  title->setWordWrap(true);
  layout->addWidget(title);
  auto *row = new QHBoxLayout;
  auto *list = new QListWidget;
  auto *text = new QTextBrowser;
  list->addItems(embeddedLicenseFiles());
  row->addWidget(list, 1);
  row->addWidget(text, 2);
  layout->addLayout(row, 1);
  QObject::connect(list, &QListWidget::currentTextChanged, &dialog, [text](const QString &name) {
    QFile file(":/licenses/" + name);
    if (file.open(QIODevice::ReadOnly))
      text->setPlainText(QString::fromUtf8(file.readAll()));
  });
  auto matches = list->findItems("THIRD_PARTY_NOTICES.md", Qt::MatchExactly);
  if (!matches.isEmpty())
    list->setCurrentItem(matches.first());
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
}
