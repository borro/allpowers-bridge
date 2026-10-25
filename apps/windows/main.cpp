#include "monitor_application.h"
#include "integration.h"
#include "service_host.h"
#include <QApplication>
#include <QDebug>

int main(int argc, char **argv) {
  // A service never constructs QApplication or loads the windowing platform.
  for (int i = 1; i < argc; ++i) {
    if (QByteArray(argv[i]) == "--service")
      return runWindowsService(argc, argv);
    if ((QByteArray(argv[i]) == "--integration-action" ||
         QByteArray(argv[i]) == "--extract-driver") &&
        i + 1 < argc) {
      const bool extract = QByteArray(argv[i]) == "--extract-driver";
      const QString operation = QString::fromLocal8Bit(argv[i + 1]);
      QCoreApplication app(argc, argv);
      const auto error = extract ? extractDriverPackage(operation) : integrationAction(operation);
      if (!error.isEmpty())
        qCritical().noquote() << error;
      return error.isEmpty() ? 0 : 5;
    }
  }
  QApplication app(argc, argv);
  return runMonitorApplication(app,app.arguments());
}
