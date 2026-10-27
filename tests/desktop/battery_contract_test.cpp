#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QAbstractNativeEventFilter>
#include <windows.h>
#include <winioctl.h>
#include <poclass.h>
#include "../../drivers/windows/bridge.h"

namespace Fake {
bool absent, unknown, ioFailure, systemFailure;
int closes, unregisters, destroys;
HANDLE handle = reinterpret_cast<HANDLE>(quintptr(123));
BOOL GetSystemPowerStatus(LPSYSTEM_POWER_STATUS s) {
  s->ACLineStatus = 1; s->BatteryLifePercent = unknown ? 255 : 67;
  s->BatteryLifeTime = unknown ? DWORD(-1) : 3600; return !systemFailure;
}
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD sharing, auto, DWORD mode, DWORD flags, auto) {
  Q_ASSERT(!_wcsicmp(path, UpsDevicePath));
  Q_ASSERT(access == GENERIC_READ && sharing == (FILE_SHARE_READ|FILE_SHARE_WRITE));
  Q_ASSERT(mode == OPEN_EXISTING && flags == 0);
  return absent ? INVALID_HANDLE_VALUE : handle;
}
DWORD GetLastError() { return ERROR_FILE_NOT_FOUND; }
BOOL CloseHandle(HANDLE h) { Q_ASSERT(h == handle); ++closes; return TRUE; }
BOOL DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD, LPVOID out, DWORD, LPDWORD, auto) {
  Q_ASSERT(h == handle);
  if (ioFailure) return FALSE;
  if (code == IOCTL_BATTERY_QUERY_TAG) *static_cast<ULONG *>(out) = 17;
  else if (code == IOCTL_BATTERY_QUERY_INFORMATION) {
    auto *q = static_cast<BATTERY_QUERY_INFORMATION *>(in); Q_ASSERT(q->BatteryTag == 17);
    if (q->InformationLevel == BatteryInformation) {
      auto *s = static_cast<BATTERY_INFORMATION *>(out);
      s->Capabilities = BATTERY_SYSTEM_BATTERY; s->FullChargedCapacity = 100;
    } else {
      Q_ASSERT(q->InformationLevel == BatteryEstimatedTime);
      *static_cast<ULONG *>(out) = unknown ? BATTERY_UNKNOWN_TIME : 1800;
    }
  } else {
    Q_ASSERT(code == IOCTL_BATTERY_QUERY_STATUS);
    Q_ASSERT(static_cast<BATTERY_WAIT_STATUS *>(in)->BatteryTag == 17);
    auto *s = static_cast<BATTERY_STATUS *>(out);
    s->Capacity = 67; s->PowerState = BATTERY_DISCHARGING;
    s->Voltage = BATTERY_UNKNOWN_VOLTAGE; s->Rate = BATTERY_UNKNOWN_RATE;
  }
  return TRUE;
}
HWND CreateWindowExW(DWORD, auto, auto, DWORD, int, int, int, int, auto, auto, auto, auto) {
  return reinterpret_cast<HWND>(handle);
}
HPOWERNOTIFY RegisterPowerSettingNotification(HANDLE, LPCGUID, DWORD) { return handle; }
BOOL UnregisterPowerSettingNotification(HPOWERNOTIFY) { ++unregisters; return TRUE; }
BOOL DestroyWindow(HWND) { ++destroys; return TRUE; }
} // namespace Fake
#define GetSystemPowerStatus Fake::GetSystemPowerStatus
#define CreateFileW Fake::CreateFileW
#define GetLastError Fake::GetLastError
#define CloseHandle Fake::CloseHandle
#define DeviceIoControl Fake::DeviceIoControl
#define CreateWindowExW Fake::CreateWindowExW
#define RegisterPowerSettingNotification Fake::RegisterPowerSettingNotification
#define UnregisterPowerSettingNotification Fake::UnregisterPowerSettingNotification
#define DestroyWindow Fake::DestroyWindow
#include "../../apps/windows/battery_probe.cpp"
#undef GetSystemPowerStatus
#undef CreateFileW
#undef GetLastError
#undef CloseHandle
#undef DeviceIoControl
#undef CreateWindowExW
#undef RegisterPowerSettingNotification
#undef UnregisterPowerSettingNotification
#undef DestroyWindow

class BatteryContract : public QObject {
  Q_OBJECT
private slots:
  void init() {
    Fake::absent = Fake::unknown = Fake::ioFailure = Fake::systemFailure = false;
    Fake::closes = Fake::unregisters = Fake::destroys = 0;
  }
  void measurements() {
    const auto data = sample();
    QVERIFY(data["present"].toBool()); QCOMPARE(data["tag"].toInt(), 17);
    QCOMPARE(data["system_ac"].toInt(), 1); QCOMPARE(data["system_percent"].toInt(), 67);
    QCOMPARE(data["system_runtime_seconds"].toInt(), 3600);
    QCOMPARE(data["percent"].toInt(), 67); QCOMPARE(data["full_capacity"].toInt(), 100);
    QCOMPARE(data["runtime_seconds"].toInt(), 1800);
    QVERIFY(data["voltage_unknown"].toBool()); QVERIFY(data["rate_unknown"].toBool());
    QCOMPARE(Fake::closes, 1);
    Fake::unknown = true; const auto unknown = sample();
    QVERIFY(unknown["system_percent"].isNull()); QVERIFY(unknown["system_runtime_seconds"].isNull());
    QVERIFY(unknown["runtime_seconds"].isNull());
  }
  void unavailable() {
    Fake::absent = true; auto data = sample();
    QVERIFY(!data["present"].toBool()); QCOMPARE(data["error"].toInt(), int(ERROR_FILE_NOT_FOUND));
    QCOMPARE(Fake::closes, 0);
    Fake::absent = false; Fake::ioFailure = true; Fake::systemFailure = true;
    data = sample(); QVERIFY(!data["present"].toBool()); QVERIFY(!data.contains("system_percent"));
    QVERIFY(!data.contains("percent")); QCOMPARE(Fake::closes, 1);
  }
  void powerEvents() {
    PowerEvents filter;
    struct { GUID guid; DWORD length; DWORD value; } setting{sourceGuid, sizeof(DWORD), 2};
    MSG message{}; message.message = WM_POWERBROADCAST; message.wParam = PBT_POWERSETTINGCHANGE;
    message.lParam = reinterpret_cast<LPARAM>(&setting);
    QVERIFY(!filter.nativeEventFilter({}, &message, nullptr));
    QCOMPARE(filter.events.size(), 1); QCOMPARE(filter.events[0].toObject()["source"].toInt(), 2);
    setting.length = 1; filter.nativeEventFilter({}, &message, nullptr);
    setting.length = sizeof(DWORD); setting.guid = {}; filter.nativeEventFilter({}, &message, nullptr);
    message.message = WM_NULL; filter.nativeEventFilter({}, &message, nullptr);
    QCOMPARE(filter.events.size(), 1);
  }
  void reportLifecycle() {
    QTemporaryDir directory;
    QCOMPARE(runBatteryProbe({}, 1), 2); QCOMPARE(runBatteryProbe(directory.filePath("bad"), 0), 2);
    QCOMPARE(runBatteryProbe(directory.filePath("bad"), 121), 2);
    const auto path = directory.filePath("probe.json");
    QCOMPARE(runBatteryProbe(path, 2), 0);
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto report = QJsonDocument::fromJson(file.readAll()).object();
    QVERIFY(report["samples"].toArray().size() >= 2); QVERIFY(report["power_events"].isArray());
    QCOMPARE(Fake::unregisters, 1); QCOMPARE(Fake::destroys, 1);
    QCOMPARE(runBatteryProbe(directory.path(), 1), 3);
    QCOMPARE(Fake::unregisters, 2); QCOMPARE(Fake::destroys, 2);
  }
};
QTEST_GUILESS_MAIN(BatteryContract)
#include "battery_contract_test.moc"
