#pragma once
#include "session.h"
#include <QBluetoothDeviceDiscoveryAgent>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QMap>
#include <QPointer>
#include <QTimer>

class BleTransport : public QObject {
  Q_OBJECT
  QBluetoothDeviceDiscoveryAgent *scanner = nullptr;
  QPointer<QLowEnergyController> controller;
  QPointer<QLowEnergyService> service;
  QLowEnergyCharacteristic tx;
  QMap<QString, QBluetoothDeviceInfo> devices;
  Session session;
  QTimer tick, poll, retry, deadline;
  QElapsedTimer elapsed, lastValid;
  QString address_, name_, phase_ = "Отключено", error_;
  int profile = 0, retrySeconds = 2;
  bool active_ = false, connected_ = false, offline_ = false;
  quint64 epoch = 0;
  quint64 connects = 0, drops = 0, polls = 0, packets = 0;
  void connectDevice();
  void discoveryEnded();
  void fail(const QString &message);
  void destroyConnection();
  void readyService(quint64 generation);
  void sendPoll();
  void setPhase(const QString &phase);

public:
  explicit BleTransport(QObject *parent = nullptr, bool offline = false);
  ~BleTransport() override;
  bool active() const {
    return active_;
  }
  bool connected() const {
    return connected_;
  }
  bool scanning() const {
    return scanner && scanner->isActive();
  }
  QString address() const {
    return address_;
  }
  QString phase() const {
    return phase_;
  }
  QJsonObject diagnostics() const;
public slots:
  void scan();
  void start(const QString &address, int model);
  void resume(const QString &address, const QString &name, int model);
  void stop();
signals:
  void found(QString address, QString name, int rssi, int model);
  void stateChanged();
  void snapshot(ap_snapshot data);
  void packet(QString hex, bool valid);
  void newSession();
};
