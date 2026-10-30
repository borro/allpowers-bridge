#include "usb_hid.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMap>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#define WIN32_LEAN_AND_MEAN
#include <hidclass.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <windows.h>

QList<UsbPort> allpowersHidPorts() {
  GUID guid;
  HidD_GetHidGuid(&guid);
  const auto list =
      SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (list == INVALID_HANDLE_VALUE)
    return {};
  QMap<QString, QList<UsbPort>> telemetry;
  QMap<QString, UsbPort> maintenance;
  for (DWORD i = 0;; ++i) {
    SP_DEVICE_INTERFACE_DATA item{sizeof(item)};
    if (!SetupDiEnumDeviceInterfaces(list, nullptr, &guid, i, &item))
      break;
    DWORD size = 0;
    SetupDiGetDeviceInterfaceDetailW(list, &item, nullptr, 0, &size, nullptr);
    if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536)
      continue;
    QByteArray storage(int(size), 0);
    auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(list, &item, detail, size, nullptr, nullptr))
      continue;
    const auto handle = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
      continue;
    HIDD_ATTRIBUTES attributes{sizeof(attributes)};
    wchar_t serialBuffer[128]{};
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps{};
    const bool valid = HidD_GetAttributes(handle, &attributes) && attributes.VendorID == 0x303a &&
                       attributes.ProductID == 0x4002 &&
                       HidD_GetSerialNumberString(handle, serialBuffer, sizeof(serialBuffer)) &&
                       HidD_GetPreparsedData(handle, &preparsed) &&
                       HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
    if (preparsed)
      HidD_FreePreparsedData(preparsed);
    CloseHandle(handle);
    const auto serial = QString::fromWCharArray(serialBuffer).toUpper();
    if (!valid || !QRegularExpression("^R2500-[0-9A-F]{12}$").match(serial).hasMatch())
      continue;
    const auto path = QString::fromWCharArray(detail->DevicePath);
    if (caps.UsagePage == HidTelemetry::UsagePage && caps.Usage == HidTelemetry::Usage &&
        caps.InputReportByteLength == 64 && caps.OutputReportByteLength == 64 &&
        caps.FeatureReportByteLength == 64)
      telemetry[serial].append(
          {serial, path, QString("ALLPOWERS USB HID · #%1").arg(serial.right(6))});
    else if (caps.UsagePage == 0xff00 && caps.Usage == 1)
      maintenance[serial] = {serial, "upgrade:" + serial,
                             QCoreApplication::translate("Allpowers", "Firmware update required") +
                                 " · #" + serial.right(6)};
  }
  SetupDiDestroyDeviceInfoList(list);
  QList<UsbPort> result;
  for (auto it = telemetry.begin(); it != telemetry.end(); ++it) {
    maintenance.remove(it.key());
    if (it.value().size() == 1)
      result.append(it.value().first());
  }
  for (const auto &port : maintenance)
    result.append(port);
  return result;
}
namespace {
class HidWorker : public QObject {
  Q_OBJECT
  HANDLE handle = INVALID_HANDLE_VALUE;
  OVERLAPPED read{};
  bool reading = false;
  uint8_t input[64]{};
  QTimer *timer = nullptr;
  QElapsedTimer clock;
  qint64 nextLive = 0;
  quint64 generation = 0;
  std::unique_ptr<HidSession> session;
  QString error;

  bool finish(OVERLAPPED &operation, BOOL immediate, DWORD &count) {
    if (!immediate && GetLastError() != ERROR_IO_PENDING)
      return false;
    if (!immediate && WaitForSingleObject(operation.hEvent, 1000) != WAIT_OBJECT_0) {
      CancelIoEx(handle, &operation);
      GetOverlappedResult(handle, &operation, &count, TRUE);
      return false;
    }
    return GetOverlappedResult(handle, &operation, &count, FALSE) != FALSE;
  }
  bool feature(uint8_t id, QByteArray &payload) {
    uint8_t buffer[64]{};
    buffer[0] = id;
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent)
      return false;
    DWORD count = 0;
    const auto started = DeviceIoControl(handle, IOCTL_HID_GET_FEATURE, buffer, sizeof(buffer),
                                         buffer, sizeof(buffer), &count, &operation);
    const bool ok = finish(operation, started, count);
    CloseHandle(operation.hEvent);
    if (ok)
      payload = QByteArray(reinterpret_cast<const char *>(buffer + 1), 63);
    return ok && buffer[0] == id;
  }
  void output(const QByteArray &bytes) {
    if (!error.isEmpty())
      return;
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) {
      error = "HID event allocation failed";
      return;
    }
    DWORD count = 0;
    const auto started =
        WriteFile(handle, bytes.constData(), DWORD(bytes.size()), &count, &operation);
    const bool ok = finish(operation, started, count) && count == DWORD(bytes.size());
    CloseHandle(operation.hEvent);
    if (!ok)
      error = "HID request failed";
  }
  void poll() {
    if (handle == INVALID_HANDLE_VALUE || !session)
      return;
    for (unsigned i = 0; i < 32 && error.isEmpty(); ++i) {
      DWORD count = 0;
      if (!reading) {
        ResetEvent(read.hEvent);
        const auto ok = ReadFile(handle, input, sizeof(input), &count, &read);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
          error = "HID read failed";
          break;
        }
        reading = true;
      }
      if (!GetOverlappedResult(handle, &read, &count, FALSE)) {
        if (GetLastError() != ERROR_IO_INCOMPLETE)
          error = "HID read failed";
        break;
      }
      reading = false;
      session->input(QByteArray(reinterpret_cast<const char *>(input), int(count)),
                     clock.elapsed());
    }
    if (error.isEmpty() && clock.elapsed() >= nextLive) {
      QByteArray payload;
      nextLive = clock.elapsed() + 1000;
      if (!feature(HidTelemetry::LiveReport, payload))
        error = "HID telemetry read failed";
      else
        session->live(payload);
    }
    if (error.isEmpty())
      session->tick(clock.elapsed());
    if (!error.isEmpty()) {
      const auto reason = error;
      close();
      emit failed(generation, reason);
    }
  }

public:
  void close() {
    if (timer)
      timer->stop();
    if (session && handle != INVALID_HANDLE_VALUE)
      session->stop();
    if (handle != INVALID_HANDLE_VALUE) {
      if (reading) {
        DWORD count = 0;
        CancelIoEx(handle, &read);
        GetOverlappedResult(handle, &read, &count, TRUE);
      }
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
    if (read.hEvent)
      CloseHandle(read.hEvent);
    read = {};
    reading = false;
    session.reset();
  }
  void open(const QString &path, const QString &serial, quint64 token) {
    close();
    generation = token;
    error.clear();
    clock.start();
    nextLive = 0;
    if (path.startsWith("upgrade:")) {
      emit failed(token, QCoreApplication::translate("Allpowers", "Firmware update required"));
      return;
    }
    handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      emit failed(token, "Could not open USB HID");
      return;
    }
    // Revalidate the actual opened collection, including a path that changed
    // between enumeration and open. Never write to UPS or maintenance handles.
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps{};
    HIDD_ATTRIBUTES attributes{sizeof(attributes)};
    wchar_t actualSerial[128]{};
    const bool valid =
        HidD_GetAttributes(handle, &attributes) && attributes.VendorID == 0x303a &&
        attributes.ProductID == 0x4002 &&
        HidD_GetSerialNumberString(handle, actualSerial, sizeof(actualSerial)) &&
        QString::fromWCharArray(actualSerial).compare(serial, Qt::CaseInsensitive) == 0 &&
        HidD_GetPreparsedData(handle, &preparsed) &&
        HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
        caps.UsagePage == HidTelemetry::UsagePage && caps.Usage == HidTelemetry::Usage &&
        caps.InputReportByteLength == 64 && caps.OutputReportByteLength == 64 &&
        caps.FeatureReportByteLength == 64;
    if (preparsed)
      HidD_FreePreparsedData(preparsed);
    if (!valid) {
      close();
      emit failed(token, "Unexpected USB HID collection");
      return;
    }
    HidD_SetNumInputBuffers(handle, 512);
    read.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QByteArray payload;
    if (!read.hEvent || !feature(HidTelemetry::CapsReport, payload)) {
      close();
      emit failed(token, "HID capabilities unavailable");
      return;
    }
    session = std::make_unique<HidSession>(
        HidSession::Callbacks{[this](QByteArray data) { output(data); },
                              [this](QByteArray data) { emit received(generation, data); },
                              [this](QString text) { error = std::move(text); }});
    if (!session->start(payload, clock.elapsed()) || !error.isEmpty()) {
      const auto reason = error;
      close();
      emit failed(token, reason);
      return;
    }
    if (!timer) {
      timer = new QTimer(this);
      timer->setInterval(5);
      connect(timer, &QTimer::timeout, this, &HidWorker::poll);
    }
    timer->start();
    emit opened(token);
  }
  void write(const QByteArray &command, quint64 token) {
    if (token == generation && session)
      session->command(command, clock.elapsed());
  }
  void cancelHistory(quint64 token) {
    if (token == generation && session)
      session->stop();
  }
signals:
  void opened(quint64);
  void received(quint64, QByteArray);
  void failed(quint64, QString);
};
class HidChannel : public UsbChannel {
  Q_OBJECT
  QThread thread;
  HidWorker *worker = new HidWorker;
  quint64 generation = 0;

public:
  explicit HidChannel(QObject *parent) : UsbChannel(parent) {
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &HidWorker::opened, this, [this](quint64 token) {
      if (token == generation)
        emit opened();
    });
    connect(worker, &HidWorker::received, this, [this](quint64 token, QByteArray data) {
      if (token == generation)
        emit received(data);
    });
    connect(worker, &HidWorker::failed, this, [this](quint64 token, QString message) {
      if (token == generation)
        emit failed(message);
    });
    thread.start();
  }
  bool hid() const override {
    return true;
  }
  ~HidChannel() override {
    worker->disconnect(this);
    QMetaObject::invokeMethod(worker, [this] { worker->close(); }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
  }
  void open(const QString &path) override {
    emit failed("HID requires an explicitly selected device identity: " + path);
  }
  void openDevice(const UsbPort &device) override {
    const auto token = ++generation;
    QMetaObject::invokeMethod(
        worker, [this, device, token] { worker->open(device.port, device.serial, token); },
        Qt::QueuedConnection);
  }
  void close() override {
    ++generation;
    QMetaObject::invokeMethod(worker, [this] { worker->close(); }, Qt::QueuedConnection);
  }
  void write(const QByteArray &command) override {
    const auto token = generation;
    QMetaObject::invokeMethod(
        worker, [this, command, token] { worker->write(command, token); }, Qt::QueuedConnection);
  }
  void cancelHistory() override {
    const auto token = generation;
    QMetaObject::invokeMethod(
        worker, [this, token] { worker->cancelHistory(token); }, Qt::QueuedConnection);
  }
};
} // namespace
UsbChannel *nativeHidChannel(QObject *parent) {
  return new HidChannel(parent);
}
#include "usb_hid.moc"
