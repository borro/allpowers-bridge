#include "usb_transport.h"
#include "../../firmware/R2500Adapter/HistoryOperation.h"
#include "../../firmware/R2500Adapter/HistoryWire.h"
#include "AllpowersModels.h"
#include <QBluetoothAddress>
#include <QCoreApplication>
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
  quint32 version = 1, known = 0, model = 0;
  if (o.contains("record_version") && !number(o, "record_version", version, 2))
    return false;
  if (version != 1 && version != 2)
    return false;
  if (version == 2 && (!number(o, "known", known, 4095) || !number(o, "protocol", model, 3)))
    return false;
  if (!number(o, "sequence", seq) || !seq || !number(o, "session", session) ||
      !number(o, "uptime_s", tick) || !number(o, "epoch", epoch) || !number(o, "soc", soc, 255) ||
      !number(o, "charge_w", in, 65535) || !number(o, "output_w", out, 65535) ||
      !number(o, "runtime_minutes", minutes, 65535) || !number(o, "flags", flags, 255) ||
      !number(o, "crc", check))
    return false;
  QByteArray bytes(28, 0);
  auto *d = reinterpret_cast<uchar *>(bytes.data());
  qToLittleEndian<quint32>(
      version == 2 ? HistoryFormat::header(known, quint8(model)) : HistoryFormat::kMagic, d);
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
  s.model = model;
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
    // AHP1 never recorded output known bits. Do not turn unknown into OFF.
    if (version == 2)
      s.known |= known & (16 | 32 | 64);
    s.ac_output = bool(flags & 8);
    s.usb_output = bool(flags & 16);
    s.dc_output = bool(flags & 32);
    if (flags & 6) {
      s.source = flags & 2 ? 2 : 1;
      s.known |= 512;
    }
  }
  if (version == 2)
    s.known &= known;
  if (!(flags & 64))
    epoch = 0;
  return true;
}
} // namespace
UsbTransport::UsbTransport(QObject *parent, bool offline, UsbChannel *io, Ports enumerate, Now time,
                           HistoryOperationIds::Entropy entropy)
    : TelemetryTransport(parent), channel(io), ports(std::move(enumerate)),
      elapsedMs(std::move(time)), operationEntropy(std::move(entropy)), offline_(offline) {
  clock.start();
  qRegisterMetaType<ap_snapshot>();
  qRegisterMetaType<HistoryPoints>();
  if (!elapsedMs)
    elapsedMs = [this] { return clock.elapsed(); };
  if (!operationEntropy)
    operationEntropy = HistoryOperationIds::random128;
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
    return QCoreApplication::translate("Allpowers", "USB disconnected");
  if (!opened_)
    return error_.isEmpty()
               ? QCoreApplication::translate("Allpowers", "Waiting for USB board")
               : "USB: " + error_ + QCoreApplication::translate("Allpowers", " · reconnecting");
  if (!verified_)
    return QCoreApplication::translate("Allpowers", "Verifying USB board firmware…");
  return station_.isEmpty()
             ? QCoreApplication::translate(
                   "Allpowers", "USB connected · select a station with the board's BOOT button")
         : !last.fresh
             ? QCoreApplication::translate("Allpowers", "USB connected · no fresh station data")
             : QCoreApplication::translate("Allpowers", "USB connected · receiving data");
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
    error_ = QCoreApplication::translate("Allpowers", "Offline test: USB is not used");
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
  error_ = QCoreApplication::translate("Allpowers", "Selected USB board not found");
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
  opened_ = verified_ = pending = false;
  if (importing)
    finishImport("error", QCoreApplication::translate(
                              "Allpowers", "USB connection interrupted while reading archive"));
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
      (importing && (data.startsWith("HISTORY-READ ") || data.startsWith("HISTORY-EXPORT ")) &&
       data.size() < HistoryFormat::kHistoryCommandCapacity))
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
  if (!importing &&
      ((statusAt < 0 && now - openedAt > 5000) || (statusAt >= 0 && now - statusAt > 6000))) {
    fail(QCoreApplication::translate("Allpowers", "No valid firmware response over USB"));
    return;
  }
  if (verified_) {
    if (importing && now >= importDeadline)
      finishImport("error", QCoreApplication::translate(
                                "Allpowers", "Archive timed out; data may be incomplete"));
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
    if (importing && (importVersion != 3 || importBegun))
      importCompromised = true;
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
      if (importing && (importVersion != 3 || importBegun))
        importCompromised = true;
      continue;
    }
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
      badLines++;
      if (importing && (importVersion != 3 || importBegun))
        importCompromised = true;
      continue;
    }
    row(doc.object());
  }
  if (buffer.size() > 8192) {
    buffer.clear();
    badLines++;
    if (importing && (importVersion != 3 || importBegun))
      importCompromised = true;
  }
}
void UsbTransport::row(const QJsonObject &o) {
  const bool operationFrame = o.contains("history_export_begin") || o.contains("history_record") ||
                              o.contains("history_dump_done");
  if (importing && importVersion == 3 && importNonceRequired && operationFrame) {
    const auto nonce = o["operation_nonce"].toString().toLatin1();
    // Correlate the operation before interpreting any metadata or errors.
    // STATUS updates cannot change this requirement while an import is active.
    if (!o["operation_nonce"].isString() ||
        !HistoryFormat::validOperationNonce(nonce.constData(), size_t(nonce.size())) ||
        nonce != importNonce)
      return;
  }
  // Versioned frames identify their operation independently of record contents.
  // Delayed complete frames from another request (or a previous boot after our
  // authoritative begin) must not finish or damage the current transaction.
  if (importing && importVersion == 3 && !importNonceRequired &&
      (o["history_export_begin"].toBool() || o["history_record"].toBool() ||
       o["history_dump_done"].toBool())) {
    quint32 version, request, session, generation;
    if (number(o, "history_export_version", version, 3) && version == 3 &&
        number(o, "request", request) && request && request != importRequest)
      return;
    if (number(o, "history_export_version", version, 3) && version == 3 &&
        number(o, "request", request) && request && number(o, "export_session", session) &&
        session && number(o, "generation", generation) && importBegun && session != importSession)
      return;
  }
  if (o.contains("product_version")) {
    for (const char *key :
         {"product_version", "product_commit", "firmware_protocol_version", "board_identity"})
      if (o[key].isString())
        details_[key] = o[key].toString().left(128);
    details_["product_dirty"] = o["product_dirty"].toBool();
  } else if (o.contains("ble_task_started")) {
    for (const char *key : {"ble_task_started", "ble_startup_failed", "history_task_started",
                            "startup_maintenance_available"})
      if (o[key].isBool())
        details_[key] = o[key];
  } else if (o.contains("adapter")) {
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
      if (importing)
        finishImport("partial", QCoreApplication::translate(
                                    "Allpowers", "Station changed while reading USB archive"));
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
      fail(QCoreApplication::translate("Allpowers", "Unknown USB board profile"));
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
  } else if (o["history_export_begin"].toBool() && importing) {
    quint32 version, request, session, generation, count, oldest, latest;
    // Before our authoritative begin, complete frames from a timed-out prior
    // request are channel residue rather than records of the new transaction.
    if (importVersion == 3 && !importNonceRequired && !importBegun &&
        (!number(o, "request", request) || request != importRequest))
      return;
    if (importVersion != 3 || importBegun || !number(o, "history_export_version", version, 3) ||
        version != 3 || !number(o, "request", request) || request != importRequest ||
        !number(o, "export_session", session) || !session || session != importSession ||
        !number(o, "generation", generation) || !number(o, "count", count, 2880) ||
        !number(o, "oldest", oldest) || !number(o, "latest", latest) ||
        (count ? (!oldest || !latest) : (oldest || latest))) {
      finishImport("error", QCoreApplication::translate("Allpowers", "Invalid USB archive start"));
      return;
    }
    importBegun = true;
    importCompromised = false;
    importGeneration = generation;
    importExpected = count;
    importOldest = oldest;
    importLatest = latest;
    details_["usb_history_frozen"] = true;
  } else if (o.contains("history_ready")) {
    quint32 session = 0, count = 0, latest = 0, oldestInventory = 0;
    const bool ready = o["history_ready"].toBool();
    if (!o["history_ready"].isBool() || !number(o, "history_session", session) ||
        !number(o, "history_records", count, 2880) || !number(o, "history_latest", latest) ||
        (ready && (!session || ((count == 0) != (latest == 0)))) ||
        (ready && o.contains("history_oldest") &&
         (!number(o, "history_oldest", oldestInventory) ||
          ((count == 0) != (oldestInventory == 0))))) {
      badLines++;
      details_["history_ready"] = false;
      const QString reason = QCoreApplication::translate("Allpowers", "Invalid USB archive state");
      if (importing)
        finishImport("error", reason);
      else {
        details_["usb_history_result"] = "error";
        details_["usb_history_complete"] = false;
        details_["usb_history_error"] = reason;
        emit stateChanged();
      }
      return;
    }
    if (importing && !o["history_ready"].toBool())
      finishImport("error", QCoreApplication::translate("Allpowers",
                                                        "Board archive unavailable during read"));
    else if (importing &&
             (session != importSession ||
              (importVersion != 3 && (count != importExpected || latest != importLatest))))
      finishImport("partial", QCoreApplication::translate(
                                  "Allpowers", "Archive changed during read; import again"));
    else if (importing && legacyVerification) {
      finishImport(importCompromised ? "partial" : "complete",
                   importCompromised ? QCoreApplication::translate(
                                           "Allpowers", "Some USB archive rows are corrupt")
                                     : QString{});
    }
    if (currentSession && session != currentSession) {
      historyAttempted = false;
      emit newSession();
    }
    currentSession = session;
    details_["history_ready"] = o["history_ready"].toBool();
    details_["history_records"] = int(count);
    details_["history_latest"] = double(latest);
    quint32 oldest, exportVersion;
    if (number(o, "history_oldest", oldest) &&
        number(o, "history_export_version", exportVersion, 3) && exportVersion >= 2) {
      details_["history_oldest"] = double(oldest);
      details_["history_export_version"] = int(exportVersion);
      quint32 generation;
      if (number(o, "history_generation", generation))
        details_["history_generation"] = double(generation);
    } else {
      details_.remove("history_oldest");
      details_.remove("history_export_version");
    }
    details_["history_session"] = double(session);
    if (o.contains("history_nonce_bits"))
      details_["history_nonce_bits"] = o["history_nonce_bits"];
    else
      details_.remove("history_nonce_bits");
    details_["board_history_clock"] = o["history_clock"];
    if (o["history_error"].isString())
      details_["board_history_error"] = o["history_error"].toString().left(128);
  } else if (o.contains("graph_live_latest_s")) {
    quint32 tick;
    if (number(o, "graph_live_latest_s", tick)) {
      anchorTick = tick;
      anchorWall = QDateTime::currentSecsSinceEpoch();
    }
  } else if (o["history_record"].toBool() && importing) {
    if (importVersion == 3 && !importBegun)
      return;
    HistoryPoint p;
    quint32 session, epoch;
    if (!historyRecord(o, p, session, epoch)) {
      historyBad++;
      importCompromised = true;
      return;
    }
    quint32 sequence = 0, crc = 0;
    number(o, "sequence", sequence);
    number(o, "crc", crc);
    if (importVersion == 3) {
      quint32 version, request, exportSession, generation, ordinal;
      if (!importBegun || !number(o, "history_export_version", version, 3) || version != 3 ||
          !number(o, "request", request) || request != importRequest ||
          !number(o, "export_session", exportSession) || exportSession != importSession ||
          !number(o, "generation", generation) || generation != importGeneration ||
          !number(o, "ordinal", ordinal, 2879) || ordinal >= importExpected) {
        historyBad++;
        importCompromised = true;
        return;
      }
      if (ordinal != quint32(importSequences.size()))
        importCompromised = true;
      if ((importCursor && !HistoryFormat::newer(sequence, importCursor)) ||
          HistoryFormat::newer(sequence, importLatest)) {
        historyBad++;
        importCompromised = true;
        return;
      }
      if (!importCursor && sequence != importOldest)
        importCompromised = true;
    } else if (importVersion == 0 &&
               HistoryFormat::sequenceDistance(legacyFirst, sequence) >= legacySpan) {
      historyBad++;
      importCompromised = true;
      return;
    }
    if (importSequences.contains(sequence))
      return;
    if (importSequences.size() >= 2880) {
      historyBad++;
      importCompromised = true;
      return;
    }
    importSequences.insert(sequence);
    importCursor = sequence;
    if (importVersion == 0)
      ++legacyReceivedPage;
    HistoryFormat::Record digestRow{};
    digestRow.sequence = sequence;
    digestRow.crc = crc;
    importDigest = HistoryFormat::digestRecord(importDigest, digestRow);
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
    if (importVersion == 3 && !importNonceRequired && !importBegun) {
      quint32 previousRequest;
      if (number(o, "request", previousRequest) && previousRequest != importRequest)
        return;
    }
    if (importVersion != 3 && o.contains("history_legacy_status")) {
      quint32 sent = 0;
      if (o["history_legacy_status"] != "complete") {
        finishImport(o["history_legacy_status"] == "error" ? "error" : "partial",
                     QCoreApplication::translate("Allpowers",
                                                 "Firmware did not finish reading archive page"));
        return;
      }
      if (importVersion == 0 &&
          (!number(o, "history_sent", sent, 2880) || sent != legacyReceivedPage))
        importCompromised = true;
    }
    if (importVersion == 3) {
      quint32 version, request, session, generation, count, sent, cursor, digest, oldest, latest;
      const bool valid =
          importBegun && number(o, "history_export_version", version, 3) && version == 3 &&
          number(o, "request", request) && request == importRequest &&
          number(o, "export_session", session) && session == importSession &&
          number(o, "generation", generation) && generation == importGeneration &&
          number(o, "count", count, 2880) && count == importExpected &&
          number(o, "sent", sent, 2880) && sent == quint32(importSequences.size()) &&
          number(o, "cursor", cursor) && cursor == importCursor && number(o, "digest", digest) &&
          digest == importDigest && number(o, "oldest", oldest) && oldest == importOldest &&
          number(o, "latest", latest) && latest == importLatest;
      if (o["status"] == "error")
        finishImport("error", QCoreApplication::translate(
                                  "Allpowers", "Firmware could not finish reading archive"));
      else if (!valid || importCompromised || o["status"] != "complete" ||
               quint32(importSequences.size()) != importExpected ||
               (importExpected && importCursor != importLatest))
        finishImport("partial",
                     QCoreApplication::translate(
                         "Allpowers", "USB archive integrity or completeness unconfirmed"));
      else
        finishImport("complete");
    } else if (importVersion == 2) {
      finishImport("partial",
                   QCoreApplication::translate(
                       "Allpowers", "Legacy export does not confirm archive completeness"));
    } else if (quint32(importSequences.size()) > importExpected) {
      finishImport("partial", QCoreApplication::translate(
                                  "Allpowers", "Legacy archive record count changed during read"));
    } else if (quint32(importSequences.size()) == importExpected &&
               importSequences.contains(importLatest)) {
      // The legacy completion has no session, count or checksum. Verify the
      // advertised inventory again before calling the import complete.
      legacyVerification = true;
      command("STATUS\n");
    } else
      nextLegacyPage();
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
void UsbTransport::finishImport(const QString &result, const QString &reason) {
  importing = false;
  legacyVerification = false;
  details_["usb_history_result"] = result;
  details_["usb_history_complete"] = result == "complete";
  details_["usb_history_imported"] = importPoints.size();
  details_["usb_history_received"] = importSequences.size();
  details_["usb_history_pages"] = int(legacyPages);
  if (reason.isEmpty())
    details_.remove("usb_history_error");
  else
    details_["usb_history_error"] = reason;
  emit historyImported(importPoints);
  importPoints.clear();
  emit stateChanged();
  emit logReceived(
      QCoreApplication::translate("Allpowers", "USB history: %1 points; %2; undated %3; corrupt %4")
          .arg(details_["usb_history_imported"].toInt())
          .arg(result)
          .arg(historyUndated)
          .arg(historyBad));
  command("STATUS\n");
}
void UsbTransport::nextLegacyPage() {
  // Legacy boot recovery scans kKeep sequence positions. The first append may
  // skip at most one 128-slot erase sector, then successful appends are
  // contiguous; a failed append disables recording until reboot. A recovered
  // RAM record can survive <kKeep more appends: 2*kKeep+128 is conservative.
  constexpr quint32 maxSpan = 2 * 2880 + 128;
  if (legacyScanned >= maxSpan) {
    finishImport(
        "partial",
        QCoreApplication::translate(
            "Allpowers", "Legacy USB archive completeness unconfirmed within allowed range"));
    return;
  }
  if (legacyPages)
    legacyFirst = HistoryFormat::previous(legacyFirst);
  else
    legacyFirst = importLatest;
  legacySpan = qMin<quint32>(2880, maxSpan - legacyScanned);
  legacyScanned += legacySpan;
  legacyReceivedPage = 0;
  for (quint32 i = 1; i < legacySpan; ++i)
    legacyFirst = HistoryFormat::previous(legacyFirst);
  ++legacyPages;
  details_["usb_history_pages"] = int(legacyPages);
  command("HISTORY-READ " + QByteArray::number(legacyFirst) + " " + QByteArray::number(legacySpan) +
          "\n");
}
void UsbTransport::requestHistory() {
  if (!opened_ || !verified_ || importing || !details_["history_ready"].toBool())
    return;
  historyAttempted = true;
  historyBad = historyUndated = 0;
  importPoints.clear();
  importSequences.clear();
  importBegun = importCompromised = legacyVerification = false;
  importCursor = 0;
  importDigest = HistoryFormat::kDigestSeed;
  legacyPages = legacyScanned = legacySpan = 0;
  importExpected = details_["history_records"].toInt();
  importLatest = quint32(details_["history_latest"].toDouble());
  importOldest = quint32(details_["history_oldest"].toDouble());
  importSession = quint32(details_["history_session"].toDouble());
  importGeneration = quint32(details_["history_generation"].toDouble());
  importVersion = details_["history_export_version"].toInt();
  importRequest = ++requestCounter;
  if (!importRequest)
    importRequest = ++requestCounter;
  importing = true;
  importDeadline = elapsedMs() + (importVersion == 3 ? 70000 : 120000);
  details_["usb_history_result"] = "reading";
  details_["usb_history_complete"] = false;
  details_["usb_history_frozen"] = false;
  details_.remove("usb_history_error");
  importNonceRequired = false;
  importNonce.clear();
  details_["usb_history_nonce_bits"] = 0;
  details_.remove("usb_history_operation_nonce");
  if (importVersion == 3) {
    quint32 bits = 0;
    if (details_.contains("history_nonce_bits")) {
      if (!number(details_, "history_nonce_bits", bits) ||
          bits != HistoryFormat::kHistoryNonceBits) {
        finishImport("error", QCoreApplication::translate(
                                  "Allpowers", "Unsupported USB archive operation ID protection"));
        return;
      }
      importNonceRequired = true;
    }
    const auto raw = operationEntropy();
    if (raw.size() != 16 || (!importNonceRequired && !HistoryOperationIds::fallbackRequest(raw))) {
      finishImport("error", QCoreApplication::translate(
                                "Allpowers", "Could not create USB archive operation ID"));
      return;
    }
    if (importNonceRequired)
      importNonce = raw.toHex();
    else
      importRequest = HistoryOperationIds::fallbackRequest(raw);
    details_["usb_history_nonce_bits"] =
        importNonceRequired ? int(HistoryFormat::kHistoryNonceBits) : 0;
    details_["usb_history_operation_nonce"] = QString::fromLatin1(importNonce);
    command("HISTORY-EXPORT " + QByteArray::number(importRequest) +
            (importNonceRequired ? " " + importNonce : QByteArray{}) + "\n");
  } else if (!importExpected) {
    if (importVersion == 2)
      finishImport("partial", QCoreApplication::translate(
                                  "Allpowers", "Legacy export does not confirm an empty archive"));
    else {
      importVersion = 0;
      legacyVerification = true;
      command("STATUS\n");
    }
  } else if (importVersion == 2 && importOldest)
    command("HISTORY-EXPORT " + QByteArray::number(importOldest) + " " +
            QByteArray::number(importExpected) + "\n");
  else {
    importVersion = 0;
    nextLegacyPage();
  }
  emit stateChanged();
}
