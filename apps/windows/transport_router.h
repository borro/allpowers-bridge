#pragma once
#include "transport.h"
#include <functional>
class TransportRouter : public TelemetryTransport {
  Q_OBJECT
public:
  using Factory = std::function<TelemetryTransport *(const QString &, QObject *)>;
  explicit TransportRouter(QObject *parent, bool offline, bool service, QString mode,
                           Factory factory = {});
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
    return current->phase();
  }
  QJsonObject diagnostics() const override {
    return current->diagnostics();
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
    current->start(id, model);
  }
  void resume(const QString &id, const QString &name, int model) override {
    current->resume(id, name, model);
  }
  void stop() override {
    current->stop();
  }
  void requestHistory() override {
    current->requestHistory();
  }

private:
  TelemetryTransport *current = nullptr;
  QString mode_;
  Factory factory;
};
