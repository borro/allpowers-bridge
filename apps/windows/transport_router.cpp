#include "transport_router.h"
#include "bluetooth.h"
#include "service_client.h"
#include "usb_transport.h"
TransportRouter::TransportRouter(QObject *parent, bool offline, bool service, QString mode,
                                 Factory make, QString leaseKey)
    : TelemetryTransport(parent), lease(std::move(leaseKey)), factory(std::move(make)) {
  if (!factory)
    factory = [offline, service](const QString &kind, QObject *owner) -> TelemetryTransport * {
      if (kind == "usb")
        return new UsbTransport(owner, offline);
      if (service && !offline)
        return new ServiceTransport(owner);
      return new BleTransport(owner, offline);
    };
  setMode(mode);
}
TransportRouter::~TransportRouter() {
  current->disconnect(this);
  // Destroying a service viewer releases IPC without stopping the shared UPS.
  delete current;
}
void TransportRouter::setMode(const QString &mode) {
  const QString next = mode == "usb" ? "usb" : "bluetooth";
  if (current && mode_ == next)
    return;
  if (current) {
    // Preserve actual activity when switching transports during a timed run.
    const auto activity = diagnostics();
    for (const auto &key :
         {"discovery_attempts", "connection_attempts", "connections", "packets", "bluetooth_used"})
      priorActivity[key] = activity[key];
    current->disconnect(this);
    if (!current->remote())
      current->stop();
    delete current;
    lease.release();
  }
  blocked_ = false;
  mode_ = next;
  current = factory(next, this);
  current->setParent(this);
  connect(current, &TelemetryTransport::found, this, &TelemetryTransport::found);
  connect(current, &TelemetryTransport::stateChanged, this, &TelemetryTransport::stateChanged);
  connect(current, &TelemetryTransport::snapshot, this, &TelemetryTransport::snapshot);
  connect(current, &TelemetryTransport::packet, this, &TelemetryTransport::packet);
  connect(current, &TelemetryTransport::newSession, this, &TelemetryTransport::newSession);
  connect(current, &TelemetryTransport::historyReady, this, &TelemetryTransport::historyReady);
  connect(current, &TelemetryTransport::historyImported, this,
          &TelemetryTransport::historyImported);
  connect(current, &TelemetryTransport::logReceived, this, &TelemetryTransport::logReceived);
  emit stateChanged();
}
