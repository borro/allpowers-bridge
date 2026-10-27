#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <windows.h>
#include "../../drivers/windows/bridge.h"
#include "../../apps/windows/integration.h"

namespace Fake {
QString scenario;
QList<SERVICE_STATUS> reports;
LPHANDLER_FUNCTION_EX handler;
int publications = 0, closes = 0;
AllpowersUps::State last{};
HANDLE device = reinterpret_cast<HANDLE>(quintptr(123));
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD, auto, DWORD, DWORD, auto) {
  Q_ASSERT(!_wcsicmp(path, UpsDevicePath)); Q_ASSERT(access == (GENERIC_READ|GENERIC_WRITE));
  return scenario == "driver-missing" ? INVALID_HANDLE_VALUE : device;
}
BOOL DeviceIoControl(HANDLE h, DWORD code, LPVOID data, DWORD size, auto, DWORD, LPDWORD, auto) {
  Q_ASSERT(h == device && code == UpsSetState && size == sizeof(AllpowersUps::State));
  ++publications; last = *static_cast<AllpowersUps::State *>(data); return scenario != "driver-write";
}
BOOL CloseHandle(HANDLE h) { if (h == device) { ++closes; return TRUE; } return ::CloseHandle(h); }
DWORD GetLastError() { return ERROR_ACCESS_DENIED; }
SERVICE_STATUS_HANDLE RegisterServiceCtrlHandlerExW(auto, LPHANDLER_FUNCTION_EX callback, auto) {
  handler = callback;
  return scenario == "register-failed" ? nullptr : reinterpret_cast<SERVICE_STATUS_HANDLE>(device);
}
BOOL SetServiceStatus(SERVICE_STATUS_HANDLE, LPSERVICE_STATUS status) {
  reports.append(*status);
  if (status->dwCurrentState == SERVICE_RUNNING) {
    QTimer::singleShot(1100, QCoreApplication::instance(), [] {
      handler(SERVICE_CONTROL_INTERROGATE, 0, nullptr, nullptr);
      handler(SERVICE_CONTROL_SHUTDOWN, 0, nullptr, nullptr);
    });
  }
  return TRUE;
}
BOOL StartServiceCtrlDispatcherW(const SERVICE_TABLE_ENTRYW *table) {
  if (scenario == "dispatch-failed") return FALSE;
  Q_ASSERT(QString::fromWCharArray(table[0].lpServiceName) == "AllpowersMonitor");
  table[0].lpServiceProc(0, nullptr); return TRUE;
}
HANDLE CreateEventW(auto security, BOOL manual, BOOL initial, auto name) {
  return scenario == "event-failed" ? nullptr : ::CreateEventW(security, manual, initial, name);
}
QJsonObject integrationStatus() {
  return {{"virtual_machine", scenario != "not-lab"}, {"test_signing", true}};
}
} // namespace Fake
#define CreateFileW Fake::CreateFileW
#define DeviceIoControl Fake::DeviceIoControl
#define CloseHandle Fake::CloseHandle
#define GetLastError Fake::GetLastError
#define RegisterServiceCtrlHandlerExW Fake::RegisterServiceCtrlHandlerExW
#define SetServiceStatus Fake::SetServiceStatus
#define StartServiceCtrlDispatcherW Fake::StartServiceCtrlDispatcherW
#define CreateEventW Fake::CreateEventW
#define integrationStatus Fake::integrationStatus
#include "../../apps/windows/service_host.cpp"
#undef CreateFileW
#undef DeviceIoControl
#undef CloseHandle
#undef GetLastError
#undef RegisterServiceCtrlHandlerExW
#undef SetServiceStatus
#undef StartServiceCtrlDispatcherW
#undef CreateEventW
#undef integrationStatus

class ServiceContract : public QObject {
  Q_OBJECT
private slots:
  void publisherRecovers() {
    Fake::publications = Fake::closes = 0;
    Publisher p; AllpowersUps::State state{};
    Fake::scenario = "driver-missing"; p.publish(state);
    QVERIFY(p.error.contains("unavailable")); QCOMPARE(Fake::publications, 0);
    Fake::scenario = "driver-write"; p.publish(state);
    QVERIFY(p.error.contains("update failed")); QCOMPARE(Fake::closes, 1);
    Fake::scenario.clear(); p.publish(state);
    QVERIFY(p.error.isEmpty()); QVERIFY(p.errorMessage.isEmpty()); QCOMPARE(Fake::publications, 2);
  }
  void lifecycle_data() {
    QTest::addColumn<QString>("scenario");
    for (auto *name : {"valid", "aged", "repeat", "missing", "empty", "too-large", "too-many",
       "unordered", "bad-snapshot", "future", "bad-directory", "not-lab", "register-failed",
       "dispatch-failed", "event-failed"}) QTest::newRow(name) << QString(name);
  }
  void lifecycle() {
    QFETCH(QString, scenario);
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--contract-child", scenario});
    QVERIFY(child.waitForStarted()); QVERIFY2(child.waitForFinished(15000), qPrintable(child.errorString()));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
  }
};

int childScenario(int argc, char **argv) {
  Fake::scenario = QString::fromLocal8Bit(argv[2]);
  QTemporaryDir root;
  if (!root.isValid()) return 80;
  qputenv("PROGRAMDATA", root.path().toUtf8());
  const auto directory = root.filePath("AllpowersMonitor");
  if (Fake::scenario == "bad-directory") {
    QFile blocker(directory); if (!blocker.open(QIODevice::WriteOnly)) return 81;
  } else {
    QDir().mkpath(directory);
    if (Fake::scenario != "missing") {
      ap_snapshot data{}; data.abi_version = 1; data.main_seen = data.settings_seen = data.fresh = 1;
      data.known = 513; data.soc = 42; data.source = 2;
      if (Fake::scenario == "aged") data.main_age_ms = 14999;
      QJsonObject row{{"at_ms", 0}, {"snapshot", Ipc::snapshot(data)},
                      {"repeat", Fake::scenario == "repeat"}};
      QJsonArray rows{row};
      if (Fake::scenario == "empty") rows = {};
      if (Fake::scenario == "unordered") { row["at_ms"] = -1; rows.append(row); }
      if (Fake::scenario == "future") { row["at_ms"] = 86400001; rows = {row}; }
      if (Fake::scenario == "bad-snapshot") { row["snapshot"] = QJsonObject{}; rows = {row}; }
      if (Fake::scenario == "too-many") for (int i=0; i<4096; ++i) rows.append(row);
      QFile file(directory + "/lab-replay.json"); if (!file.open(QIODevice::WriteOnly)) return 82;
      file.write(Fake::scenario == "too-large" ? QByteArray(1024*1024+1, ' ') : QJsonDocument(rows).toJson(QJsonDocument::Compact));
    }
  }
  QByteArray name = argv[0], replay = "--lab-replay";
  char *args[]{name.data(), replay.data(), nullptr};
  const int result = runWindowsService(2, args);
  const bool valid = Fake::scenario == "valid" || Fake::scenario == "aged" || Fake::scenario == "repeat";
  if (valid) {
    if (result || Fake::publications < 2 || Fake::closes != 1) return 83;
    if (Fake::last.flags & AllpowersUps::Present) return 84; // shutdown withdraws UPS state
    QList<DWORD> states;
    for (const auto &status : Fake::reports) {
      states.append(status.dwCurrentState);
      if (status.dwCurrentState == SERVICE_RUNNING &&
          status.dwControlsAccepted != (SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN)) return 85;
    }
    if (!states.contains(SERVICE_RUNNING) || !states.contains(SERVICE_STOP_PENDING) || states.last() != SERVICE_STOPPED) return 86;
  } else if (!result) return 87;
  return 0;
}
int main(int argc, char **argv) {
  if (argc > 2 && QByteArray(argv[1]) == "--contract-child") return childScenario(argc, argv);
  QCoreApplication app(argc, argv);
  ServiceContract test; return QTest::qExec(&test, argc, argv);
}
#include "service_contract_test.moc"
