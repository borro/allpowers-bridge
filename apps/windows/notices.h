#pragma once
#include <QString>
#include <QStringList>
class QWidget;
QStringList embeddedLicenseFiles();
bool exportEmbeddedLicenses(const QString &directory);
void showNotices(QWidget *parent);
