#include "service_host.h"
#include "../../drivers/windows/bridge.h"
#include "bluetooth.h"
#include "history.h"
#include "i18n.h"
#include "integration.h"
#include "ipc.h"
#include "product-version.h"
#include "ups_state.h"
#include <QBluetoothAddress>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
struct Publisher {
  HANDLE handle = INVALID_HANDLE_VALUE;
  QString error;
  QJsonObject errorMessage;
  ~Publisher() {
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
  }
  void publish(const AllpowersUps::State &state) {
    if (handle == INVALID_HANDLE_VALUE)
      handle = CreateFileW(UpsDevicePath, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      const auto code = GetLastError();
      error = QString("Driver unavailable (%1)").arg(code);
      errorMessage = uiMessage("driver.unavailable", {int(code)});
      return;
    }
    DWORD bytes = 0;
    if (!DeviceIoControl(handle, UpsSetState, const_cast<AllpowersUps::State *>(&state),
                         sizeof(state), nullptr, 0, &bytes, nullptr)) {
      const auto code = GetLastError();
      error = QString("Driver update failed (%1)").arg(code);
      errorMessage = uiMessage("driver.update_failed", {int(code)});
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    } else {
      error.clear();
      errorMessage = {};
    }
  }
};
struct Client {
  QByteArray buffer;
  HistoryPoints history;
  QString historyAddress;
  qint64 window = 0;
  int requests = 0;
};
} // namespace
struct ServiceHost::Impl {
  ServiceHost *owner;
  ServiceOptions options;
  LocalPipeServer server;
  BleTransport ble;
  QThread thread;
  HistoryWorker *history = nullptr;
  HistoryPoints points;
  QJsonObject config, stats;
  QHash<QLocalSocket *, Client> clients;
  QHash<QString, QJsonObject> devices;
  QTimer tick;
  QElapsedTimer uptime;
  Publisher publisher;
  ap_snapshot last{};
  quint64 generation = 1, sequence = 0;
  qint64 lastSample = -1;
  bool busy = false, started = false;
  QString error;
  Impl(ServiceHost *parent, ServiceOptions o)
      : owner(parent), options(std::move(o)), server(parent),
        ble(parent, options.offline, !options.offline) {
    uptime.start();
  }
  void receiveSnapshot(ap_snapshot s) {
    last = s;
    if (options.publishDriver)
      publisher.publish(makeUpsState(s, generation, ++sequence));
    auto wall = QDateTime::currentSecsSinceEpoch();
    if (!busy && s.main_seen && wall != lastSample && !ble.address().isEmpty()) {
      busy = true;
      lastSample = wall;
      auto *worker = history;
      auto address = ble.address();
      auto tick = uptime.elapsed() / 1000;
      QMetaObject::invokeMethod(
          worker, [worker, address, s, wall, tick] { worker->sample(address, s, wall, tick); },
          Qt::QueuedConnection);
    }
  }
  void send(QLocalSocket *socket, QJsonObject object) {
    object["v"] = Ipc::Version;
    if (socket->bytesToWrite() > 2 * Ipc::MaxFrame) {
      socket->abort();
      return;
    }
    const auto bytes = Ipc::frame(object);
    if (bytes.isEmpty())
      socket->abort();
    else
      socket->write(bytes);
  }
  QJsonObject state() const {
    auto info = ble.diagnostics();
    info["driver_error"] = publisher.error;
    const auto ups = makeUpsState(last, generation, sequence + 1);
    info["ups_present"] = bool(ups.flags & AllpowersUps::Present) && publisher.error.isEmpty() &&
                          options.publishDriver;
    info["ups_reason"] =
        publisher.error.isEmpty() && !(ups.flags & AllpowersUps::Present)
            ? QCoreApplication::translate("Allpowers", "No fresh confirmed charge and power source")
            : publisher.error;
    auto messages = info["ui_messages"].toObject();
    messages["driver_error"] = publisher.errorMessage;
    messages["ups_reason"] = publisher.error.isEmpty() && !(ups.flags & AllpowersUps::Present)
                                 ? uiMessage("ups.no_fresh_data")
                                 : publisher.errorMessage;
    info["ui_messages"] = messages;
    info["history"] = stats;
    info["history_error"] = error;
    info["history_path"] = QDir(options.directory).filePath("history.sqlite3");
    info["profile"] = config["profile"].toInt();
    info["service"] = true;
    info["application"] = APP_VERSION;
    return {{"op", "state"}, {"diagnostics", info}, {"data", Ipc::snapshot(last)}};
  }
  void broadcast(QJsonObject object) {
    for (auto *s : clients.keys())
      send(s, object);
  }
  void log(QString message) {
    if (history)
      QMetaObject::invokeMethod(
          history, [h = history, message] { h->log(message); }, Qt::QueuedConnection);
    broadcast({{"op", "log"}, {"message", message}});
  }
  void request(QLocalSocket *socket, const QJsonObject &o) {
    auto &client = clients[socket];
    const auto second = uptime.elapsed() / 1000;
    if (client.window != second) {
      client.window = second;
      client.requests = 0;
    }
    if (++client.requests > 64) {
      socket->abort();
      return;
    }
    const QString op = o["op"].toString();
    if (op == "state")
      send(socket, state());
    else if (op == "scan") {
      devices.clear();
      ble.scan();
    } else if (op == "stop") {
      ble.stop();
      send(socket, state());
    } else if (op == "select") {
      const auto address = QBluetoothAddress(o["address"].toString());
      const auto name = o["name"].toString();
      const int profile = o["profile"].toInt(-1);
      if (address.isNull() || name.size() > 128 || profile < 0 || profile > 3) {
        send(socket, {{"op", "error"},
                      {"message", "Invalid station selection"},
                      {"ui_message", uiMessage("service.invalid_selection")}});
        return;
      }
      const QJsonObject selected{
          {"address", address.toString()}, {"name", name}, {"profile", profile}};
      QSaveFile file(QDir(options.directory).filePath("settings.json"));
      const auto bytes = QJsonDocument(selected).toJson();
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Could not save station selection";
        send(socket, {{"op", "error"},
                      {"message", error},
                      {"ui_message", uiMessage("service.save_failed")}});
        return;
      }
      ble.stop();
      ++generation;
      config = selected;
      error.clear();
      points.clear();
      QMetaObject::invokeMethod(
          history,
          [h = history, address = address.toString()] {
            h->load(address, QDateTime::currentSecsSinceEpoch());
          },
          Qt::QueuedConnection);
      ble.resume(address.toString(), name, profile);
      broadcast(state());
    } else if (op == "history") {
      const int offset = o["offset"].toInt(-1);
      if (offset < 0 || offset > 7000) {
        socket->abort();
        return;
      }
      if (!offset) {
        client.history = points;
        client.historyAddress = ble.address();
      }
      QJsonArray rows;
      for (int i = offset; i < qMin(offset + Ipc::HistoryPage, int(client.history.size())); ++i)
        rows.append(Ipc::point(client.history[i]));
      send(socket, {{"op", "history"},
                    {"offset", offset},
                    {"total", client.history.size()},
                    {"address", client.historyAddress},
                    {"points", rows}});
    } else
      send(socket, {{"op", "error"},
                    {"message", "Unknown operation"},
                    {"ui_message", uiMessage("service.unknown_operation")}});
  }
};
ServiceHost::ServiceHost(const ServiceOptions &options, QObject *parent)
    : QObject(parent), impl(new Impl(this, options)) {
  qRegisterMetaType<ap_snapshot>();
  qRegisterMetaType<HistoryPoints>();
}
bool ServiceHost::start() {
  auto *d = impl;
  if (d->started || d->options.directory.isEmpty())
    return false;
  if (!QDir().mkpath(d->options.directory)) {
    d->error = "Could not create service data directory";
    return false;
  }
  QFile config(QDir(d->options.directory).filePath("settings.json"));
  if (config.open(QIODevice::ReadOnly) && config.size() <= Ipc::MaxFrame)
    d->config = QJsonDocument::fromJson(config.readAll()).object();
  d->last.abi_version = 1;
  d->history = new HistoryWorker;
  d->history->moveToThread(&d->thread);
  connect(&d->thread, &QThread::finished, d->history, &QObject::deleteLater);
  connect(d->history, &HistoryWorker::sampleFinished, this, [d] { d->busy=false; });
  connect(d->history, &HistoryWorker::loaded, this,
          [d](QString address, HistoryPoints p, QJsonObject stats) {
            if (address != d->ble.address())
              return;
            d->points = p;
            d->stats = stats;
            if (!p.isEmpty())
              d->broadcast({{"op", "sample"}, {"point", Ipc::point(p.last())}});
          });
  connect(d->history, &HistoryWorker::problem, this, [d](QString error) {
    d->error = error;
  });
  d->thread.start();
  auto *h = d->history;
  const auto path = QDir(d->options.directory).filePath("history.sqlite3");
  const auto address = d->config["address"].toString();
  QMetaObject::invokeMethod(
      h,
      [h, path, address] {
        h->open(path);
        h->load(address, QDateTime::currentSecsSinceEpoch());
      },
      Qt::QueuedConnection);
  connect(&d->server, &LocalPipeServer::connected, this, [d](QLocalSocket *socket) {
    if (d->clients.size() >= 8) {
      socket->abort();
      socket->deleteLater();
      return;
    }
    d->clients.insert(socket, {});
    socket->setReadBufferSize(Ipc::MaxFrame + 4);
    QObject::connect(socket, &QLocalSocket::readyRead, d->owner, [d, socket] {
      auto &buffer = d->clients[socket].buffer;
      buffer += socket->readAll();
      QList<QJsonObject> requests;
      if (!Ipc::consume(buffer, requests)) {
        socket->abort();
        return;
      }
      for (const auto &request : requests) {
        if (!d->clients.contains(socket))
          break;
        d->request(socket, request);
      }
    });
    QObject::connect(socket, &QLocalSocket::disconnected, d->owner, [d, socket] {
      d->clients.remove(socket);
      socket->deleteLater();
    });
    d->send(socket, d->state());
    for (auto device : d->devices)
      d->send(socket, device);
  });
  if (!d->server.listen(d->options.pipeName.isEmpty() ? Ipc::endpoint() : d->options.pipeName)) {
    d->error = d->server.error;
    return false;
  }
  connect(&d->ble, &BleTransport::found, this,
          [d](QString address, QString name, int rssi, int model) {
            QJsonObject device{{"op", "found"},
                               {"address", address},
                               {"name", name},
                               {"rssi", rssi},
                               {"model", model}};
            if (d->devices.size() < 256 || d->devices.contains(address))
              d->devices[address] = device;
            d->broadcast(device);
          });
  connect(&d->ble, &BleTransport::stateChanged, this, [d] {
    d->broadcast(d->state());
    d->log(d->ble.phase());
  });
  connect(&d->ble, &BleTransport::packet, this, [d](QString hex, bool valid) {
    d->broadcast({{"op", "packet"}, {"hex", hex}, {"valid", valid}});
  });
  connect(&d->ble, &BleTransport::newSession, this,
          [h] { QMetaObject::invokeMethod(h, &HistoryWorker::newSession, Qt::QueuedConnection); });
  connect(&d->ble, &BleTransport::snapshot, this, [d](ap_snapshot s) { d->receiveSnapshot(s); });
  d->tick.setInterval(1000);
  connect(&d->tick, &QTimer::timeout, this, [d] { d->broadcast(d->state()); });
  d->tick.start();
  d->started = true;
  if (!address.isEmpty())
    d->ble.resume(address, d->config["name"].toString(), d->config["profile"].toInt());
  d->log("ALLPOWERS service started");
  return true;
}
ServiceHost::~ServiceHost() {
  auto *d = impl;
  d->ble.disconnect(this);
  d->ble.stop();
  if (d->options.publishDriver)
    d->publisher.publish(makeUpsState({}, d->generation, ++d->sequence));
  for (auto *s : d->clients.keys()) {
    s->disconnect(this);
    s->abort();
  }
  if (d->thread.isRunning()) {
    QMetaObject::invokeMethod(d->history, [] {}, Qt::BlockingQueuedConnection);
    d->thread.quit();
    d->thread.wait();
  }
  delete d;
}
QJsonObject ServiceHost::diagnostics() const {
  return impl->state();
}
QString ServiceHost::error() const {
  return impl->error;
}
bool ServiceHost::injectLabSnapshot(ap_snapshot data) {
  if (!impl->options.offline || !impl->started)
    return false;
  ap_snapshot validated{};
  if (!Ipc::snapshot(Ipc::snapshot(data), validated))
    return false;
  impl->receiveSnapshot(validated);
  return true;
}
QString serviceDataDirectory() {
  return QDir(qEnvironmentVariable("PROGRAMDATA", "C:/ProgramData")).filePath("AllpowersMonitor");
}

namespace {
SERVICE_STATUS_HANDLE scmHandle = nullptr;
SERVICE_STATUS scmStatus{};
std::mutex scmMutex;
HANDLE serviceStopped = nullptr;
QCoreApplication *serviceApp = nullptr;
ServiceHost *runningHost = nullptr;
bool labReplay = false;
std::atomic<DWORD> serviceFailure{NO_ERROR};
QTimer *replayTimer = nullptr;
void reportScm(DWORD state, DWORD error = NO_ERROR) {
  if (error)
    serviceFailure.store(error);
  std::lock_guard<std::mutex> lock(scmMutex);
  scmStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  scmStatus.dwCurrentState = state;
  scmStatus.dwWin32ExitCode = error;
  scmStatus.dwControlsAccepted =
      state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
  scmStatus.dwWaitHint =
      state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ? 15000 : 0;
  scmStatus.dwCheckPoint = scmStatus.dwWaitHint ? scmStatus.dwCheckPoint + 1 : 0;
  SetServiceStatus(scmHandle, &scmStatus);
}
DWORD WINAPI serviceControl(DWORD control, DWORD, LPVOID, LPVOID) {
  if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
    reportScm(SERVICE_STOP_PENDING);
    QMetaObject::invokeMethod(
        serviceApp,
        [] {
          delete runningHost;
          runningHost = nullptr;
          serviceApp->quit();
        },
        Qt::QueuedConnection);
  } else if (control == SERVICE_CONTROL_INTERROGATE) {
    std::lock_guard<std::mutex> lock(scmMutex);
    SetServiceStatus(scmHandle, &scmStatus);
  }
  return NO_ERROR;
}
void WINAPI serviceMain(DWORD, LPWSTR *) {
  scmHandle = RegisterServiceCtrlHandlerExW(L"AllpowersMonitor", serviceControl, nullptr);
  if (!scmHandle) {
    serviceFailure.store(GetLastError());
    QMetaObject::invokeMethod(serviceApp, &QCoreApplication::quit, Qt::QueuedConnection);
    return;
  }
  reportScm(SERVICE_START_PENDING);
  QMetaObject::invokeMethod(
      serviceApp,
      [] {
        runningHost = new ServiceHost({serviceDataDirectory(), {}, labReplay, true});
        if (!runningHost->start()) {
          reportScm(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
          serviceApp->quit();
        } else {
          if (labReplay) {
            QFile file(QDir(serviceDataDirectory()).filePath("lab-replay.json"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
              reportScm(SERVICE_STOPPED, ERROR_INVALID_DATA);
              serviceApp->quit();
              return;
            }
            const auto rows = QJsonDocument::fromJson(file.readAll()).array();
            if (rows.isEmpty() || rows.size() > 4096) {
              reportScm(SERVICE_STOPPED, ERROR_INVALID_DATA);
              serviceApp->quit();
              return;
            }
            qint64 last = -1;
            for (const auto &row : rows) {
              auto object = row.toObject();
              ap_snapshot data{};
              const qint64 at = object["at_ms"].toInteger(-1);
              if (at < last || at < 0 || at > 86400000 ||
                  !Ipc::snapshot(object["snapshot"].toObject(), data)) {
                reportScm(SERVICE_STOPPED, ERROR_INVALID_DATA);
                serviceApp->quit();
                return;
              }
              last = at;
            }
            replayTimer = new QTimer(serviceApp);
            auto elapsed = std::make_shared<QElapsedTimer>();
            elapsed->start();
            QObject::connect(replayTimer, &QTimer::timeout, serviceApp, [rows, elapsed] {
              QJsonObject current;
              qint64 at = 0;
              for (const auto &row : rows) {
                if (row.toObject()["at_ms"].toInteger() > elapsed->elapsed())
                  break;
                current = row.toObject();
                at = current["at_ms"].toInteger();
              }
              if (current.isEmpty() || !runningHost)
                return;
              ap_snapshot data{};
              Ipc::snapshot(current["snapshot"].toObject(), data);
              if (!current["repeat"].toBool()) {
                const auto age = quint32(elapsed->elapsed() - at);
                data.main_age_ms = qMin(quint32(15001), data.main_age_ms + age);
                data.settings_age_ms = qMin(quint32(15001), data.settings_age_ms + age);
                if (data.main_age_ms >= 15000 || data.settings_age_ms >= 15000)
                  data.fresh = 0;
              }
              runningHost->injectLabSnapshot(data);
            });
            replayTimer->start(500);
          }
          reportScm(SERVICE_RUNNING);
        }
      },
      Qt::QueuedConnection);
  WaitForSingleObject(serviceStopped, INFINITE);
}
} // namespace
int runWindowsService(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  labReplay = app.arguments().contains("--lab-replay");
  if (labReplay) {
    const auto status = integrationStatus();
    if (!status["virtual_machine"].toBool() || !status["test_signing"].toBool())
      return ERROR_ACCESS_DENIED;
  }
  app.setApplicationName("AllpowersMonitor");
  serviceApp = &app;
  serviceStopped = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!serviceStopped)
    return int(GetLastError());
  DWORD dispatcherError = NO_ERROR;
  std::thread dispatcher([&] {
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(L"AllpowersMonitor"), serviceMain},
                                    {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) {
      dispatcherError = GetLastError();
      QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    }
  });
  app.exec();
  delete runningHost;
  runningHost = nullptr;
  if (scmHandle)
    reportScm(SERVICE_STOPPED, serviceFailure.load());
  SetEvent(serviceStopped);
  dispatcher.join();
  CloseHandle(serviceStopped);
  serviceStopped = nullptr;
  serviceApp = nullptr;
  return int(dispatcherError ? dispatcherError : serviceFailure.load());
}
