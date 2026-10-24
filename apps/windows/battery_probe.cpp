#include "battery_probe.h"
#include "../../drivers/windows/bridge.h"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <winioctl.h>
#include <poclass.h>
// clang-format on
namespace {
const GUID sourceGuid = {
    0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};
class PowerEvents : public QAbstractNativeEventFilter {
public:
  QJsonArray events;
  bool nativeEventFilter(const QByteArray &, void *native, qintptr *) override {
    auto *message = static_cast<MSG *>(native);
    if (message->message == WM_POWERBROADCAST && message->wParam == PBT_POWERSETTINGCHANGE) {
      auto *setting = reinterpret_cast<POWERBROADCAST_SETTING *>(message->lParam);
      if (IsEqualGUID(setting->PowerSetting, sourceGuid) && setting->DataLength == sizeof(DWORD)) {
        DWORD source = 0;
        memcpy(&source, setting->Data, sizeof(source));
        events.append(
            QJsonObject{{"at", QDateTime::currentMSecsSinceEpoch()}, {"source", int(source)}});
      }
    }
    return false;
  }
};
QJsonObject sample() {
  QJsonObject result{{"at", QDateTime::currentMSecsSinceEpoch()}};
  SYSTEM_POWER_STATUS system{};
  if (GetSystemPowerStatus(&system)) {
    result["system_ac"] = int(system.ACLineStatus);
    result["system_percent"] =
        system.BatteryLifePercent == 255 ? QJsonValue() : QJsonValue(system.BatteryLifePercent);
    result["system_runtime_seconds"] = system.BatteryLifeTime == DWORD(-1)
                                           ? QJsonValue()
                                           : QJsonValue(double(system.BatteryLifeTime));
  }
  auto handle = CreateFileW(UpsDevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    result["present"] = false;
    result["error"] = int(GetLastError());
    return result;
  }
  DWORD bytes = 0, timeout = 0, tag = 0;
  bool present = DeviceIoControl(handle, IOCTL_BATTERY_QUERY_TAG, &timeout, sizeof(timeout), &tag,
                                 sizeof(tag), &bytes, nullptr) &&
                 tag;
  result["present"] = present;
  result["tag"] = double(tag);
  if (present) {
    BATTERY_QUERY_INFORMATION query{};
    query.BatteryTag = tag;
    query.InformationLevel = BatteryInformation;
    BATTERY_INFORMATION info{};
    if (DeviceIoControl(handle, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), &info,
                        sizeof(info), &bytes, nullptr)) {
      result["capabilities"] = double(info.Capabilities);
      result["full_capacity"] = double(info.FullChargedCapacity);
    }
    BATTERY_WAIT_STATUS wait{};
    wait.BatteryTag = tag;
    BATTERY_STATUS status{};
    if (DeviceIoControl(handle, IOCTL_BATTERY_QUERY_STATUS, &wait, sizeof(wait), &status,
                        sizeof(status), &bytes, nullptr)) {
      result["percent"] = double(status.Capacity);
      result["power_state"] = double(status.PowerState);
      result["voltage_unknown"] = status.Voltage == BATTERY_UNKNOWN_VOLTAGE;
      result["rate_unknown"] = status.Rate == BATTERY_UNKNOWN_RATE;
    }
    query.InformationLevel = BatteryEstimatedTime;
    ULONG seconds = BATTERY_UNKNOWN_TIME;
    if (DeviceIoControl(handle, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), &seconds,
                        sizeof(seconds), &bytes, nullptr))
      result["runtime_seconds"] =
          seconds == BATTERY_UNKNOWN_TIME ? QJsonValue() : QJsonValue(double(seconds));
  }
  CloseHandle(handle);
  return result;
}
} // namespace
int runBatteryProbe(const QString &report, int seconds) {
  if (report.isEmpty() || seconds < 1 || seconds > 120)
    return 2;
  auto *app = QCoreApplication::instance();
  PowerEvents events;
  app->installNativeEventFilter(&events);
  HWND window = CreateWindowExW(0, L"STATIC", L"ALLPOWERS battery probe", 0, 0, 0, 0, 0, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
  auto notification =
      RegisterPowerSettingNotification(window, &sourceGuid, DEVICE_NOTIFY_WINDOW_HANDLE);
  QJsonArray samples;
  samples.append(sample());
  QTimer timer;
  timer.setInterval(1000);
  QObject::connect(&timer, &QTimer::timeout, app, [&] { samples.append(sample()); });
  timer.start();
  QTimer::singleShot(seconds * 1000, app, [&] {
    QFile file(report);
    const auto data =
        QJsonDocument(QJsonObject{{"samples", samples}, {"power_events", events.events}}).toJson();
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    app->exit(ok ? 0 : 3);
  });
  auto result = app->exec();
  app->removeNativeEventFilter(&events);
  if (notification)
    UnregisterPowerSettingNotification(notification);
  if (window)
    DestroyWindow(window);
  return result;
}
