#include <QtTest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <mutex>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include "../../apps/windows/usb_serial.h"

namespace Fake {
struct Device { QStringList ancestors; QString port; bool registry = true; DWORD type = REG_SZ; };
struct State {
  QList<Device> devices;
  QString fail, path;
  QByteArray input, output;
  DCB configured{};
  COMMTIMEOUTS timeouts{};
  int opens = 0, closes = 0, reads = 0, listsClosed = 0;
  bool shortWrite = false;
} s;
std::mutex mutex;
HANDLE handle = reinterpret_cast<HANDLE>(quintptr(123));
HDEVINFO SetupDiGetClassDevsW(auto, auto, auto, DWORD flags) {
  Q_ASSERT(flags == DIGCF_PRESENT);
  return s.fail == "enumerate" ? INVALID_HANDLE_VALUE : handle;
}
BOOL SetupDiEnumDeviceInfo(auto, DWORD index, PSP_DEVINFO_DATA dev) {
  dev->DevInst = index * 16; return index < DWORD(s.devices.size());
}
CONFIGRET CM_Get_Device_IDW(DEVINST node, PWSTR out, ULONG size, ULONG) {
  const auto &ancestors = s.devices[node/16].ancestors;
  if (node % 16 >= ULONG(ancestors.size())) return CR_FAILURE;
  const auto value = ancestors[node%16].toStdWString(); wcscpy_s(out, size, value.c_str());
  return CR_SUCCESS;
}
CONFIGRET CM_Get_Parent(PDEVINST parent, DEVINST node, ULONG) {
  if (node%16+1 >= ULONG(s.devices[node/16].ancestors.size())) return CR_NO_SUCH_DEVNODE;
  *parent = node + 1; return CR_SUCCESS;
}
HKEY SetupDiOpenDevRegKey(auto, PSP_DEVINFO_DATA dev, DWORD, DWORD, DWORD, DWORD) {
  return s.devices[dev->DevInst/16].registry ? reinterpret_cast<HKEY>(quintptr(dev->DevInst/16+1))
                                          : reinterpret_cast<HKEY>(INVALID_HANDLE_VALUE);
}
LSTATUS RegQueryValueExW(HKEY key, auto, auto, LPDWORD type, LPBYTE bytes, LPDWORD capacity) {
  const auto &device = s.devices[quintptr(key)-1]; *type = device.type;
  auto port = device.port.toStdWString();
  memcpy(bytes, port.c_str(), qMin(size_t(*capacity), (port.size()+1)*sizeof(wchar_t)));
  return s.fail == "registry-read" ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
}
LSTATUS RegCloseKey(HKEY) { return ERROR_SUCCESS; }
BOOL SetupDiDestroyDeviceInfoList(HDEVINFO) { ++s.listsClosed; return TRUE; }
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, auto, DWORD disposition, DWORD flags, auto) {
  std::lock_guard lock(mutex);
  Q_ASSERT(access == (GENERIC_READ|GENERIC_WRITE) && share == 0 && disposition == OPEN_EXISTING && flags == 0);
  ++s.opens; s.path = QString::fromWCharArray(path);
  return s.fail == "open" ? INVALID_HANDLE_VALUE : handle;
}
BOOL CloseHandle(HANDLE value) { std::lock_guard lock(mutex); Q_ASSERT(value == handle); ++s.closes; return TRUE; }
DWORD GetLastError() { return ERROR_ACCESS_DENIED; }
BOOL GetCommState(HANDLE, LPDCB state) {
  std::lock_guard lock(mutex);
  // Deliberately hostile previous state: production must clear all flow control.
  memset(state, 0xff, sizeof(*state)); return s.fail != "get-state";
}
BOOL SetCommState(HANDLE, LPDCB state) { std::lock_guard lock(mutex); s.configured = *state; return s.fail != "set-state"; }
BOOL SetCommTimeouts(HANDLE, LPCOMMTIMEOUTS timeouts) {
  std::lock_guard lock(mutex); s.timeouts = *timeouts; return s.fail != "timeouts";
}
BOOL ReadFile(HANDLE, LPVOID buffer, DWORD capacity, LPDWORD read, auto) {
  std::lock_guard lock(mutex); ++s.reads;
  *read = qMin(capacity, DWORD(s.input.size())); memcpy(buffer, s.input.data(), *read);
  s.input.remove(0, *read); return s.fail != "read";
}
BOOL WriteFile(HANDLE, LPCVOID bytes, DWORD count, LPDWORD written, auto) {
  std::lock_guard lock(mutex);
  s.output.append(static_cast<const char *>(bytes), count);
  *written = s.shortWrite ? count-1 : count; return s.fail != "write";
}
} // namespace Fake

#include "usb_serial_win32_redirect.h"
#include "../../apps/windows/usb_serial.cpp"
#include "usb_serial_win32_restore.h"

class SerialContract : public QObject {
  Q_OBJECT
private slots:
  void init() { Fake::s = {}; }
  void enumerateFiltersAndAmbiguity() {
    const QString a = "USB\\VID_303A&PID_4002\\R2500-000000000001";
    const QString b = "USB\\VID_303A&PID_4002\\R2500-000000000002";
    Fake::s.devices = {{{"child", a}, "COM17"}, {{a.toLower()}, "COM18"},
      {{b.toLower()}, "COM19"}, {{"USB\\VID_9999&PID_4002\\R2500-000000000003"}, "COM20"},
      {{"root"}, "COM21"}, {{a}, "COM0"}, {{a}, "COM22", false}, {{a}, "COM23", true, REG_BINARY},
      {{"1","2","3","4","5","6",a}, "COM24"}, {{}, "COM25"}};
    const auto ports = allpowersUsbPorts();
    QCOMPARE(ports.size(), 1); QCOMPARE(ports[0].serial, QString("R2500-000000000002"));
    QCOMPARE(ports[0].port, QString("COM19")); QVERIFY(ports[0].description.contains("COM19"));
    QCOMPARE(Fake::s.listsClosed, 1);
    Fake::s.fail = "registry-read"; QVERIFY(allpowersUsbPorts().isEmpty());
    Fake::s.fail = "enumerate"; QVERIFY(allpowersUsbPorts().isEmpty());
    QCOMPARE(Fake::s.listsClosed, 2);
  }
  void portValidation_data() {
    QTest::addColumn<QString>("port");
    for (const auto *p : {"COM0", "com1", "COM100000", "COM1/../x", "", "\\\\.\\COM1"})
      QTest::newRow(p) << QString(p);
  }
  void portValidation() {
    QFETCH(QString, port); SerialWorker worker; QSignalSpy failures(&worker, &SerialWorker::failed);
    worker.open(port, 12); QCOMPARE(failures.count(), 1); QCOMPARE(failures[0][0].toULongLong(), 12);
    QCOMPARE(Fake::s.opens, 0); worker.close(); worker.write("ignored"); QVERIFY(Fake::s.output.isEmpty());
  }
  void configurationAndBackpressure() {
    SerialWorker worker; QSignalSpy opened(&worker, &SerialWorker::opened);
    QSignalSpy received(&worker, &SerialWorker::received);
    worker.open("COM123", 7); QCOMPARE(opened.count(), 1);
    QCOMPARE(Fake::s.path, QString("\\\\.\\COM123"));
    const auto &d = Fake::s.configured;
    QCOMPARE(d.BaudRate, DWORD(115200)); QCOMPARE(d.ByteSize, BYTE(8));
    QCOMPARE(d.Parity, BYTE(NOPARITY)); QCOMPARE(d.StopBits, BYTE(ONESTOPBIT));
    QVERIFY(d.fBinary && !d.fParity && !d.fOutxCtsFlow && !d.fOutxDsrFlow);
    QVERIFY(!d.fOutX && !d.fInX && !d.fDsrSensitivity && !d.fAbortOnError);
    QCOMPARE(DWORD(d.fDtrControl), DWORD(DTR_CONTROL_ENABLE));
    QCOMPARE(DWORD(d.fRtsControl), DWORD(RTS_CONTROL_DISABLE));
    QCOMPARE(Fake::s.timeouts.ReadIntervalTimeout, DWORD(MAXDWORD));
    QCOMPARE(Fake::s.timeouts.WriteTotalTimeoutConstant, DWORD(100));
    QCOMPARE(Fake::s.timeouts.ReadTotalTimeoutConstant, DWORD(0));
    Fake::s.input = "first"; QTRY_COMPARE(received.count(), 1);
    QCOMPARE(received[0][1].toByteArray(), QByteArray("first"));
    Fake::s.input = "second"; const int reads = Fake::s.reads;
    QTest::qWait(60); QCOMPARE(Fake::s.reads, reads);
    worker.acknowledge(6); QTest::qWait(40); QCOMPARE(received.count(), 1);
    worker.acknowledge(7); QTRY_COMPARE(received.count(), 2);
    worker.write("request"); QCOMPARE(Fake::s.output, QByteArray("request"));
    worker.close(); worker.close(); QCOMPARE(Fake::s.closes, 1);
    worker.open("COM2", 8); QCOMPARE(opened.count(), 2); worker.close(); QCOMPARE(Fake::s.closes, 2);
  }
  void failures_data() {
    QTest::addColumn<QString>("failure");
    for (const auto *f : {"open", "get-state", "set-state", "timeouts", "read", "write", "short-write"})
      QTest::newRow(f) << QString(f);
  }
  void failures() {
    QFETCH(QString, failure); Fake::s.fail = failure; Fake::s.shortWrite = failure == "short-write";
    SerialWorker worker; QSignalSpy errors(&worker, &SerialWorker::failed);
    worker.open("COM1", 3);
    if (failure == "write" || failure == "short-write") worker.write("packet");
    QTRY_COMPARE(errors.count(), 1);
    QCOMPARE(errors[0][0].toULongLong(), 3); QVERIFY(errors[0][1].toString().contains("Windows 5"));
    QCOMPARE(Fake::s.closes, failure == "open" ? 0 : 1);
    worker.close(); worker.write("after failure"); QCOMPARE(errors.count(), 1);
  }
  void threadedChannelLifetime() {
    auto channel = std::unique_ptr<UsbChannel>(nativeUsbChannel(nullptr));
    QSignalSpy opened(channel.get(), &UsbChannel::opened), received(channel.get(), &UsbChannel::received);
    QSignalSpy failed(channel.get(), &UsbChannel::failed);
    channel->open("COM2"); QTRY_COMPARE(opened.count(), 1);
    { std::lock_guard lock(Fake::mutex); Fake::s.input = "response"; }
    QTRY_COMPARE(received.count(), 1); QCOMPARE(received[0][0].toByteArray(), QByteArray("response"));
    channel->write("query");
    QTRY_VERIFY(([&] { std::lock_guard lock(Fake::mutex); return Fake::s.output == "query"; })());
    channel->close(); channel->open("invalid"); QTRY_COMPARE(failed.count(), 1);
    channel->open("COM3"); QTRY_COMPARE(opened.count(), 2);
    channel.reset(); QCOMPARE(Fake::s.closes, 2);
  }
  void staleGenerationIsIgnored() {
    auto channel = std::unique_ptr<UsbChannel>(nativeUsbChannel(nullptr));
    QSignalSpy opened(channel.get(), &UsbChannel::opened), failed(channel.get(), &UsbChannel::failed);
    channel->open("invalid"); channel->open("COM1"); channel->close();
    // Destruction drains the worker queue; queued signals from all old requests
    // must not be delivered to consumers after close increments the generation.
    QTest::qWait(80); QCOMPARE(opened.count(), 0); QCOMPARE(failed.count(), 0);
    channel.reset(); QCOMPARE(Fake::s.closes, 1);
  }
};
QTEST_GUILESS_MAIN(SerialContract)
#include "usb_serial_contract_test.moc"
