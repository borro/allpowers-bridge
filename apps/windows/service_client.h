#pragma once
#include "bluetooth.h"
#include "history.h"
#include <QLocalSocket>
class ServiceTransport : public BleTransport {
  Q_OBJECT
  QLocalSocket socket;
  QTimer retry, historyTimer;
  QString pipeName;
  QByteArray buffer;
  QJsonObject info;
  HistoryPoints pendingHistory;
  HistoryPoints currentHistory, historyDelta;
  QString historyAddress;
  bool historyBusy = false;
  qint64 stateAt = 0;
  QElapsedTimer clock;
  bool verifyOwner = true;
  void connectPipe();
  void receive();
  void send(QJsonObject);

public:
  explicit ServiceTransport(QObject *parent = nullptr, QString pipe = {}, bool verifyOwner = true);
  ~ServiceTransport() override;
  bool active() const override {
    return info["active"].toBool();
  }
  bool connected() const override {
    return socket.state() == QLocalSocket::ConnectedState && info["connected"].toBool();
  }
  bool scanning() const override {
    return info["scanning"].toBool();
  }
  QString address() const override {
    return info["address"].toString();
  }
  QString phase() const override;
  QJsonObject diagnostics() const override;
  bool remote() const override {
    return true;
  }
  bool available() const override {
    return socket.state() == QLocalSocket::ConnectedState;
  }
  void scan() override;
  void start(const QString &, int) override;
  void resume(const QString &, const QString &, int) override;
  void stop() override;
};
