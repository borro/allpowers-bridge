#include "usb_hid.h"
#include <QSignalSpy>
#include <QTest>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <deque>
#include <map>
#include <atomic>
#include <vector>
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidclass.h>

// Closed native boundary: no device enumeration, device handle, IOCTL or report
// reaches Windows. Real event handles are only synchronization primitives.
namespace FakeHid {
QMutex lock;
struct Spec { USHORT usage = 0xff01, reportSize = 64, vid = 0x303a; QString serial = "R2500-000000000001"; };
std::vector<Spec> enumeration;
struct Device { std::deque<QByteArray> reports; Spec spec; };
std::map<HANDLE, Device> devices;
int opens = 0, writes = 0, reads = 0, cancelled = 0;
bool wrongIdentity = false;
std::atomic<int> fault = 0; // 1=open, 2=feature, 3=write, 4=read, 5=pending write timeout
HANDLE WINAPI create(LPCWSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES,
                     DWORD disposition, DWORD flags, HANDLE) {
  QMutexLocker guard(&lock);
  Q_ASSERT(access == 0 || access == (GENERIC_READ | GENERIC_WRITE));
  Q_ASSERT(sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE));
  Q_ASSERT(disposition == OPEN_EXISTING && flags == (access ? FILE_FLAG_OVERLAPPED : 0));
  if (fault == 1) return INVALID_HANDLE_VALUE;
  const auto handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Device device;
  if (!access) device.spec = enumeration.at(QString::fromWCharArray(path).mid(5).toUInt());
  devices.emplace(handle, device); ++opens; return handle;
}
BOOL WINAPI close(HANDLE handle) {
  QMutexLocker guard(&lock); devices.erase(handle); return ::CloseHandle(handle);
}
BOOLEAN WINAPI attributes(HANDLE h, PHIDD_ATTRIBUTES a) {
  QMutexLocker guard(&lock); a->VendorID = devices.at(h).spec.vid; a->ProductID = 0x4002; return TRUE;
}
BOOLEAN WINAPI serial(HANDLE h, PVOID out, ULONG size) {
  QMutexLocker guard(&lock);
  return wcscpy_s(static_cast<wchar_t *>(out), size / sizeof(wchar_t),
                   wrongIdentity ? L"R2500-000000000002" : devices.at(h).spec.serial.toStdWString().c_str()) == 0;
}
BOOLEAN WINAPI preparsed(HANDLE h, PHIDP_PREPARSED_DATA *p) { *p = reinterpret_cast<PHIDP_PREPARSED_DATA>(h); return TRUE; }
BOOLEAN WINAPI freeData(PHIDP_PREPARSED_DATA) { return TRUE; }
NTSTATUS __stdcall caps(PHIDP_PREPARSED_DATA data, PHIDP_CAPS p) {
  QMutexLocker guard(&lock); const auto &spec = devices.at(reinterpret_cast<HANDLE>(data)).spec;
  *p = {}; p->UsagePage = spec.usage; p->Usage = HidTelemetry::Usage;
  p->InputReportByteLength = p->OutputReportByteLength = p->FeatureReportByteLength = spec.reportSize;
  return HIDP_STATUS_SUCCESS;
}
BOOLEAN WINAPI buffers(HANDLE, ULONG count) { Q_ASSERT(count == 512); return TRUE; }
BOOL WINAPI control(HANDLE, DWORD code, LPVOID in, DWORD inSize, LPVOID out,
                     DWORD outSize, LPDWORD count, LPOVERLAPPED operation) {
  Q_ASSERT(code == IOCTL_HID_GET_FEATURE && in == out && inSize == 64 && outSize == 64);
  if (fault == 2) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return FALSE; }
  auto *p = static_cast<uint8_t *>(out);
  if (p[0] == HidTelemetry::CapsReport) HidTelemetry::capabilities(p + 1, 42);
  else {
    Q_ASSERT(p[0] == HidTelemetry::LiveReport);
    HidTelemetry::Live v; v.boot = 42; v.soc = 73; v.address[0] = 2; v.address[5] = 1;
    v.model = 3; v.known = 513; v.flags = HidTelemetry::Fresh | HidTelemetry::Connected;
    HidTelemetry::encode(v, p + 1);
  }
  operation->Internal = 0; operation->InternalHigh = *count = 64; return TRUE;
}
BOOL WINAPI write(HANDLE, LPCVOID data, DWORD size, LPDWORD count, LPOVERLAPPED operation) {
  QMutexLocker guard(&lock); ++writes;
  if (fault == 3) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return FALSE; }
  if (fault == 5) { operation->Internal = ERROR_IO_PENDING; SetLastError(ERROR_IO_PENDING); return FALSE; }
  const auto *p = static_cast<const uint8_t *>(data);
  Q_ASSERT(size == 64 && p[0] == HidTelemetry::RequestReport);
  HidTelemetry::Request request;
  Q_ASSERT(HidTelemetry::decode(p + 1, 63, request));
  const QByteArray body = request.op == HidTelemetry::Op::Metadata
    ? "{\"station_name\":\"test\",\"station_address\":\"02:00:00:00:00:01\"}\n"
    : "{\"diagnostic\":true,\"rows\":[]}\n";
  const auto digest = HidTelemetry::crc32(body.constData(), size_t(body.size()));
  for (size_t offset = 0; offset < size_t(body.size());) {
    uint8_t response[64]{}; response[0] = HidTelemetry::ResponseReport;
    offset += HidTelemetry::fragment(request, HidTelemetry::Result::Ok,
      reinterpret_cast<const uint8_t *>(body.constData()), size_t(body.size()), digest, offset, response + 1);
    for (auto &entry : devices)
      entry.second.reports.emplace_back(reinterpret_cast<char *>(response), 64);
  }
  operation->Internal = 0; operation->InternalHigh = *count = size; return TRUE;
}
std::map<LPOVERLAPPED, LPVOID> destinations;
BOOL WINAPI read(HANDLE, LPVOID out, DWORD size, LPDWORD, LPOVERLAPPED operation) {
  QMutexLocker guard(&lock); ++reads; Q_ASSERT(size == 64);
  if (fault == 4) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return FALSE; }
  destinations[operation] = out; operation->Internal = ERROR_IO_PENDING;
  SetLastError(ERROR_IO_PENDING); return FALSE;
}
BOOL WINAPI completed(HANDLE handle, LPOVERLAPPED operation, LPDWORD count, BOOL) {
  QMutexLocker guard(&lock);
  if (operation->Internal == ERROR_OPERATION_ABORTED) {
    destinations.erase(operation); SetLastError(ERROR_OPERATION_ABORTED); return FALSE;
  }
  if (operation->Internal == ERROR_IO_PENDING) {
    auto &reports = devices.at(handle).reports;
    if (reports.empty()) { SetLastError(ERROR_IO_INCOMPLETE); return FALSE; }
    memcpy(destinations.at(operation), reports.front().constData(), 64); reports.pop_front();
    destinations.erase(operation); operation->Internal = 0; operation->InternalHigh = 64;
  }
  *count = DWORD(operation->InternalHigh); return TRUE;
}
BOOL WINAPI cancel(HANDLE, LPOVERLAPPED operation) {
  QMutexLocker guard(&lock); ++cancelled; operation->Internal = ERROR_OPERATION_ABORTED; return TRUE;
}
HDEVINFO WINAPI enumerate(const GUID *, PCWSTR, HWND, DWORD) { return reinterpret_cast<HDEVINFO>(1); }
BOOL WINAPI enumerateInterface(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD index, PSP_DEVICE_INTERFACE_DATA out) {
  if (index >= enumeration.size()) return FALSE;
  out->Reserved = index; return TRUE;
}
BOOL WINAPI detail(HDEVINFO, PSP_DEVICE_INTERFACE_DATA item, PSP_DEVICE_INTERFACE_DETAIL_DATA_W out,
                     DWORD size, PDWORD required, PSP_DEVINFO_DATA) {
  const auto path = QString("enum-%1").arg(item->Reserved).toStdWString();
  const DWORD needed = DWORD(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + path.size() * sizeof(wchar_t));
  if (required) *required = needed;
  if (!out || size < needed) return FALSE;
  wcscpy_s(out->DevicePath, path.size() + 1, path.c_str()); return TRUE;
}
BOOL WINAPI destroy(HDEVINFO) { return TRUE; }
}
#define CreateFileW FakeHid::create
#define CloseHandle FakeHid::close
#define HidD_GetAttributes FakeHid::attributes
#define HidD_GetSerialNumberString FakeHid::serial
#define HidD_GetPreparsedData FakeHid::preparsed
#define HidD_FreePreparsedData FakeHid::freeData
#define HidP_GetCaps FakeHid::caps
#define HidD_SetNumInputBuffers FakeHid::buffers
#define DeviceIoControl FakeHid::control
#define WriteFile FakeHid::write
#define ReadFile FakeHid::read
#define GetOverlappedResult FakeHid::completed
#define CancelIoEx FakeHid::cancel
#define SetupDiGetClassDevsW FakeHid::enumerate
#define SetupDiEnumDeviceInterfaces FakeHid::enumerateInterface
#define SetupDiGetDeviceInterfaceDetailW FakeHid::detail
#define SetupDiDestroyDeviceInfoList FakeHid::destroy
#include "../../apps/windows/usb_hid.cpp"
#undef CreateFileW
#undef CloseHandle
#undef HidD_GetAttributes
#undef HidD_GetSerialNumberString
#undef HidD_GetPreparsedData
#undef HidD_FreePreparsedData
#undef HidP_GetCaps
#undef HidD_SetNumInputBuffers
#undef DeviceIoControl
#undef WriteFile
#undef ReadFile
#undef GetOverlappedResult
#undef CancelIoEx
#undef SetupDiGetClassDevsW
#undef SetupDiEnumDeviceInterfaces
#undef SetupDiGetDeviceInterfaceDetailW
#undef SetupDiDestroyDeviceInfoList

class HidNativeTests : public QObject {
  Q_OBJECT
private slots:
  void init() {
    FakeHid::opens = FakeHid::writes = FakeHid::reads = FakeHid::cancelled = 0;
    FakeHid::fault = 0; FakeHid::wrongIdentity = false; FakeHid::enumeration.clear();
    QVERIFY(FakeHid::devices.empty()); QVERIFY(FakeHid::destinations.empty());
  }
  void discoverByUsageAndSerial() {
    using FakeHid::Spec;
    FakeHid::enumeration = {Spec{0xff00}, Spec{}, Spec{0x84},
      Spec{0xff00, 64, 0x303a, "R2500-000000000002"},
      Spec{0xff01, 63}, Spec{0xff01, 64, 0x1234}, Spec{0xff01, 64, 0x303a, "invalid"}};
    const auto ports = allpowersHidPorts(); QCOMPARE(ports.size(), 2);
    QCOMPARE(ports[0].serial, QString("R2500-000000000001")); QCOMPARE(ports[0].port, QString("enum-1"));
    QVERIFY(ports[1].port.startsWith("upgrade:"));
    QCOMPARE(FakeHid::writes, 0); QVERIFY(FakeHid::devices.empty());
    FakeHid::enumeration.push_back(Spec{});
    const auto duplicate = allpowersHidPorts(); QCOMPARE(duplicate.size(), 1);
    QCOMPARE(duplicate[0].serial, QString("R2500-000000000002"));
  }
  void failuresAndRecovery_data() {
    QTest::addColumn<int>("mode");
    for (int i = 1; i <= 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
  }
  void failuresAndRecovery() {
    QFETCH(int, mode); FakeHid::fault = mode;
    auto channel = std::unique_ptr<UsbChannel>(nativeHidChannel(nullptr));
    QSignalSpy errors(channel.get(), &UsbChannel::failed);
    const UsbPort port{"R2500-000000000001", "synthetic", "fixture"};
    channel->openDevice(port); QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 4000);
    QVERIFY(FakeHid::devices.empty()); QVERIFY(FakeHid::destinations.empty());
    FakeHid::fault = 0; QSignalSpy received(channel.get(), &UsbChannel::received);
    channel->openDevice(port); QTRY_VERIFY_WITH_TIMEOUT(received.size() >= 2, 4000);
    channel.reset(); QVERIFY(FakeHid::devices.empty()); QVERIFY(FakeHid::destinations.empty());
  }
  void oldFirmwareAndMissingIdentityNeverWrite() {
    auto channel = std::unique_ptr<UsbChannel>(nativeHidChannel(nullptr));
    QSignalSpy errors(channel.get(), &UsbChannel::failed);
    channel->open("synthetic"); QCOMPARE(errors.size(), 1);
    channel->openDevice({"R2500-000000000001", "upgrade:synthetic", "old"});
    QTRY_COMPARE(errors.size(), 2); QCOMPARE(FakeHid::opens, 0); QCOMPARE(FakeHid::writes, 0);
  }
  void twoHandlesAndIndependentClose() {
    const UsbPort selected{"R2500-000000000001", "synthetic", "fixture"};
    auto first = std::unique_ptr<UsbChannel>(nativeHidChannel(nullptr));
    auto second = std::unique_ptr<UsbChannel>(nativeHidChannel(nullptr));
    QSignalSpy one(first.get(), &UsbChannel::received), two(second.get(), &UsbChannel::received);
    QSignalSpy firstErrors(first.get(), &UsbChannel::failed), secondErrors(second.get(), &UsbChannel::failed);
    first->openDevice(selected); second->openDevice(selected);
    QTRY_VERIFY_WITH_TIMEOUT(one.size() >= 2 && two.size() >= 2, 5000);
    QCOMPARE(firstErrors.size(), 0); QCOMPARE(secondErrors.size(), 0);
    QCOMPARE(FakeHid::opens, 2);
    first.reset(); const auto before = two.size();
    QTRY_VERIFY_WITH_TIMEOUT(two.size() > before, 3000);
    second.reset(); QVERIFY(FakeHid::devices.empty()); QVERIFY(FakeHid::cancelled >= 2);
    QVERIFY(FakeHid::destinations.empty());
  }
  void rejectIdentityBeforeRequests() {
    { QMutexLocker guard(&FakeHid::lock); FakeHid::wrongIdentity = true; }
    auto channel = std::unique_ptr<UsbChannel>(nativeHidChannel(nullptr));
    QSignalSpy errors(channel.get(), &UsbChannel::failed); const auto writes = FakeHid::writes;
    channel->openDevice({"R2500-000000000001", "synthetic", "fixture"});
    QTRY_COMPARE(errors.size(), 1); QCOMPARE(FakeHid::writes, writes);
    channel.reset(); QVERIFY(FakeHid::devices.empty());
    { QMutexLocker guard(&FakeHid::lock); FakeHid::wrongIdentity = false; }
  }
};
QTEST_GUILESS_MAIN(HidNativeTests)
#include "hid_native_test.moc"
