#pragma once
#include "api.h"
#include <QByteArray>
#include <QObject>

class Session {
  void *handle = nullptr;
  quint64 generation_ = 0;

public:
  Session();
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  quint64 reset(int model);
  quint64 generation() const {
    return generation_;
  }
  bool receive(quint64 generation, const QByteArray &data, quint32 now);
  ap_snapshot snapshot(quint32 now) const;
  QByteArray poll() const;
};
Q_DECLARE_METATYPE(ap_snapshot)
