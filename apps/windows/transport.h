#pragma once
#include "history.h"
#include <QObject>

// Application telemetry contract. USB and service clients never create BLE
// discovery objects; presentation and history do not depend on the transport.
class TelemetryTransport : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual bool active() const = 0;
  virtual bool connected() const = 0;
  virtual bool scanning() const = 0;
  virtual QString address() const = 0; // Station identity/history key.
  virtual QString endpoint() const {
    return address();
  }
  virtual QString phase() const = 0;
  virtual QJsonObject diagnostics() const = 0;
  virtual bool remote() const {
    return false;
  }
  virtual bool shared() const {
    return remote();
  }
  virtual bool usb() const {
    return false;
  }
  virtual bool available() const {
    return true;
  }
public slots:
  virtual void scan() = 0;
  virtual void start(const QString &, int) = 0;
  virtual void resume(const QString &, const QString &, int) = 0;
  virtual void stop() = 0;
  virtual void requestHistory() {}
signals:
  void found(QString endpoint, QString name, int rssi, int model);
  void stateChanged();
  void snapshot(ap_snapshot data);
  void packet(QString text, bool valid);
  void newSession();
  void historyReady(HistoryPoints points);
  void historyImported(HistoryPoints points);
  void logReceived(QString message);
};
