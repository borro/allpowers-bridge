#pragma once
#include "transport.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <functional>

// Independent of SQLite/history: a blocked history worker must not hide a failure.
class RuntimeLog {
public:
  explicit RuntimeLog(const QString &directory);
  ~RuntimeLog();
  RuntimeLog(const RuntimeLog &) = delete;
  RuntimeLog &operator=(const RuntimeLog &) = delete;
  static void record(const QString &event, QJsonObject fields = {});
  static QString path();
};

class TransportTrace : public QObject {
  TelemetryTransport *transport;
  std::function<QJsonObject()> extra;
  QTimer timer;
  QElapsedTimer clock;
  QJsonObject previousState;
  ap_snapshot last{};
  qint64 receivedAt = -1, heartbeatAt = 0;
  quint64 snapshots = 0;
  void state();
  QJsonObject context() const;

public:
  TransportTrace(TelemetryTransport *source, QObject *parent,
                 std::function<QJsonObject()> details = {});
};
