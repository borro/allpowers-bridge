#pragma once
#include "TelemetryRecovery.h"
#include "i18n.h"
#include "transport.h"
#include <QBluetoothDeviceDiscoveryAgent>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <functional>

// Notification I/O is separate from session, freshness and recovery policy.
// Supplying a channel avoids creating any native Bluetooth objects.
class BleChannel : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void scan() = 0;
  virtual void connectToDevice(const QString &address, quint64 generation) = 0;
  virtual void close() = 0;
  virtual void write(const QByteArray &) = 0;
signals:
  void found(QString address, QString name, int rssi, int model);
  // Ready means the notification subscription is usable, not merely link-up.
  void ready(quint64 generation);
  void received(quint64 generation, QByteArray packet);
  void failed(quint64 generation, QString reason);
};

class BleTransport : public TelemetryTransport {
  Q_OBJECT
  QBluetoothDeviceDiscoveryAgent *scanner = nullptr;
  QPointer<QLowEnergyController> controller;
  QPointer<QLowEnergyService> service;
  QLowEnergyCharacteristic tx;
  QMap<QString, QBluetoothDeviceInfo> devices;
  Session session;
  QTimer tick, poll, retry, deadline;
  QElapsedTimer elapsed;
  BleChannel *channel_ = nullptr;
  std::function<qint64()> clock_;
  qint64 retryAt_ = -1, deadlineAt_ = -1, pollAt_ = -1;
  AllpowersCore::TelemetryRecovery recovery;
  QString address_, name_, phase_ = QCoreApplication::translate("Allpowers", "Disconnected"),
                           error_;
  QJsonObject phaseMessage_ = uiMessage("ble.disconnected"), errorMessage_;
  int profile = 0, retrySeconds = 2;
  bool active_ = false, connected_ = false, offline_ = false;
  quint64 epoch = 0;
  quint64 discoveries = 0, attempts = 0, connects = 0, drops = 0, polls = 0, packets = 0;
  void connectDevice();
  void discoveryEnded();
  void fail(const QString &message, QJsonObject descriptor = {});
  void destroyConnection();
  void readyService(quint64 generation);
  void sendPoll();
  void setPhase(const QString &phase, QJsonObject descriptor = {});
  qint64 nowMs() const { return clock_ ? clock_() : elapsed.elapsed(); }
  void acceptReady(quint64 generation);
  void acceptPacket(quint64 generation, quint64 token, const QByteArray &value);

public:
  explicit BleTransport(QObject *parent = nullptr, bool offline = false, bool snapshots = true,
                        BleChannel *channel = nullptr, std::function<qint64()> clock = {});
  ~BleTransport() override;
  virtual bool active() const {
    return active_;
  }
  virtual bool connected() const {
    return connected_;
  }
  virtual bool scanning() const {
    return scanner && scanner->isActive();
  }
  virtual QString address() const {
    return address_;
  }
  virtual QString phase() const {
    return phase_;
  }
  virtual QJsonObject diagnostics() const;
  virtual bool remote() const {
    return false;
  }
  virtual bool available() const {
    return true;
  }
public slots:
  void refresh();
  virtual void scan();
  virtual void start(const QString &address, int model);
  virtual void resume(const QString &address, const QString &name, int model);
  virtual void stop();
};
