#include "runtime_log.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUuid>

namespace {
QMutex mutex;
QString logPath, run;
QElapsedTimer uptime;
QtMessageHandler previousHandler = nullptr;
quint64 dropped = 0;
qint64 rateWindow = 0;
int rateCount = 0;
constexpr qint64 maxBytes = 2 * 1024 * 1024;

QJsonValue scrub(QJsonValue value) {
  if (value.isObject()) {
    auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
      if (it.key() == "address" || it.key() == "endpoint" || it.key() == "usb_serial" ||
          it.key() == "station_address" || it.key() == "name" || it.key() == "station_name")
        it.value() = "<redacted>";
      else
        it.value() = scrub(it.value());
    }
    return object;
  }
  if (value.isArray()) {
    auto array = value.toArray();
    for (qsizetype i = 0; i < array.size(); ++i)
      array[i] = scrub(array[i]);
    return array;
  }
  if (value.isString()) {
    auto text = value.toString();
    static const QRegularExpression mac("(?i)\\b[0-9a-f]{2}(?:[:-][0-9a-f]{2}){5}\\b");
    static const QRegularExpression serial("(?i)R2500-[0-9a-f]{12}");
    text.replace(mac, "<address>");
    text.replace(serial, "<serial>");
    const auto home = QDir::homePath();
    if (!home.isEmpty()) {
      text.replace(home, "<home>", Qt::CaseInsensitive);
      text.replace(QDir::toNativeSeparators(home), "<home>", Qt::CaseInsensitive);
    }
    return text.left(4096);
  }
  return value;
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
  if (type != QtDebugMsg && type != QtInfoMsg)
    RuntimeLog::record(type == QtFatalMsg ? "qt.fatal" : "qt.message",
                       {{"level", int(type)},
                        {"category", QString::fromUtf8(context.category)},
                        {"message", message}});
  if (previousHandler)
    previousHandler(type, context, message);
}
} // namespace

RuntimeLog::RuntimeLog(const QString &directory) {
  {
    QMutexLocker guard(&mutex);
    Q_ASSERT(logPath.isEmpty());
    QDir().mkpath(directory);
    logPath = QDir(directory).filePath("diagnostics.jsonl");
    run = QUuid::createUuid().toString(QUuid::WithoutBraces);
    uptime.start();
    dropped = 0;
    rateWindow = 0;
    rateCount = 0;
  }
  previousHandler = qInstallMessageHandler(messageHandler);
  record("runtime.start", {{"application", QCoreApplication::applicationName()},
                           {"version", QCoreApplication::applicationVersion()},
                           {"qt", qVersion()},
                           {"os", QSysInfo::prettyProductName()}});
}
RuntimeLog::~RuntimeLog() {
  record("runtime.end");
  qInstallMessageHandler(previousHandler);
  QMutexLocker guard(&mutex);
  logPath.clear();
}
QString RuntimeLog::path() {
  QMutexLocker guard(&mutex);
  return logPath;
}
void RuntimeLog::record(const QString &event, QJsonObject fields) {
  // A Qt warning emitted by the file/locking code must never recurse into this sink.
  static thread_local bool writing = false;
  if (writing)
    return;
  writing = true;
  const auto write = [&] {
    QMutexLocker guard(&mutex);
    if (logPath.isEmpty())
      return;
    const auto now = uptime.elapsed();
    if (now - rateWindow >= 1000) {
      rateWindow = now;
      rateCount = 0;
    }
    if (++rateCount > 50 && !event.startsWith("runtime.") && event != "qt.fatal") {
      ++dropped;
      return;
    }
    // Several Monitor processes may share the directory. Never wait on their I/O.
    QLockFile lock(logPath + ".lock");
    if (!lock.tryLock(0)) {
      ++dropped;
      return;
    }
    QFile file(logPath);
    if (file.size() >= maxBytes) {
      const auto oldest = logPath + ".2";
      if (QFile::exists(oldest) && !QFile::remove(oldest)) {
        ++dropped;
        return;
      }
      if (QFile::exists(logPath + ".1") && !QFile::rename(logPath + ".1", oldest)) {
        ++dropped;
        return;
      }
      if (!file.rename(logPath + ".1")) {
        ++dropped;
        return;
      }
      file.setFileName(logPath);
    }
    QJsonObject row{{"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                    {"uptime_ms", now},
                    {"run", run},
                    {"pid", QCoreApplication::applicationPid()},
                    {"event", event},
                    {"data", scrub(fields)},
                    {"dropped_records", qint64(dropped)}};
    const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > 65536 || !file.open(QIODevice::WriteOnly | QIODevice::Append) ||
        file.write(bytes) != bytes.size() || !file.flush()) {
      ++dropped;
      return;
    }
    dropped = 0;
  };
  write();
  writing = false;
}

TransportTrace::TransportTrace(TelemetryTransport *source, QObject *parent,
                               std::function<QJsonObject()> details)
    : QObject(parent), transport(source), extra(std::move(details)) {
  clock.start();
  connect(source, &TelemetryTransport::stateChanged, this, &TransportTrace::state);
  connect(source, &TelemetryTransport::newSession, this,
          [this] { RuntimeLog::record("transport.session", context()); });
  connect(source, &TelemetryTransport::logReceived, this,
          [](QString text) { RuntimeLog::record("transport.message", {{"message", text}}); });
  connect(source, &TelemetryTransport::snapshot, this, [this](ap_snapshot value) {
    const bool changed = !snapshots || value.fresh != last.fresh || value.known != last.known;
    last = value;
    receivedAt = clock.elapsed();
    ++snapshots;
    if (changed)
      RuntimeLog::record("telemetry.state", context());
  });
  connect(source, &QObject::destroyed, this, [this] {
    timer.stop();
    transport = nullptr;
  });
  connect(&timer, &QTimer::timeout, this, [this] {
    auto data = context();
    data["heartbeat_gap_ms"] = clock.elapsed() - heartbeatAt;
    heartbeatAt = clock.elapsed();
    RuntimeLog::record("runtime.heartbeat", data);
  });
  timer.start(30000);
}
QJsonObject TransportTrace::context() const {
  auto data = transport ? transport->diagnostics() : QJsonObject{};
  data["snapshot_count"] = qint64(snapshots);
  data["snapshot_age_ms"] = receivedAt < 0 ? -1 : clock.elapsed() - receivedAt;
  data["fresh"] = bool(last.fresh);
  data["known"] = int(last.known);
  data["valid_frames"] = qint64(last.valid_frames);
  data["invalid_frames"] = qint64(last.invalid_frames);
  data["main_age_ms"] = qint64(last.main_age_ms);
  data["settings_age_ms"] = qint64(last.settings_age_ms);
  if (extra)
    data["viewer"] = extra();
  return data;
}
void TransportTrace::state() {
  const auto info = transport->diagnostics();
  QJsonObject state{{"active", transport->active()},
                    {"connected", transport->connected()},
                    {"phase", transport->phase()}};
  for (const auto &key : {"error", "gatt_connected", "usb_verified", "usb_history_result"})
    state[key] = info[key];
  if (state == previousState)
    return;
  previousState = state;
  RuntimeLog::record("transport.state", context());
}
