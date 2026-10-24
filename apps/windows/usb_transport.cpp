#include "usb_transport.h"
#include "AllpowersModels.h"
#include <QBluetoothAddress>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <limits>
namespace {
bool number(const QJsonObject &o, const char *key, quint32 &out, quint32 max = UINT32_MAX) {
  auto v = o[key];
  double n = v.toDouble(-1);
  if (!v.isDouble() || n < 0 || n > max || double(quint32(n)) != n)
    return false;
  out = quint32(n);
  return true;
}
quint32 crc32(const QByteArray &data) {
  quint32 crc = 0xffffffffu;
  for (auto c : data) {
    crc ^= quint8(c);
    for (int b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ (0xedb88320u & quint32(-qint32(crc & 1)));
  }
  return ~crc;
}
bool historyRecord(const QJsonObject &o, HistoryPoint &p, quint32 &session, quint32 &epoch) {
  quint32 seq, tick, soc, in, out, minutes, flags, check;
  if (!number(o, "sequence", seq) || !seq || !number(o, "session", session) ||
      !number(o, "uptime_s", tick) || !number(o, "epoch", epoch) || !number(o, "soc", soc, 255) ||
      !number(o, "charge_w", in, 65535) || !number(o, "output_w", out, 65535) ||
      !number(o, "runtime_minutes", minutes, 65535) || !number(o, "flags", flags, 255) ||
      !number(o, "crc", check))
    return false;
  QByteArray bytes(28, 0);
  auto *d = reinterpret_cast<uchar *>(bytes.data());
  qToLittleEndian<quint32>(0x31504841, d);
  qToLittleEndian(seq, d + 4);
  qToLittleEndian(session, d + 8);
  qToLittleEndian(tick, d + 12);
  qToLittleEndian(epoch, d + 16);
  qToLittleEndian<quint16>(in, d + 20);
  qToLittleEndian<quint16>(out, d + 22);
  qToLittleEndian<quint16>(minutes, d + 24);
  d[26] = uchar(soc);
  d[27] = uchar(flags);
  if (crc32(bytes) != check || ((flags & 1) && soc > 100) || ((flags & 6) == 6))
    return false;
  p.tick = tick;
  p.stride = 60;
  p.session = QString("usb-%1").arg(session);
  if (flags & 128)
    p.session += QString("-gap-%1").arg(seq);
  auto &s = p.data;
  s.abi_version = 1;
  s.fresh = bool(flags & 1);
  s.main_seen = 1;
  if (soc <= 100) {
    s.soc = soc;
    s.known |= 1;
  }
  if (in != 65535) {
    s.input_w = in;
    s.known |= 2;
  }
  if (out != 65535) {
    s.output_w = out;
    s.known |= 4;
  }
  if (minutes != 65535) {
    s.runtime_s = minutes * 60;
    s.known |= 8;
  }
  if (s.fresh) {
    s.known |= 16 | 32 | 64;
    s.ac_output = bool(flags & 8);
    s.usb_output = bool(flags & 16);
    s.dc_output = bool(flags & 32);
    if (flags & 6) {
      s.source = flags & 2 ? 2 : 1;
      s.known |= 512;
    }
  }
  if (!(flags & 64))
    epoch = 0;
  return true;
}
} // namespace
UsbTransport::UsbTransport(QObject *parent, bool offline, UsbChannel *io, Ports enumerate, Now time)
    : TelemetryTransport(parent), channel(io), ports(std::move(enumerate)),
      elapsedMs(std::move(time)), offline_(offline) {
  clock.start();
  qRegisterMetaType<ap_snapshot>();
  qRegisterMetaType<HistoryPoints>();
  if (!elapsedMs)
    elapsedMs = [this] { return clock.elapsed(); };
  last.abi_version = 1;
  if (!ports)
    ports = allpowersUsbPorts;
  if (!channel && !offline)
    channel = nativeUsbChannel(this);
  if (channel) {
    channel->setParent(this);
    connect(channel, &UsbChannel::opened, this, [this] {
      if (!active_)
        return;
      opened_ = true;
      openedAt = elapsedMs();
      command("STATUS\n");
      emit stateChanged();
    });
    connect(channel, &UsbChannel::received, this, &UsbTransport::receive);
    connect(channel, &UsbChannel::failed, this, &UsbTransport::fail);
  }
  timer.setInterval(500);
  connect(&timer, &QTimer::timeout, this, &UsbTransport::tick);
  timer.start();
}
UsbTransport::~UsbTransport() {
  timer.stop();
  if (channel) {
    channel->disconnect(this);
    delete channel;
    channel = nullptr;
  }
}
QString UsbTransport::phase() const {
  if (!active_)
    return "USB отключён";
  if (!opened_)
    return error_.isEmpty() ? "Ожидание USB-платы" : "USB: " + error_ + " · повторное подключение";
  if (!verified_)
    return "Проверка прошивки USB-платы…";
  return station_.isEmpty() ? "USB подключён · выберите станцию кнопкой BOOT на плате"
         : !last.fresh ? "USB подключён · нет свежих данных станции"
                       : "USB подключён · получение данных";
}
QJsonObject UsbTransport::diagnostics() const {
  auto info = details_;
  info["transport"] = "USB CDC · Waveshare / Pocket";
  info["endpoint"] = serial_;
  info["usb_serial"] = serial_;
  info["usb_port"] = port_;
  info["usb_board"] = board_;
  info["usb_verified"] = verified_;
  info["usb_bad_lines"] = badLines;
  info["usb_history_busy"] = importing;
  info["usb_history_bad"] = historyBad;
  info["usb_history_undated"] = historyUndated;
  info["usb_diagnostics"] = boardDiagnostics;
  info["address"] = station_;
  info["name"] = name_;
  info["phase"] = phase();
  info["error"] = error_;
  info["active"] = active_;
  info["connected"] = connected();
  info["scanning"] = false;
  info["gatt_connected"] = status_["gatt_connected"];
  return info;
}
void UsbTransport::scan() {
  if (offline_ && !channel) {
    emit stateChanged();
    return;
  }
  const auto list = ports();
  for (const auto &p : list)
    emit found(p.serial, p.description, 0, 0);
  emit stateChanged();
}
void UsbTransport::resume(const QString &id, const QString &, int) {
  stop();
  serial_ = id;
  active_ = true;
  retryAt = 0;
  connectPort();
  emit stateChanged();
}
void UsbTransport::connectPort() {
  retryAt = elapsedMs() + 2000;
  if (!channel) {
    error_ = "Автономный тест: USB не используется";
    return;
  }
  for (const auto &p : ports())
    if (p.serial == serial_) {
      opened_ = verified_ = pending = knownPending = false;
      status_ = {};
      details_ = {};
      boardDiagnostics = {};
      station_.clear();
      name_.clear();
      board_.clear();
      frameAt = statusAt = -1;
      historyAttempted = importing = false;
      importPoints.clear();
      currentSession = anchorTick = 0;
      anchorWall = 0;
      port_ = p.port;
      error_.clear();
      channel->open(port_);
      return;
    }
  error_ = "Выбранная USB-плата не найдена";
}
void UsbTransport::stop() {
  active_ = opened_ = verified_ = pending = importing = historyAttempted = false;
  if (channel)
    channel->close();
  buffer.clear();
  status_ = {};
  details_ = {};
  boardDiagnostics = {};
  station_.clear();
  name_.clear();
  board_.clear();
  statusAt = frameAt = -1;
  lastFrames = 0;
  anchorWall = 0;
  anchorTick = currentSession = 0;
  importPoints.clear();
  last = {};
  last.abi_version = 1;
  emit snapshot(last);
  emit stateChanged();
}
void UsbTransport::fail(QString error) {
  if (!active_)
    return;
  if (channel)
    channel->close();
  opened_ = verified_ = pending = importing = false;
  buffer.clear();
  last.fresh = 0;
  error_ = std::move(error);
  retryAt = elapsedMs() + 2000;
  emit snapshot(last);
  emit stateChanged();
}
void UsbTransport::command(const QByteArray &data) {
  // A strict read-only allow-list. No boot/restart, station selection, clock,
  // screen or firmware commands can be sent through this transport.
  if (!channel || !opened_)
    return;
  if (data == "STATUS\n" || data == "DIAG\n" ||
      (importing && data.startsWith("HISTORY-READ ") && data.size() < 40))
    channel->write(data);
}
void UsbTransport::tick() {
  if (!active_)
    return;
  auto now = elapsedMs();
  if (!opened_) {
    if (now >= retryAt)
      connectPort();
    return;
  }
  if ((statusAt < 0 && now - openedAt > 5000) || (statusAt >= 0 && now - statusAt > 6000)) {
    fail("Нет корректного ответа прошивки по USB");
    return;
  }
  if (verified_) {
    if (importing && now >= importDeadline) {
      importing = false;
      importPoints.clear();
      details_["usb_history_error"] = "Время ожидания архива истекло; можно повторить чтение";
      emit logReceived(details_["usb_history_error"].toString());
      emit stateChanged();
    }
    auto copy = last;
    const auto age = frameAt < 0 ? 15000 : qMin<qint64>(UINT32_MAX, now - frameAt);
    copy.main_age_ms = copy.settings_age_ms = quint32(age);
    if (age >= 15000 || now - statusAt > 2500)
      copy.fresh = 0;
    emit snapshot(copy);
    if (now - diagAt >= 5000) {
      diagAt = now;
      command("DIAG\n");
    }
    if (!historyAttempted && details_["history_ready"].toBool() && !station_.isEmpty())
      requestHistory();
  }
}
void UsbTransport::receive(const QByteArray &bytes) {
  if (!active_ || !opened_)
    return;
  if (bytes.size() > 65536 || buffer.size() + bytes.size() > 65536) {
    badLines++;
    buffer.clear();
    return;
  }
  buffer += bytes;
  for (int end; (end = buffer.indexOf('\n')) >= 0;) {
    auto line = buffer.left(end).trimmed();
    buffer.remove(0, end + 1);
    if (line.isEmpty())
      continue;
    if (line.size() > 8192) {
      badLines++;
      continue;
    }
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
      badLines++;
      continue;
    }
    row(doc.object());
  }
  if (buffer.size() > 8192) {
    buffer.clear();
    badLines++;
  }
}
void UsbTransport::row(const QJsonObject &o) {
  if (o.contains("adapter")) {
    pending = knownPending = false;
    if (o["adapter"] != "UPSDEV" || !o["version"].isString() || !o["fresh"].isBool() ||
        !o["gatt_connected"].isBool()) {
      badLines++;
      return;
    }
    quint32 soc, in, out, runtime, raw, valid, invalid;
    if (!number(o, "soc", soc, 100) || !number(o, "charge_w", in, 65535) ||
        !number(o, "output_w", out, 65535) || !number(o, "runtime_s", runtime) ||
        !number(o, "source_raw", raw, 65535) || !number(o, "valid_frames", valid) ||
        !number(o, "invalid_frames", invalid)) {
      badLines++;
      return;
    }
    status_ = o;
    pending = true;
    knownPending = false;
  } else if (o.contains("station_address")) {
    if (!pending)
      return;
    auto address = QBluetoothAddress(o["station_address"].toString());
    if (address.isNull()) {
      if (!o["station_address"].toString().isEmpty()) {
        pending = false;
        badLines++;
      }
      return;
    }
    auto id = address.toString();
    if (id != station_) {
      station_ = id;
      historyAttempted = false;
      importing = false;
      importPoints.clear();
      emit newSession();
      emit stateChanged();
    }
    name_ = o["station_name"].toString().left(128);
  } else if (o.contains("telemetry_known")) {
    quint32 known, protocol;
    if (!pending || !number(o, "telemetry_known", known, 4095) ||
        !number(o, "protocol", protocol, 3)) {
      badLines++;
      return;
    }
    details_["telemetry_known"] = double(known);
    details_["protocol"] = int(protocol);
    knownPending = true;
    details_["ups_source_known"] = o["ups_source_known"].toBool();
    if (verified_)
      publish();
  } else if (o.contains("board")) {
    const auto board = o["board"].toString();
    if (board != "waveshare-147" && board != "pocket-096") {
      fail("Неизвестный профиль USB-платы");
      return;
    }
    board_ = board;
    verified_ = !status_.isEmpty() && knownPending;
    if (pending && knownPending)
      publish();
    emit stateChanged();
  } else if (o.contains("diagnostic") && o["diagnostic"].toBool()) {
    auto rows = o["rows"].toArray();
    if (rows.size() > 64) {
      badLines++;
      return;
    }
    boardDiagnostics = {};
    for (const auto &r : rows) {
      auto item = r.toObject();
      auto label = item["label"].toString();
      if (!label.isEmpty() && label.size() <= 64)
        boardDiagnostics[label] = item["value"].toString().left(256);
    }
    emit stateChanged();
  } else if (o.contains("history_ready")) {
    quint32 session, count, latest;
    if (!number(o, "history_session", session) || !number(o, "history_records", count, 2880) ||
        !number(o, "history_latest", latest)) {
      badLines++;
      return;
    }
    if (currentSession && session != currentSession) {
      historyAttempted = false;
      emit newSession();
    }
    currentSession = session;
    details_["history_ready"] = o["history_ready"].toBool();
    details_["history_records"] = int(count);
    details_["history_latest"] = double(latest);
    details_["history_session"] = double(session);
    details_["board_history_clock"] = o["history_clock"];
  } else if (o.contains("graph_live_latest_s")) {
    quint32 tick;
    if (number(o, "graph_live_latest_s", tick)) {
      anchorTick = tick;
      anchorWall = QDateTime::currentSecsSinceEpoch();
    }
  } else if (o["history_record"].toBool() && importing) {
    if (importPoints.size() >= 2880) {
      historyBad++;
      return;
    }
    HistoryPoint p;
    quint32 session, epoch;
    if (!historyRecord(o, p, session, epoch)) {
      historyBad++;
      return;
    }
    // The existing flash record has no station identity. Keep its archive
    // separate from MAC-keyed station history rather than guessing its owner.
    p.address = "usb-board:" + serial_;
    if (epoch >= 1577836800u)
      p.wall = epoch;
    else if (session == currentSession && anchorWall && p.tick <= anchorTick)
      p.wall = anchorWall - (anchorTick - p.tick);
    else {
      historyUndated++;
      return;
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (p.wall > now + 2 || p.wall < now - 172800) {
      historyUndated++;
      return;
    }
    importPoints.append(p);
  } else if (o["history_dump_done"].toBool() && importing) {
    importing = false;
    details_["usb_history_imported"] = importPoints.size();
    emit historyImported(importPoints);
    importPoints.clear();
    emit stateChanged();
    emit logReceived(QString("История USB: %1 точек; без даты %2; повреждённых %3")
                         .arg(details_["usb_history_imported"].toInt())
                         .arg(historyUndated)
                         .arg(historyBad));
  } else {
    for (auto it = o.begin(); it != o.end(); ++it)
      if ((it.key().startsWith("hid_") || it.key().startsWith("ble_") ||
           it.key().startsWith("heap_") || it.key() == "psram_free" ||
           it.key() == "reset_reason") &&
          it.value().isDouble())
        details_[it.key()] = it.value();
  }
}
void UsbTransport::publish() {
  if (!pending || !verified_ || !knownPending)
    return;
  pending = false;
  auto now = elapsedMs();
  last = {};
  last.abi_version = 1;
  last.model = details_["protocol"].toInt();
  last.known = details_["telemetry_known"].toInt();
  last.soc = status_["soc"].toInt();
  last.input_w = status_["charge_w"].toInt();
  last.output_w = status_["output_w"].toInt();
  last.runtime_s = quint32(status_["runtime_s"].toDouble());
  // STATUS exposes runtime from the HID UPS report, which may be retained when
  // the UPS profile is unavailable. Do not present that cached value as current.
  if (last.model != 3 || !details_["ups_source_known"].toBool() || last.runtime_s >= 0x80000000u)
    last.known &= ~quint32(AllpowersModels::RuntimeMinutes);
  last.source_raw = status_["source_raw"].toInt();
  last.valid_frames = quint32(status_["valid_frames"].toDouble());
  last.invalid_frames = quint32(status_["invalid_frames"].toDouble());
  auto source = status_["source"].toString();
  last.source = source == "mains" ? 2 : source == "battery" ? 1 : 0;
  if (last.model == 3 && !details_["ups_source_known"].toBool())
    last.source = 0;
  if (!last.source)
    last.known &= ~quint32(AllpowersModels::MainsSource);
  last.ac_output = status_["ac_output"].toBool();
  last.usb_output = status_["usb_output"].toBool();
  last.dc_output = status_["dc_output"].toBool();
  if (!status_["ac_output"].isBool())
    last.known &= ~quint32(AllpowersModels::AcOutput);
  if (!status_["usb_output"].isBool())
    last.known &= ~quint32(AllpowersModels::UsbOutput);
  if (!status_["dc_output"].isBool())
    last.known &= ~quint32(AllpowersModels::DcOutput);
  last.main_seen = last.valid_frames > 0;
  last.settings_seen = bool(last.known & (512 | 16 | 32 | 64));
  last.fresh =
      status_["fresh"].toBool() && status_["gatt_connected"].toBool() && !station_.isEmpty();
  if (frameAt < 0 || lastFrames != last.valid_frames) {
    frameAt = now;
    lastFrames = last.valid_frames;
  }
  if (now - frameAt >= 15000)
    last.fresh = 0;
  last.main_age_ms = last.settings_age_ms = quint32(qMin<qint64>(UINT32_MAX, now - frameAt));
  statusAt = now;
  error_.clear();
  emit snapshot(last);
  emit stateChanged();
}
void UsbTransport::requestHistory() {
  if (!opened_ || !verified_ || importing || !details_["history_ready"].toBool())
    return;
  historyAttempted = true;
  historyBad = historyUndated = 0;
  importPoints.clear();
  quint32 count = details_["history_records"].toInt(),
          latest = quint32(details_["history_latest"].toDouble());
  if (!count || !latest)
    return;
  quint32 first = latest;
  for (quint32 i = 1; i < count; ++i)
    first = first == 1 ? UINT32_MAX : first - 1;
  importing = true;
  importDeadline = elapsedMs() + 60000;
  details_.remove("usb_history_error");
  command("HISTORY-READ " + QByteArray::number(first) + " " + QByteArray::number(count) + "\n");
  emit stateChanged();
}
