#pragma once
#include "connection_lease.h"
#include "transport.h"
#include <QCoreApplication>
#include <functional>
class TransportRouter : public TelemetryTransport {
  Q_OBJECT
public:
  using Factory = std::function<TelemetryTransport *(const QString &, QObject *)>;
  explicit TransportRouter(QObject *parent, bool offline, bool service, QString mode,
                           Factory factory = {},
                           QString leaseKey = QStringLiteral("ALLPOWERS.DirectConnection.v1"));
  ~TransportRouter() override;
  QString mode() const {
    return mode_;
  }
  void setMode(const QString &);
  bool active() const override {
    return current->active();
  }
  bool connected() const override {
    return current->connected();
  }
  bool scanning() const override {
    return current->scanning();
  }
  QString address() const override {
    return current->address();
  }
  QString endpoint() const override {
    return current->endpoint();
  }
  QString phase() const override {
    return blocked_ ? QCoreApplication::translate("Allpowers",
                                                  "Connection in use by another ALLPOWERS "
                                                  "application. Disconnect it and try again.")
                    : current->phase();
  }
  QJsonObject diagnostics() const override {
    auto info = current->diagnostics();
    for (const auto &key : {"discovery_attempts", "connection_attempts", "connections", "packets"})
      info[key] = priorActivity[key].toInteger() + info[key].toInteger();
    info["bluetooth_used"] =
        priorActivity["bluetooth_used"].toBool() || info["bluetooth_used"].toBool();
    if (blocked_)
      info["error"] = phase();
    return info;
  }
  bool remote() const override {
    return current->remote();
  }
  bool usb() const override {
    return current->usb();
  }
  bool available() const override {
    return current->available();
  }
  void scan() override {
    current->scan();
  }
  void start(const QString &id, int model) override {
    if (claim())
      current->start(id, model);
  }
  void resume(const QString &id, const QString &name, int model) override {
    if (claim())
      current->resume(id, name, model);
  }
  void stop() override {
    current->stop();
    lease.release();
    blocked_ = false;
    emit stateChanged();
  }
  void requestHistory() override {
    current->requestHistory();
  }

private:
  ConnectionLease lease;
  bool blocked_ = false;
  bool claim() {
    blocked_ = !current->remote() && !lease.acquire();
    if (blocked_)
      emit stateChanged();
    return !blocked_;
  }
  TelemetryTransport *current = nullptr;
  QString mode_;
  Factory factory;
  QJsonObject priorActivity;
};
