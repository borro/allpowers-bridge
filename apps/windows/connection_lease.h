#pragma once
#include <QString>

// Named kernel mutex for ownership/crash recovery, with a process-local guard to
// prevent Windows' recursive mutex acquisition by controllers on one thread.
class ConnectionLease {
  void *handle = nullptr;
  QString name;

public:
  explicit ConnectionLease(QString key = QStringLiteral("ALLPOWERS.DirectConnection.v1"));
  ~ConnectionLease();
  ConnectionLease(const ConnectionLease &) = delete;
  ConnectionLease &operator=(const ConnectionLease &) = delete;
  bool acquire();
  void release();
};
