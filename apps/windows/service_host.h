#pragma once
#include "api.h"
#include <QJsonObject>
#include <QObject>
struct ServiceOptions {
  QString directory, pipeName;
  bool offline = false, publishDriver = true;
};
class ServiceHost : public QObject {
  Q_OBJECT
  struct Impl;
  Impl *impl;

public:
  explicit ServiceHost(const ServiceOptions &, QObject *parent = nullptr);
  ~ServiceHost() override;
  bool start();
  QJsonObject diagnostics() const;
  QString error() const;
  bool injectLabSnapshot(ap_snapshot data);
};
QString serviceDataDirectory();
int runWindowsService(int argc, char **argv);
