#include "notices.h"
#include "i18n.h"
#include "product-version.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <filesystem>

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
    // License texts can be recreated from resources. QSaveFile::commit() forces
    // a disk sync for every file (hundreds per export), which can stall a slow
    // Windows volume. Keep atomic replacement, without a durability barrier:
    // fully write/close a sibling temporary file, then rename over the target.
    const auto bytes = source.readAll();
    QString temporary;
    {
      QTemporaryFile output(path + ".XXXXXX");
      if (source.error() != QFileDevice::NoError || !output.open() ||
          output.write(bytes) != bytes.size() || !output.flush())
        return false;
      temporary = output.fileName();
      output.setAutoRemove(false);
      // QTemporaryFile::close() retains its native handle until destruction.
    }
    std::error_code error;
    std::filesystem::rename(QFile(temporary).filesystemFileName(), QFile(path).filesystemFileName(),
                            error);
    if (error) {
      QFile::remove(temporary);
      return false;
    }
  }
  return true;
}
void showNotices(QWidget *parent) {
  QDialog dialog(parent);
  dialog.setWindowTitle(QCoreApplication::translate("Allpowers", "About · ALLPOWERS Monitor"));
  dialog.resize(900, 620);
  auto *layout = new QVBoxLayout(&dialog);
  auto *title = new QLabel(
      QCoreApplication::translate(
          "Allpowers", "ALLPOWERS Monitor %1 · C++23\nUses Qt %2, static linking, LGPLv3.\nQt © "
                       "The Qt Company Ltd. and other contributors.\nSource code and Qt "
                       "replacement instructions are in the sources package.")
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
  translateDialogButtons(*buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
}
