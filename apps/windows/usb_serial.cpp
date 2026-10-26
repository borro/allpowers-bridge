#include "usb_serial.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
// clang-format on
QList<UsbPort> allpowersUsbPorts() {
  const GUID ports = {0x4d36e978, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
  QList<UsbPort> result;
  auto list = SetupDiGetClassDevsW(&ports, nullptr, nullptr, DIGCF_PRESENT);
  if (list == INVALID_HANDLE_VALUE)
    return result;
  const QRegularExpression identity("^USB\\\\VID_303A&PID_4002\\\\(R2500-[0-9A-F]{12})$",
                                    QRegularExpression::CaseInsensitiveOption);
  for (DWORD i = 0;; ++i) {
    SP_DEVINFO_DATA dev{sizeof(dev)};
    if (!SetupDiEnumDeviceInfo(list, i, &dev))
      break;
    QString serial;
    auto node = dev.DevInst;
    for (int depth = 0; depth < 6; ++depth) {
      wchar_t id[MAX_DEVICE_ID_LEN]{};
      if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
        break;
      auto match = identity.match(QString::fromWCharArray(id));
      if (match.hasMatch()) {
        serial = match.captured(1).toUpper();
        break;
      }
      DEVINST parent;
      if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
        break;
      node = parent;
    }
    if (serial.isEmpty())
      continue;
    auto key = SetupDiOpenDevRegKey(list, &dev, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE)
      continue;
    wchar_t name[128]{};
    DWORD size = sizeof(name), type = 0;
    auto ok =
        RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<PBYTE>(name), &size);
    RegCloseKey(key);
    const auto port = QString::fromWCharArray(name);
    if (ok == ERROR_SUCCESS && type == REG_SZ &&
        QRegularExpression("^COM[1-9][0-9]{0,4}$").match(port).hasMatch())
      result.append({serial, port, QString("ALLPOWERS USB · %1 · %2").arg(serial, port)});
  }
  SetupDiDestroyDeviceInfoList(list);
  // An ambiguous identity must never select an arbitrary device.
  QSet<QString> duplicate, seen;
  for (const auto &p : result) {
    if (seen.contains(p.serial))
      duplicate.insert(p.serial);
    seen.insert(p.serial);
  }
  result.removeIf([&](const UsbPort &p) { return duplicate.contains(p.serial); });
  return result;
}
namespace {
class SerialWorker : public QObject {
  Q_OBJECT
  HANDLE handle = INVALID_HANDLE_VALUE;
  QTimer *timer = nullptr;
  quint64 generation = 0;
  bool awaitingRead = false;

public:
  void close() {
    if (timer)
      timer->stop();
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
  }
  void fail(const QString &action) {
    const auto error = GetLastError();
    close();
    emit failed(generation, QString("%1 · Windows %2").arg(action).arg(error));
  }
  void open(const QString &port, quint64 token) {
    close();
    generation = token;
    awaitingRead = false;
    if (!QRegularExpression("^COM[1-9][0-9]{0,4}$").match(port).hasMatch()) {
      emit failed(generation, QCoreApplication::translate("Allpowers", "Invalid USB port"));
      return;
    }
    auto path = ("\\\\.\\" + port).toStdWString();
    handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                         nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      fail(QCoreApplication::translate("Allpowers", "Could not open USB port (it may be in use)"));
      return;
    }
    DCB state{};
    state.DCBlength = sizeof(state);
    if (!GetCommState(handle, &state)) {
      fail("GetCommState");
      return;
    }
    state.BaudRate = 115200;
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    state.fBinary = TRUE;
    state.fParity = FALSE;
    state.fOutxCtsFlow = FALSE;
    state.fOutxDsrFlow = FALSE;
    state.fDtrControl = DTR_CONTROL_ENABLE;
    state.fRtsControl = RTS_CONTROL_DISABLE;
    state.fOutX = FALSE;
    state.fInX = FALSE;
    state.fDsrSensitivity = FALSE;
    state.fAbortOnError = FALSE;
    // Never use 1200 baud: that value is the firmware's bootloader trigger.
    COMMTIMEOUTS timeouts{MAXDWORD, 0, 0, 0, 100};
    if (!SetCommState(handle, &state) || !SetCommTimeouts(handle, &timeouts)) {
      fail(QCoreApplication::translate("Allpowers", "USB setup"));
      return;
    }
    if (!timer) {
      timer = new QTimer(this);
      timer->setInterval(20);
      connect(timer, &QTimer::timeout, this, [this] {
        if (awaitingRead)
          return;
        char data[16384];
        DWORD count = 0;
        if (!ReadFile(handle, data, sizeof(data), &count, nullptr)) {
          fail(QCoreApplication::translate("Allpowers", "USB read"));
          return;
        }
        if (count) {
          awaitingRead = true;
          emit received(generation, QByteArray(data, int(count)));
        }
      });
    }
    timer->start();
    emit opened(generation);
  }
  void write(const QByteArray &bytes) {
    if (handle == INVALID_HANDLE_VALUE)
      return;
    DWORD sent = 0;
    if (!WriteFile(handle, bytes.constData(), DWORD(bytes.size()), &sent, nullptr) ||
        sent != DWORD(bytes.size()))
      fail(QCoreApplication::translate("Allpowers", "USB request write"));
  }
  void acknowledge(quint64 token) {
    if (token == generation)
      awaitingRead = false;
  }
signals:
  void opened(quint64);
  void received(quint64, QByteArray);
  void failed(quint64, QString);
};
class NativeChannel : public UsbChannel {
  Q_OBJECT
  QThread thread;
  SerialWorker *worker = new SerialWorker;
  quint64 generation = 0;

public:
  explicit NativeChannel(QObject *parent) : UsbChannel(parent) {
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SerialWorker::opened, this, [this](quint64 token) {
      if (token == generation)
        emit opened();
    });
    connect(worker, &SerialWorker::received, this, [this](quint64 token, QByteArray b) {
      if (token == generation)
        emit received(b);
      QMetaObject::invokeMethod(
          worker, [this, token] { worker->acknowledge(token); }, Qt::QueuedConnection);
    });
    connect(worker, &SerialWorker::failed, this, [this](quint64 token, QString e) {
      if (token == generation)
        emit failed(e);
    });
    thread.start();
  }
  ~NativeChannel() override {
    worker->disconnect(this);
    QMetaObject::invokeMethod(worker, [this] { worker->close(); }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
  }
  void open(const QString &port) override {
    const auto token = ++generation;
    QMetaObject::invokeMethod(
        worker, [this, port, token] { worker->open(port, token); }, Qt::QueuedConnection);
  }
  void close() override {
    ++generation;
    QMetaObject::invokeMethod(worker, [this] { worker->close(); }, Qt::QueuedConnection);
  }
  void write(const QByteArray &bytes) override {
    QMetaObject::invokeMethod(
        worker, [this, bytes] { worker->write(bytes); }, Qt::QueuedConnection);
  }
};
} // namespace
UsbChannel *nativeUsbChannel(QObject *parent) {
  return new NativeChannel(parent);
}
#include "usb_serial.moc"
