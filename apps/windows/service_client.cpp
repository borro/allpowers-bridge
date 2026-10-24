#include "service_client.h"
#include "integration.h"
#include "ipc.h"
#include <QDateTime>
#include <QJsonArray>

ServiceTransport::ServiceTransport(QObject *parent, QString pipe, bool checkOwner)
    : BleTransport(parent, true, false), pipeName(pipe.isEmpty() ? Ipc::endpoint() : pipe),
      verifyOwner(checkOwner) {
  clock.start();
  socket.setReadBufferSize(Ipc::MaxFrame + 4);
  connect(&socket, &QLocalSocket::connected, this, [this] {
    if (verifyOwner && !isServicePipe(socket.socketDescriptor())) {
      socket.abort();
      return;
    }
    buffer.clear();
    send({{"op", "state"}});
    historyBusy = false;
    historyTimer.start();
  });
  connect(&socket, &QLocalSocket::readyRead, this, &ServiceTransport::receive);
  connect(&socket, &QLocalSocket::disconnected, this, [this] {
    info["active"] = false;
    info["connected"] = false;
    info["scanning"] = false;
    historyTimer.stop();
    historyBusy = false;
    ap_snapshot missing{};
    missing.abi_version = 1;
    emit snapshot(missing);
    emit stateChanged();
  });
  connect(&socket, &QLocalSocket::errorOccurred, this, [this](auto) { emit stateChanged(); });
  retry.setInterval(1000);
  connect(&retry, &QTimer::timeout, this, [this] {
    if (socket.state() == QLocalSocket::UnconnectedState)
      connectPipe();
    else if (socket.state() == QLocalSocket::ConnectedState && clock.elapsed() - stateAt > 5000)
      socket.abort();
  });
  retry.start();
  historyTimer.setInterval(5000);
  connect(&historyTimer, &QTimer::timeout, this, [this] {
    if (!historyBusy) {
      historyBusy = true;
      historyDelta.clear();
      send({{"op", "history"}, {"offset", 0}});
    }
  });
  QTimer::singleShot(0, this, &ServiceTransport::connectPipe);
}
ServiceTransport::~ServiceTransport() {
  // QLocalSocket's destructor can emit disconnected. Its slots use info/history,
  // which would otherwise already be destroyed by member destruction order.
  retry.stop();
  historyTimer.stop();
  socket.disconnect(this);
  socket.abort();
}
void ServiceTransport::connectPipe() {
  stateAt = clock.elapsed();
  socket.connectToServer(pipeName);
}
void ServiceTransport::send(QJsonObject message) {
  if (socket.state() != QLocalSocket::ConnectedState)
    return;
  message["v"] = Ipc::Version;
  auto data = Ipc::frame(message);
  if (!data.isEmpty() && socket.bytesToWrite() < Ipc::MaxFrame)
    socket.write(data);
}
QString ServiceTransport::phase() const {
  return socket.state() == QLocalSocket::ConnectedState
             ? info["phase"].toString("Подключение к службе…")
             : "Служба ИБП недоступна";
}
QJsonObject ServiceTransport::diagnostics() const {
  auto result = info;
  result["service"] = true;
  result["transport"] = "Служба Windows / локальный IPC";
  if (socket.state() != QLocalSocket::ConnectedState)
    result["error"] = socket.errorString();
  return result;
}
void ServiceTransport::receive() {
  buffer += socket.readAll();
  QList<QJsonObject> messages;
  if (!Ipc::consume(buffer, messages)) {
    socket.abort();
    return;
  }
  for (const auto &message : messages) {
    auto op = message["op"].toString();
    if (op == "state") {
      ap_snapshot data{};
      if (!Ipc::snapshot(message["data"].toObject(), data)) {
        socket.abort();
        return;
      }
      const auto oldAddress = address();
      const auto previous = info;
      info = message["diagnostics"].toObject();
      stateAt = clock.elapsed();
      if (oldAddress != address()) {
        pendingHistory.clear();
        currentHistory.clear();
        historyDelta.clear();
        historyBusy = false;
        emit historyReady({});
        emit newSession();
      }
      bool changed = previous.isEmpty();
      for (const auto &key : {"address", "name", "phase", "error", "active", "connected",
                              "scanning", "profile", "driver_error"})
        changed = changed || previous[key] != info[key];
      if (changed)
        emit stateChanged();
      emit snapshot(data);
    } else if (op == "found")
      emit found(message["address"].toString(), message["name"].toString(), message["rssi"].toInt(),
                 message["model"].toInt());
    else if (op == "packet")
      emit packet(message["hex"].toString(), message["valid"].toBool());
    else if (op == "log")
      emit logReceived(message["message"].toString());
    else if (op == "error") {
      info["error"] = message["message"];
      emit stateChanged();
    } else if (op == "sample") {
      HistoryPoint point;
      if (!Ipc::point(message["point"].toObject(), point) || point.address != address())
        continue;
      currentHistory = mergeHistory(currentHistory, {point});
      const auto oldest = QDateTime::currentSecsSinceEpoch() - 172800;
      while (!currentHistory.isEmpty() &&
             (currentHistory.first().wall < oldest || currentHistory.size() > 7000))
        currentHistory.removeFirst();
      if (historyBusy && historyDelta.size() < 128)
        historyDelta.append(point);
      emit historyReady(currentHistory);
    } else if (op == "history") {
      const int offset = message["offset"].toInt(-1), total = message["total"].toInt(-1);
      if (offset == 0) {
        pendingHistory.clear();
        historyAddress = message["address"].toString();
      }
      if (total < 0 || total > 7000 || offset != pendingHistory.size() ||
          historyAddress != address()) {
        historyBusy = false;
        pendingHistory.clear();
        continue;
      }
      const auto rows = message["points"].toArray();
      if (rows.size() > Ipc::HistoryPage || (rows.isEmpty() && offset < total)) {
        socket.abort();
        return;
      }
      for (const auto &row : rows) {
        HistoryPoint point;
        if (!Ipc::point(row.toObject(), point) || point.address != historyAddress) {
          socket.abort();
          return;
        }
        pendingHistory.append(point);
      }
      if (pendingHistory.size() < total)
        send({{"op", "history"}, {"offset", pendingHistory.size()}});
      else {
        historyBusy = false;
        currentHistory = mergeHistory(pendingHistory, historyDelta);
        historyDelta.clear();
        emit historyReady(currentHistory);
      }
    }
  }
}
void ServiceTransport::scan() {
  send({{"op", "scan"}});
}
void ServiceTransport::start(const QString &address, int model) {
  resume(address, {}, model);
}
void ServiceTransport::resume(const QString &address, const QString &name, int model) {
  send({{"op", "select"}, {"address", address}, {"name", name}, {"profile", model}});
}
void ServiceTransport::stop() {
  send({{"op", "stop"}});
}
