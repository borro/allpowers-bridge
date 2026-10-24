#pragma once
#include <QJsonObject>
#include <QString>
bool integrationInstalled();
bool isServicePipe(qintptr handle);
QJsonObject integrationStatus();
QString integrationAction(const QString &operation);
bool elevateIntegrationAction(const QString &operation);
QString extractDriverPackage(const QString &directory);
