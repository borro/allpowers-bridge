#pragma once
#include "history_operation_id.h"
#include "usb_serial.h"
#include <HidTelemetryWire.h>
#include <QJsonObject>
#include <memory>

QList<UsbPort> allpowersHidPorts();
UsbChannel *nativeHidChannel(QObject *parent);

// Device-independent session: the Windows worker supplies reports, clock and
// output. Tests exercise the same framing/retry/archive code without USB.
class HidSession {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  struct Callbacks {
    std::function<void(QByteArray)> output, received;
    std::function<void(QString)> failed;
    HistoryOperationIds::Entropy entropy = HistoryOperationIds::random128;
    std::function<qint64()> utcNow;
  };
  explicit HidSession(Callbacks callbacks);
  ~HidSession();
  bool start(const QByteArray &capabilities, qint64 now);
  void live(const QByteArray &report);
  void input(const QByteArray &report, qint64 now);
  void command(const QByteArray &command, qint64 now);
  void tick(qint64 now);
  void stop();
};
