#include "ipc.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QtEndian>
#include <limits>
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on

namespace Ipc {
QString endpoint() {
  return "AllpowersMonitor.Service.v1";
}
QByteArray frame(const QJsonObject &object) {
  auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
  if (data.size() > MaxFrame)
    return {};
  QByteArray result(4, '\0');
  qToLittleEndian<quint32>(quint32(data.size()), result.data());
  return result + data;
}
bool consume(QByteArray &buffer, QList<QJsonObject> &out) {
  while (buffer.size() >= 4) {
    auto size = qFromLittleEndian<quint32>(buffer.constData());
    if (!size || size > MaxFrame)
      return false;
    if (buffer.size() < size + 4)
      break;
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(buffer.mid(4, size), &error);
    if (error.error || !doc.isObject() || doc.object()["v"].toInt() != Version)
      return false;
    out.append(doc.object());
    buffer.remove(0, size + 4);
  }
  return buffer.size() <= MaxFrame + 4;
}
// Explicit fields keep IPC independent of C struct padding and preserve C ABI 1.
#define FIELDS(X)                                                                                  \
  X(abi_version)                                                                                   \
  X(model)                                                                                         \
  X(known)                                                                                         \
  X(main_seen)                                                                                     \
  X(settings_seen) X(fresh) X(soc) X(input_w) X(output_w) X(runtime_s) X(source) X(source_raw)     \
      X(ac_output) X(usb_output) X(dc_output) X(main_age_ms) X(settings_age_ms) X(valid_frames)    \
          X(invalid_frames)
QJsonObject snapshot(const ap_snapshot &s) {
  QJsonObject o;
#define WRITE(name) o[#name] = double(s.name);
  FIELDS(WRITE)
#undef WRITE
  return o;
}
bool snapshot(const QJsonObject &o, ap_snapshot &s) {
  ap_snapshot value{};
#define READ(name)                                                                                 \
  {                                                                                                \
    auto n = o[#name];                                                                             \
    const double d = n.toDouble(-1);                                                               \
    if (!n.isDouble() || d < 0 || d > std::numeric_limits<quint32>::max() ||                       \
        double(quint32(d)) != d)                                                                   \
      return false;                                                                                \
    value.name = quint32(d);                                                                       \
  }
  FIELDS(READ)
#undef READ
  if (value.abi_version != 1 || value.soc > 100 || value.source > 2 || value.model > 3 ||
      value.fresh > 1 || value.main_seen > 1 || value.settings_seen > 1)
    return false;
  s = value;
  return true;
}
#undef FIELDS
QJsonObject point(const HistoryPoint &p) {
  auto s = p.data;
  s.abi_version = 1;
  return {{"address", p.address}, {"session", p.session}, {"wall", p.wall},
          {"tick", p.tick},       {"stride", p.stride},   {"data", snapshot(s)}};
}
bool point(const QJsonObject &o, HistoryPoint &p) {
  if (!snapshot(o["data"].toObject(), p.data))
    return false;
  p.address = o["address"].toString();
  p.session = o["session"].toString();
  p.wall = o["wall"].toInteger();
  p.tick = o["tick"].toInteger();
  p.stride = o["stride"].toInt();
  return p.address.size() <= 64 && p.session.size() <= 64 && (p.stride == 1 || p.stride == 60);
}
} // namespace Ipc
struct LocalPipeServer::Native {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  OVERLAPPED operation{};
  HANDLE event = nullptr;
  QWinEventNotifier *notifier = nullptr;
  QString path;
  bool immediate = false;
};
LocalPipeServer::LocalPipeServer(QObject *parent) : QObject(parent), native(new Native) {}
LocalPipeServer::~LocalPipeServer() {
  if (native->notifier)
    native->notifier->setEnabled(false);
  if (native->pipe != INVALID_HANDLE_VALUE) {
    CancelIoEx(native->pipe, nullptr);
    DWORD ignored = 0;
    if (!native->immediate)
      GetOverlappedResult(native->pipe, &native->operation, &ignored, TRUE);
    CloseHandle(native->pipe);
  }
  delete native->notifier;
  if (native->event)
    CloseHandle(native->event);
  delete native;
}
bool LocalPipeServer::listen(const QString &name) {
  if (native->event || name.isEmpty() || name.contains('\\') || name.contains('/'))
    return false;
  native->path = "\\\\.\\pipe\\" + name;
  native->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!native->event)
    return false;
  native->notifier = new QWinEventNotifier(native->event, this);
  connect(native->notifier, &QWinEventNotifier::activated, this, [this] { accept(); });
  return createListener(true);
}
bool LocalPipeServer::createListener(bool first) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GRGW;;;IU)",
                                                            SDDL_REVISION_1, &descriptor, nullptr))
    return false;
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
  native->pipe = CreateNamedPipeW(
      reinterpret_cast<LPCWSTR>(native->path.utf16()),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 9,
      Ipc::MaxFrame, Ipc::MaxFrame, 3000, &attributes);
  LocalFree(descriptor);
  if (native->pipe == INVALID_HANDLE_VALUE) {
    error = QString::number(GetLastError());
    return false;
  }
  native->operation = {};
  native->operation.hEvent = native->event;
  ResetEvent(native->event);
  native->notifier->setEnabled(true);
  native->immediate = false;
  if (ConnectNamedPipe(native->pipe, &native->operation)) {
    native->immediate = true;
    SetEvent(native->event);
  } else if (GetLastError() == ERROR_PIPE_CONNECTED) {
    native->immediate = true;
    SetEvent(native->event);
  } else if (GetLastError() != ERROR_IO_PENDING) {
    error = QString::number(GetLastError());
    return false;
  }
  return true;
}
void LocalPipeServer::accept() {
  native->notifier->setEnabled(false);
  DWORD transferred = 0;
  if (!native->immediate &&
      !GetOverlappedResult(native->pipe, &native->operation, &transferred, FALSE) &&
      GetLastError() != ERROR_PIPE_CONNECTED) {
    native->notifier->setEnabled(true);
    return;
  }
  auto pipe = native->pipe;
  native->pipe = INVALID_HANDLE_VALUE;
  auto *socket = new QLocalSocket(this);
  if (!socket->setSocketDescriptor(reinterpret_cast<qintptr>(pipe), QLocalSocket::ConnectedState)) {
    CloseHandle(pipe);
    socket->deleteLater();
  } else
    emit connected(socket);
  if (!createListener(false)) {
    // Retry once clients release a slot rather than accepting an unbounded pool.
    QTimer::singleShot(500, this, [this] {
      if (native->pipe == INVALID_HANDLE_VALUE)
        createListener(false);
    });
  }
}
