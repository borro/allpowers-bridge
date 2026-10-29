#include "bluetooth.h"
#include <QBluetoothUuid>
#include <QCoreApplication>

BleTransport::BleTransport(QObject *parent, bool offline, bool snapshots, BleChannel *channel,
                           std::function<qint64()> clock)
    : TelemetryTransport(parent), channel_(channel), clock_(std::move(clock)), offline_(offline) {
  elapsed.start();
  if (channel_) {
    channel_->setParent(this);
    connect(channel_, &BleChannel::found, this, &TelemetryTransport::found);
    connect(channel_, &BleChannel::ready, this, &BleTransport::acceptReady);
    connect(channel_, &BleChannel::received, this, [this](quint64 generation, QByteArray value) {
      acceptPacket(generation, session.generation(), value);
    });
    connect(channel_, &BleChannel::failed, this, [this](quint64 generation, QString reason) {
      if (active_ && generation == epoch)
        fail(reason);
    });
  }
  if (!offline && !channel_) {
    scanner = new QBluetoothDeviceDiscoveryAgent(this);
    scanner->setLowEnergyDiscoveryTimeout(7000);
    auto device = [this](const QBluetoothDeviceInfo &info) {
      if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration))
        return;
      QString address = info.address().toString();
      if (address.isEmpty())
        return;
      devices[address] = info;
      auto bytes = info.name().toUtf8();
      emit found(address,
                 info.name().isEmpty() ? QCoreApplication::translate("Allpowers", "Unnamed")
                                       : info.name(),
                 info.rssi(), int(ap_model_hint(bytes.constData())));
      if (active_ && address == address_ && !controller)
        connectDevice();
    };
    connect(scanner, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, device);
    connect(scanner, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
            [device](const auto &i, auto) { device(i); });
    connect(scanner, &QBluetoothDeviceDiscoveryAgent::finished, this,
            &BleTransport::discoveryEnded);
    connect(scanner, &QBluetoothDeviceDiscoveryAgent::canceled, this,
            &BleTransport::discoveryEnded);
    connect(scanner, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [this](auto) {
      error_ = scanner->errorString();
      errorMessage_ = {};
      discoveryEnded();
    });
  }
  tick.setInterval(500);
  connect(&tick, &QTimer::timeout, this, &BleTransport::refresh);
  if (snapshots && !clock_)
    tick.start();
  poll.setInterval(int(ap_poll_interval_ms()));
  connect(&poll, &QTimer::timeout, this, &BleTransport::sendPoll);
  retry.setSingleShot(true);
  connect(&retry, &QTimer::timeout, this, &BleTransport::connectDevice);
  deadline.setSingleShot(true);
  connect(&deadline, &QTimer::timeout, this, [this] {
    fail(QCoreApplication::translate("Allpowers", "BLE timed out"), uiMessage("ble.timeout"));
  });
}
BleTransport::~BleTransport() {
  stop();
}
void BleTransport::setPhase(const QString &phase, QJsonObject descriptor) {
  phase_ = phase;
  phaseMessage_ = std::move(descriptor);
  emit stateChanged();
}
void BleTransport::scan() {
  if (channel_ && !offline_) {
    ++discoveries;
    channel_->scan();
    emit stateChanged();
    return;
  }
  if (!scanner) {
    error_ = QCoreApplication::translate("Allpowers", "Offline test: Bluetooth is not used");
    errorMessage_ = uiMessage("ble.offline");
    emit stateChanged();
    return;
  }
  if (scanner->isActive())
    return;
  auto current = devices.value(address_);
  devices.clear();
  if (active_ && current.isValid())
    devices[address_] = current;
  error_.clear();
  errorMessage_ = {};
  ++discoveries;
  scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
  emit stateChanged();
}
void BleTransport::destroyConnection() {
  deadline.stop();
  poll.stop();
  deadlineAt_ = pollAt_ = -1;
  if (channel_)
    channel_->close();
  connected_ = false;
  if (service) {
    service->disconnect(this);
    service->deleteLater();
    service.clear();
  }
  if (controller) {
    controller->disconnect(this);
    controller->disconnectFromDevice();
    controller->deleteLater();
    controller.clear();
  }
  tx = QLowEnergyCharacteristic();
}
void BleTransport::start(const QString &address, int model) {
  stop();
  if (!channel_ && !devices.contains(address)) {
    error_ = QCoreApplication::translate("Allpowers", "Scan for a station first");
    errorMessage_ = uiMessage("ble.scan_first");
    emit stateChanged();
    return;
  }
  address_ = address;
  name_ = channel_ ? QString() : devices[address].name();
  profile = model;
  active_ = true;
  retrySeconds = 2;
  connectDevice();
}
void BleTransport::resume(const QString &address, const QString &name, int model) {
  stop();
  const QBluetoothAddress saved(address);
  if (saved.isNull())
    return;
  address_ = saved.toString();
  name_ = name;
  profile = qBound(0, model, 3);
  active_ = true;
  retrySeconds = 2;
  connectDevice();
}
void BleTransport::discoveryEnded() {
  if (active_ && !controller && !retry.isActive()) {
    setPhase(QCoreApplication::translate("Allpowers", "Station unavailable · scan in %1 s")
                 .arg(retrySeconds),
             uiMessage("ble.unavailable", {retrySeconds}));
    retryAt_ = nowMs() + retrySeconds * 1000;
    if (!clock_)
      retry.start(retrySeconds * 1000);
    retrySeconds = qMin(20, retrySeconds * 2);
  } else {
    emit stateChanged();
  }
}
void BleTransport::stop() {
  active_ = false;
  ++epoch;
  retry.stop();
  retryAt_ = -1;
  if (scanner && scanner->isActive())
    scanner->stop();
  destroyConnection();
  session.reset(profile);
  setPhase(QCoreApplication::translate("Allpowers", "Disconnected"), uiMessage("ble.disconnected"));
  emit snapshot(session.snapshot(quint32(nowMs())));
}
void BleTransport::fail(const QString &message, QJsonObject descriptor) {
  if (!active_)
    return;
  error_ = message;
  errorMessage_ = std::move(descriptor);
  drops++;
  ++epoch;
  destroyConnection();
  auto errorDescriptor = errorMessage_;
  errorDescriptor["fallback"] = message;
  setPhase(QCoreApplication::translate("Allpowers", "%1 · retry in %2 s")
               .arg(message, QString::number(retrySeconds)),
           uiMessage("ble.retry", {errorDescriptor, retrySeconds}));
  retryAt_ = nowMs() + retrySeconds * 1000;
  if (!clock_)
    retry.start(retrySeconds * 1000);
  retrySeconds = qMin(20, retrySeconds * 2);
}
void BleTransport::connectDevice() {
  if (!active_)
    return;
  // Offline transports are also injected into UI tests. Never create native
  // discovery/controller objects in this mode, even for explicit requests.
  if (offline_) {
    setPhase(QCoreApplication::translate("Allpowers", "Offline test: Bluetooth is not used"),
             uiMessage("ble.offline"));
    return;
  }
  if (channel_) {
    retry.stop();
    retryAt_ = -1;
    destroyConnection();
    ++epoch;
    session.reset(profile ? profile : int(ap_model_hint(name_.toUtf8().constData())));
    emit newSession();
    deadlineAt_ = nowMs() + 30000;
    ++attempts;
    setPhase(QCoreApplication::translate("Allpowers", "Connecting…"), uiMessage("ble.connecting"));
    channel_->connectToDevice(address_, epoch);
    return;
  }
  const auto info = devices.value(address_);
  if (!info.isValid()) {
    setPhase(QCoreApplication::translate("Allpowers", "Finding the last station…"),
             uiMessage("ble.finding"));
    scan();
    return;
  }
  retry.stop();
  retryAt_ = -1;
  destroyConnection();
  ++epoch;
  const auto generation = epoch;
  name_ = info.name();
  auto name = info.name().toUtf8();
  session.reset(profile ? profile : int(ap_model_hint(name.constData())));
  emit newSession();
  auto *c = QLowEnergyController::createCentral(info, this);
  controller = c;
  if (scanner && scanner->isActive())
    scanner->stop();
  auto current = [this, c, generation] {
    return active_ && controller == c && epoch == generation;
  };
  connect(c, &QLowEnergyController::connected, this, [this, c, current] {
    if (current()) {
      setPhase(QCoreApplication::translate("Allpowers", "Verifying station…"),
               uiMessage("ble.verifying"));
      c->discoverServices();
    }
  });
  connect(c, &QLowEnergyController::errorOccurred, this, [this, c, current](auto) {
    if (current())
      fail(c->errorString());
  });
  connect(c, &QLowEnergyController::disconnected, this, [this, current] {
    if (current())
      fail(QCoreApplication::translate("Allpowers", "Connection lost"), uiMessage("ble.lost"));
  });
  connect(c, &QLowEnergyController::discoveryFinished, this, [this, c, current, generation] {
    if (!current())
      return;
    service = c->createServiceObject(QBluetoothUuid(QString::fromLatin1(ap_uuid(0))), this);
    if (!service) {
      fail(QCoreApplication::translate("Allpowers", "Device does not provide ALLPOWERS telemetry"),
           uiMessage("ble.no_telemetry"));
      return;
    }
    auto *s = service.data();
    connect(s, &QLowEnergyService::errorOccurred, this, [this, s, current](auto error) {
      if (current() && service == s)
        fail(QCoreApplication::translate("Allpowers", "GATT error: %1").arg(int(error)),
             uiMessage("ble.gatt", {int(error)}));
    });
    connect(s, &QLowEnergyService::stateChanged, this, [this, s, current, generation](auto state) {
      if (current() && service == s && state == QLowEnergyService::RemoteServiceDiscovered)
        readyService(generation);
    });
    setPhase(QCoreApplication::translate("Allpowers", "Preparing telemetry…"),
             uiMessage("ble.preparing"));
    s->discoverDetails(QLowEnergyService::FullDiscovery);
  });
  setPhase(QCoreApplication::translate("Allpowers", "Connecting…"), uiMessage("ble.connecting"));
  deadlineAt_ = nowMs() + 30000;
  if (!clock_)
    deadline.start(30000);
  ++attempts;
  c->connectToDevice();
}
void BleTransport::readyService(quint64 generation) {
  auto *s = service.data();
  if (!s || epoch != generation)
    return;
  auto rx = s->characteristic(QBluetoothUuid(QString::fromLatin1(ap_uuid(1))));
  tx = s->characteristic(QBluetoothUuid(QString::fromLatin1(ap_uuid(2))));
  auto flags = rx.properties();
  if (!rx.isValid() ||
      !(flags & (QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Indicate))) {
    fail(QCoreApplication::translate("Allpowers", "Notification channel unavailable"),
         uiMessage("ble.no_notify"));
    return;
  }
  if (!session.poll().isEmpty() &&
      (!tx.isValid() || !(tx.properties() & (QLowEnergyCharacteristic::Write |
                                             QLowEnergyCharacteristic::WriteNoResponse)))) {
    fail(QCoreApplication::translate("Allpowers", "Telemetry request channel unavailable"),
         uiMessage("ble.no_request"));
    return;
  }
  auto cccd = rx.clientCharacteristicConfiguration();
  if (!cccd.isValid()) {
    fail(QCoreApplication::translate("Allpowers", "CCCD subscription descriptor missing"),
         uiMessage("ble.no_cccd"));
    return;
  }
  const auto token = session.generation();
  connect(s, &QLowEnergyService::characteristicChanged, this,
          [this, s, rx, generation, token](const auto &characteristic, const QByteArray &value) {
            if (!active_ || epoch != generation || service != s || characteristic != rx)
              return;
            acceptPacket(generation, token, value);
          });
  connect(s, &QLowEnergyService::descriptorWritten, this,
          [this, s, cccd, generation](const auto &descriptor, const QByteArray &value) {
            if (!active_ || service != s || epoch != generation || descriptor != cccd ||
                value == QLowEnergyCharacteristic::CCCDDisable)
              return;
            acceptReady(generation);
          });
  s->writeDescriptor(cccd, flags & QLowEnergyCharacteristic::Notify
                               ? QLowEnergyCharacteristic::CCCDEnableNotification
                               : QLowEnergyCharacteristic::CCCDEnableIndication);
}
void BleTransport::sendPoll() {
  auto data = session.poll();
  if (!connected_ || (!service && !channel_) || data.isEmpty())
    return;
  if (channel_) {
    channel_->write(data);
    ++polls;
    pollAt_ = nowMs() + ap_poll_interval_ms();
    return;
  }
  if (!(tx.properties() &
        (QLowEnergyCharacteristic::Write | QLowEnergyCharacteristic::WriteNoResponse))) {
    fail(QCoreApplication::translate("Allpowers", "Request channel missing"),
         uiMessage("ble.no_channel"));
    return;
  }
  service->writeCharacteristic(tx, data,
                               tx.properties() & QLowEnergyCharacteristic::Write
                                   ? QLowEnergyService::WriteWithResponse
                                   : QLowEnergyService::WriteWithoutResponse);
  polls++;
  pollAt_ = nowMs() + ap_poll_interval_ms();
}

void BleTransport::refresh() {
  const auto now = nowMs();
  // Native QTimers remain the scheduler for the native clock. An externally
  // supplied clock is advanced by its owner and drives those same callbacks.
  // Do not run both schedulers: a late native timer could otherwise double-poll.
  if (clock_) {
    if (active_ && retryAt_ >= 0 && now >= retryAt_)
      connectDevice();
    if (active_ && deadlineAt_ >= 0 && now >= deadlineAt_)
      fail(QCoreApplication::translate("Allpowers", "BLE timed out"), uiMessage("ble.timeout"));
    if (connected_ && pollAt_ >= 0 && now >= pollAt_)
      sendPoll();
  }
  auto s = session.snapshot(quint32(now));
  if (!connected_)
    s.fresh = 0;
  emit snapshot(s);
  if (connected_) {
    recovery.observe(quint32(now), s.fresh);
    if (recovery.reconnectDue(quint32(now)))
      fail(QCoreApplication::translate("Allpowers", "Full telemetry is not arriving"),
           uiMessage("ble.incomplete"));
  }
}
void BleTransport::acceptPacket(quint64 generation, quint64 token, const QByteArray &value) {
  if (!active_ || generation != epoch)
    return;
  const bool valid = session.receive(token, value, quint32(nowMs()));
  ++packets;
  emit packet(QString::fromLatin1(value.toHex()), valid);
}
void BleTransport::acceptReady(quint64 generation) {
  if (!active_ || generation != epoch || connected_)
    return;
  deadline.stop();
  deadlineAt_ = -1;
  connected_ = true;
  ++connects;
  retrySeconds = 2;
  recovery.begin(quint32(nowMs()));
  error_.clear();
  errorMessage_ = {};
  setPhase(QCoreApplication::translate("Allpowers", "Connected · receiving data"),
           uiMessage("ble.connected"));
  if (!clock_)
    poll.start();
  sendPoll();
}
QJsonObject BleTransport::diagnostics() const {
  return {{"transport", "Qt Bluetooth / Windows WinRT"},
          {"address", address_},
          {"name", name_},
          {"phase", phase_},
          {"error", error_},
          {"ui_messages", QJsonObject{{"phase", phaseMessage_}, {"error", errorMessage_}}},
          {"active", active_},
          {"connected", connected_},
          {"scanning", scanning()},
          {"discovery_attempts", qint64(discoveries)},
          {"connection_attempts", qint64(attempts)},
          {"bluetooth_used", discoveries > 0 || attempts > 0},
          {"connections", qint64(connects)},
          {"drops", qint64(drops)},
          {"retry_at_ms", retryAt_},
          {"gatt_deadline_ms", deadlineAt_},
          {"polls", qint64(polls)},
          {"packets", qint64(packets)},
          {"mtu", controller ? controller->mtu() : 0},
          {"service", QString::fromLatin1(ap_uuid(0))},
          {"notify", QString::fromLatin1(ap_uuid(1))},
          {"write", QString::fromLatin1(ap_uuid(2))}};
}
