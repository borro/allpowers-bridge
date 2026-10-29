#pragma once
#include "transport.h"
#include <QStringList>
#include <functional>
class QApplication;

// Platform dependencies of the real CLI/UI entry. Empty functions select the
// normal native implementations; injected transports own all their I/O.
struct MonitorRuntime {
  std::function<bool()> serviceInstalled;
  std::function<TelemetryTransport *(QObject *, bool offline)> makeTransport;
  QString leaseKey = QStringLiteral("ALLPOWERS.DirectConnection.v1");
};
int runMonitorApplication(QApplication &, const QStringList &arguments,
                          const MonitorRuntime &runtime = {});
