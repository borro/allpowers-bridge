#include "../../firmware/R2500Adapter/HidTelemetryServer.h"
#include "usb_hid.h"
#include <QDateTime>
#include <QJsonDocument>
#include <array>

using namespace HidTelemetry;
namespace {
QByteArray line(const QJsonObject &o) {
  return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}
const uint8_t *bytes(const QByteArray &b) {
  return reinterpret_cast<const uint8_t *>(b.constData());
}
} // namespace
struct HidSession::Impl {
  Callbacks cb;
  uint64_t boot = 0;
  qint64 clock = 0, metadataAt = 0;
  bool failed = false, livePublished = false;
  bool clockSupported = false, clockRequested = false, historyReady = false;
  QJsonObject identity;
  struct Pending {
    bool active = false;
    Request request;
    Assembly assembly;
    qint64 sent = 0;
    int retries = 0;
  };
  std::array<Pending, 4> pending;
  Assembly packets;
  Id archive;
  QByteArray archiveNonce;
  quint32 archiveRequest = 0, ordinal = 0, count = 0, cursor = 0;
  QJsonObject archiveFields;
  bool importing = false;

  void fail(QString text) {
    if (!failed) {
      failed = true;
      cb.failed(std::move(text));
    }
  }
  Id random() {
    Id id;
    const auto raw = cb.entropy();
    if (raw.size() == 16)
      memcpy(id.bytes, raw.constData(), 16);
    if (id.empty())
      fail(QStringLiteral("Could not create HID operation identity"));
    return id;
  }
  void send(Pending &p) {
    uint8_t report[64]{};
    report[0] = RequestReport;
    encode(p.request, report + 1);
    p.assembly.expect(p.request);
    p.sent = clock;
    cb.output(QByteArray(reinterpret_cast<const char *>(report), sizeof(report)));
  }
  void request(unsigned slot, Op op, Id target = {}, uint32_t position = 0, uint32_t epoch = 0,
               uint32_t uptime = 0) {
    auto &p = pending[slot];
    p.request = {op, random(), target, boot, position, epoch, uptime};
    if (failed)
      return;
    p.active = true;
    p.retries = 0;
    send(p);
  }
  void archiveEnd(bool ok) {
    auto done = archiveFields;
    done["history_dump_done"] = true;
    done["sent"] = double(ordinal);
    done["cursor"] = double(cursor);
    done["status"] = ok ? "complete" : "error";
    cb.received(line(done));
    importing = false;
    request(2, Op::Close, archive);
  }
  void response(unsigned slot, Result result, const QByteArray &body) {
    auto &p = pending[slot];
    const auto op = p.request.op;
    if (result == Result::Busy && p.retries < 3) {
      // Retain the same operation ID and retry at the normal deadline.
      p.sent = clock;
      return;
    }
    p.active = false;
    if (op == Op::Clock) {
      clockRequested = result == Result::Ok;
      cb.received(line({{"history_clock_sync_ok", result == Result::Ok}}));
      return;
    }
    if (result != Result::Ok) {
      if (slot == 2 && importing)
        archiveEnd(false);
      else if (op == Op::Metadata)
        fail(QStringLiteral("HID metadata unavailable"));
      return;
    }
    const auto *b = bytes(body);
    if (op == Op::Metadata || op == Op::Diagnostics) {
      QByteArray forwarded;
      for (const auto &row : body.split('\n')) {
        if (row.isEmpty())
          continue;
        auto doc = QJsonDocument::fromJson(row);
        if (!doc.isObject()) {
          fail(QStringLiteral("Invalid HID metadata"));
          return;
        }
        const auto object = doc.object();
        if (object.contains("history_ready"))
          historyReady = object["history_ready"].toBool();
        if (object.contains("station_name"))
          identity = object;
        // Initial STATUS rows bootstrap board verification. After the first
        // Feature snapshot, only Live may publish telemetry or station changes;
        // legacy metadata carries UPS-retained runtime and has no firmware ages.
        if (op == Op::Metadata && livePublished &&
            (object.contains("adapter") || object.contains("station_address") ||
             object.contains("telemetry_known")))
          continue;
        forwarded += row + '\n';
      }
      if (!forwarded.isEmpty())
        cb.received(forwarded);
      return;
    }
    if (op == Op::Open) {
      if (body.size() != 24 || get32(b + 8) > 2880 || !get32(b)) {
        archiveEnd(false);
        return;
      }
      ordinal = cursor = 0;
      count = get32(b + 8);
      archiveFields = {{"history_export_version", 3},
                       {"request", double(archiveRequest)},
                       {"operation_nonce", QString::fromLatin1(archiveNonce)},
                       {"export_session", double(get32(b))},
                       {"generation", double(get32(b + 4))},
                       {"count", double(count)},
                       {"oldest", double(get32(b + 12))},
                       {"latest", double(get32(b + 16))},
                       {"digest", double(get32(b + 20))}};
      auto begin = archiveFields;
      begin["history_export_begin"] = true;
      cb.received(line(begin));
      if (!count)
        archiveEnd(true);
      else
        request(2, Op::Page, archive, ordinal);
    } else if (op == Op::Page) {
      const quint32 expected = qMin<quint32>(8, count - ordinal);
      if (body.size() != 4 + int(expected) * 32 || get32(b) != ordinal) {
        archiveEnd(false);
        return;
      }
      for (quint32 i = 0; i < expected; ++i) {
        HistoryFormat::Record record{};
        if (!decodeRecord(b + 4 + i * 32, record)) {
          archiveEnd(false);
          return;
        }
        char json[768]{};
        const int length = HistoryFormat::serialize(record, json, sizeof(json));
        if (length <= 0 || length >= int(sizeof(json))) {
          archiveEnd(false);
          return;
        }
        auto row = QJsonDocument::fromJson(QByteArray(json, length)).object();
        for (auto it = archiveFields.begin(); it != archiveFields.end(); ++it)
          row[it.key()] = it.value();
        row["ordinal"] = double(ordinal++);
        cursor = record.sequence;
        cb.received(line(row));
      }
      if (ordinal == count)
        archiveEnd(true);
      else
        request(2, Op::Page, archive, ordinal);
    }
  }
};
HidSession::HidSession(Callbacks cb) : impl(std::make_unique<Impl>()) {
  impl->cb = std::move(cb);
}
HidSession::~HidSession() = default;
void HidSession::stop() {
  auto &d = *impl;
  if (d.importing && !d.failed)
    d.request(2, Op::Close, d.archive);
  d.importing = false;
}
bool HidSession::start(const QByteArray &caps, qint64 now) {
  auto &d = *impl;
  const auto *p = bytes(caps);
  if (!sealed(p, size_t(caps.size())) || memcmp(p, "APHID1", 6) || p[14] != Version || p[15] != 2 ||
      p[16] != 8 || get16(p + 17) != 2880 || get16(p + 19) != MessageLimit || !get64(p + 6)) {
    d.fail(QStringLiteral("Unsupported HID telemetry firmware"));
    return false;
  }
  d.boot = get64(p + 6);
  d.clockSupported = (p[29] & 1) != 0;
  d.clock = d.metadataAt = now;
  Request event;
  event.boot = d.boot;
  event.op = Op::Packet;
  d.packets.expect(event);
  d.request(0, Op::Metadata);
  return !d.failed;
}
void HidSession::live(const QByteArray &report) {
  auto &d = *impl;
  Live value;
  if (d.failed)
    return;
  if (!decode(bytes(report), size_t(report.size()), value) || value.boot != d.boot) {
    d.fail(QStringLiteral("HID telemetry changed or is corrupt; reconnecting"));
    return;
  }
  if (d.identity.isEmpty())
    return; // Initial metadata precedes live rows.
  if (d.clockSupported && d.historyReady && !d.clockRequested) {
    const auto epoch = d.cb.utcNow ? d.cb.utcNow() : QDateTime::currentSecsSinceEpoch();
    if (epoch >= 1577836800 && epoch <= UINT32_MAX) {
      d.clockRequested = true;
      d.request(3, Op::Clock, {}, 0, uint32_t(epoch), value.uptimeSeconds);
    }
  }
  QString address;
  for (auto n : value.address) {
    if (!address.isEmpty())
      address += ':';
    address += QString("%1").arg(n, 2, 16, QChar('0'));
  }
  if (address == "00:00:00:00:00:00")
    address.clear();
  auto identity = d.identity;
  identity["station_address"] = address.toUpper();
  if (address.compare(d.identity["station_address"].toString(), Qt::CaseInsensitive) != 0)
    identity["station_name"] = QString{};
  QJsonObject status{{"adapter", "UPSDEV"},
                     {"version", QString(QChar(value.firmware))},
                     {"hid_live", true},
                     {"board_uptime_s", double(value.uptimeSeconds)},
                     {"gatt_connected", bool(value.flags & Connected)},
                     {"fresh", bool(value.flags & Fresh)},
                     {"soc", value.soc},
                     {"charge_w", value.input},
                     {"output_w", value.output},
                     {"runtime_s", double(value.runtime)},
                     {"source_raw", value.sourceRaw},
                     {"source", value.source == 2   ? "mains"
                                : value.source == 1 ? "battery"
                                                    : "unknown"},
                     {"valid_frames", double(value.valid)},
                     {"invalid_frames", double(value.invalid)},
                     {"ac_output", bool(value.flags & AcOn)},
                     {"usb_output", bool(value.flags & UsbOn)},
                     {"dc_output", bool(value.flags & DcOn)},
                     {"ups_enabled", bool(value.flags & UpsEnabled)},
                     {"ups_status", value.ups},
                     {"main_seen", bool(value.flags & MainSeen)},
                     {"settings_seen", bool(value.flags & SettingsSeen)},
                     {"main_age_ms", double(value.mainAge)},
                     {"settings_age_ms", double(value.settingsAge)}};
  QJsonObject known{{"telemetry_known", double(value.known)},
                    {"protocol", value.model},
                    {"ups_source_known", value.model == 3 && bool(value.flags & Fresh)}};
  d.livePublished = true;
  d.cb.received(line(status) + line(identity) + line(known));
}
void HidSession::input(const QByteArray &report, qint64 now) {
  auto &d = *impl;
  if (d.failed || report.size() != 64 || uint8_t(report[0]) != ResponseReport)
    return;
  d.clock = now;
  const auto *p = bytes(report) + 1;
  if (d.packets.accept(p, Payload) == 1)
    d.cb.received(
        QByteArray(reinterpret_cast<const char *>(d.packets.bytes), int(d.packets.size())));
  for (unsigned i = 0; i < d.pending.size(); ++i) {
    auto &pending = d.pending[i];
    if (!pending.active)
      continue;
    const int result = pending.assembly.accept(p, Payload);
    if (result == 1) {
      auto body = QByteArray(reinterpret_cast<const char *>(pending.assembly.bytes),
                             int(pending.assembly.size()));
      d.response(i, pending.assembly.status(), body);
      break;
    }
    // A lost fragment is retried with the same identity by tick(). Other clients
    // continue receiving their own reports; no shared read cursor is consumed.
  }
}
void HidSession::command(const QByteArray &command, qint64 now) {
  auto &d = *impl;
  if (d.failed || !d.boot)
    return;
  d.clock = now;
  if (command == "DIAG\n" && !d.pending[1].active)
    d.request(1, Op::Diagnostics);
  if (!command.startsWith("HISTORY-EXPORT ") || d.importing ||
      (d.pending[2].active && d.pending[2].request.op != Op::Close))
    return;
  const auto args = command.trimmed().split(' ');
  if (args.size() != 3 || args[2].size() != 32)
    return;
  bool ok = false;
  const auto number = args[1].toUInt(&ok);
  const auto raw = QByteArray::fromHex(args[2]);
  if (!ok || !number || raw.size() != 16 || raw.toHex() != args[2])
    return;
  memcpy(d.archive.bytes, raw.constData(), 16);
  if (d.archive.empty())
    return;
  d.archiveNonce = args[2];
  d.archiveRequest = number;
  d.importing = true;
  d.ordinal = d.cursor = 0;
  d.archiveFields = {{"history_export_version", 3},
                     {"request", double(number)},
                     {"operation_nonce", QString::fromLatin1(args[2])}};
  d.request(2, Op::Open, d.archive);
}
void HidSession::tick(qint64 now) {
  auto &d = *impl;
  if (d.failed)
    return;
  d.clock = now;
  for (auto &p : d.pending)
    if (p.active && now - p.sent >= 10000) {
      if (++p.retries > 3) {
        if (p.request.op == Op::Clock) {
          p.active = false;
          d.clockRequested = false;
          d.cb.received(line({{"history_clock_sync_ok", false}}));
          continue;
        }
        d.fail(QStringLiteral("HID response timed out"));
        return;
      }
      d.send(p);
    }
  if (!d.pending[0].active && now - d.metadataAt >= 10000) {
    d.metadataAt = now;
    d.request(0, Op::Metadata);
  }
}
