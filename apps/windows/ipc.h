#pragma once
#include "history.h"
#include <QJsonObject>
#include <QLocalSocket>
#include <QWinEventNotifier>

namespace Ipc {
constexpr int Version = 1, MaxFrame = 65536, HistoryPage = 128;
QString endpoint();
QByteArray frame(const QJsonObject &);
// Returns false for oversized/malformed/non-v1 messages; incomplete frames wait.
bool consume(QByteArray &, QList<QJsonObject> &);
QJsonObject snapshot(const ap_snapshot &);
bool snapshot(const QJsonObject &, ap_snapshot &);
QJsonObject point(const HistoryPoint &);
bool point(const QJsonObject &, HistoryPoint &);
} // namespace Ipc

class LocalPipeServer : public QObject {
  Q_OBJECT
  struct Native;
  Native *native = nullptr;
  bool createListener(bool first);
  void accept();

public:
  explicit LocalPipeServer(QObject *parent = nullptr);
  ~LocalPipeServer() override;
  bool listen(const QString &name);
  QString error;
signals:
  void connected(QLocalSocket *socket);
};
