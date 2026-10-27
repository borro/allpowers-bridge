// Compile the production implementation against a closed Win32 boundary.
// No SCM, driver-store, ACL, elevation or device operation reaches Windows.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <windows.h>
#include <setupapi.h>
#include <wintrust.h>
#include <aclapi.h>
#include <newdev.h>
#include <objbase.h>
#include <sddl.h>
#include <shellapi.h>
#include <softpub.h>
#include "../../drivers/windows/bridge.h"

namespace Fake {
struct State {
  QString fail, root;
  QStringList calls;
  bool vm = true, modern = true, signing = true, service = false, device = false;
  bool trusted = true, reparse = false, rollbackFails = false, processHandle = true;
  DWORD status = SERVICE_STOPPED, error = ERROR_ACCESS_DENIED, pid = 42, exitCode = 0;
  QString inf = "oem12.inf";
} s;
bool ok(const char *name) { s.calls.append(name); return s.fail != name; }
auto handle = reinterpret_cast<HANDLE>(quintptr(123));
DWORD GetLastError() { return s.error; }
BOOL CloseServiceHandle(SC_HANDLE) { return ok("close-service"); }
SC_HANDLE OpenSCManagerW(auto, auto, DWORD) { return ok("scm") ? reinterpret_cast<SC_HANDLE>(handle) : nullptr; }
SC_HANDLE OpenServiceW(SC_HANDLE, LPCWSTR name, DWORD) {
  Q_ASSERT(QString::fromWCharArray(name) == "AllpowersMonitor");
  if (s.service && ok("open-service")) return reinterpret_cast<SC_HANDLE>(handle);
  s.error = ERROR_SERVICE_DOES_NOT_EXIST;
  return nullptr;
}
BOOL ControlService(SC_HANDLE, DWORD action, LPSERVICE_STATUS) {
  Q_ASSERT(action == SERVICE_CONTROL_STOP);
  if (!ok("stop")) return FALSE;
  s.status = SERVICE_STOPPED; return TRUE;
}
BOOL QueryServiceStatusEx(SC_HANDLE, auto, LPBYTE bytes, DWORD, LPDWORD) {
  auto *status = reinterpret_cast<SERVICE_STATUS_PROCESS *>(bytes);
  status->dwCurrentState = s.status; status->dwProcessId = s.pid;
  status->dwWin32ExitCode = 17;
  return ok("query");
}
BOOL StartServiceW(SC_HANDLE, DWORD, auto) {
  if (!ok("start")) return FALSE;
  s.status = s.fail == "startup-exit" ? SERVICE_STOPPED : SERVICE_RUNNING;
  return TRUE;
}
BOOL DeleteService(SC_HANDLE) { return ok("delete-service"); }
SC_HANDLE CreateServiceW(SC_HANDLE, auto, auto, DWORD, DWORD, DWORD, DWORD,
                        LPCWSTR command, auto, auto, auto, LPCWSTR account, auto) {
  Q_ASSERT(QString::fromWCharArray(command).endsWith("\" --service"));
  Q_ASSERT(QString::fromWCharArray(account) == "LocalSystem");
  return ok("create-service") ? reinterpret_cast<SC_HANDLE>(handle) : nullptr;
}
BOOL ChangeServiceConfig2W(SC_HANDLE, DWORD level, LPVOID) {
  return ok(level == SERVICE_CONFIG_SERVICE_SID_INFO ? "sid" : "recovery");
}
BOOL GetNamedPipeServerProcessId(HANDLE, PULONG pid) { *pid = 42; return ok("pipe-pid"); }
UINT GetSystemFirmwareTable(DWORD, DWORD, PVOID bytes, DWORD size) {
  const QByteArray value = s.vm ? "QEMU test machine" : "physical test machine";
  if (bytes) memcpy(bytes, value.data(), qMin(int(size), int(value.size())));
  return ok("firmware") ? UINT(value.size()) : 0;
}
LONG WINAPI query(ULONG, PVOID data, ULONG, PULONG) {
  static_cast<ULONG *>(data)[1] = s.signing ? 2 : 0; return 0;
}
LONG WINAPI version(OSVERSIONINFOW *v) {
  v->dwMajorVersion = 10; v->dwBuildNumber = s.modern ? 22000 : 19045; return 0;
}
HMODULE GetModuleHandleW(LPCWSTR) { return ok("module") ? reinterpret_cast<HMODULE>(handle) : nullptr; }
FARPROC GetProcAddress(HMODULE, LPCSTR name) {
  if (!ok("procedure")) return nullptr;
  return QString(name) == "RtlGetVersion" ? reinterpret_cast<FARPROC>(version)
                                         : reinterpret_cast<FARPROC>(query);
}
LONG WinVerifyTrust(HWND, GUID *, LPVOID data) {
  auto *trust = static_cast<WINTRUST_DATA *>(data);
  return ok(trust->dwStateAction == WTD_STATEACTION_CLOSE ? "trust-close" : "trust") ? 0 : 1;
}
DWORD GetFileAttributesW(LPCWSTR path) {
  if (s.reparse) return FILE_ATTRIBUTE_REPARSE_POINT;
  return QFile::exists(QString::fromWCharArray(path)) ? FILE_ATTRIBUTE_DIRECTORY : INVALID_FILE_ATTRIBUTES;
}
DWORD GetNamedSecurityInfoW(auto, auto, auto, PSID *owner, auto, auto, auto, PSECURITY_DESCRIPTOR *sd) {
  *owner = handle; *sd = nullptr; return ok("owner") ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}
BOOL CreateWellKnownSid(auto, auto, PSID, LPDWORD) { return TRUE; }
BOOL EqualSid(PSID, PSID) { return s.trusted; }
HLOCAL LocalFree(HLOCAL) { ok("free-acl"); return nullptr; }
BOOL ConvertStringSecurityDescriptorToSecurityDescriptorW(auto, DWORD, PSECURITY_DESCRIPTOR *sd, auto) {
  *sd = handle; return ok("parse-acl");
}
BOOL CreateDirectoryW(LPCWSTR path, auto) {
  const auto directory = QDir::fromNativeSeparators(QString::fromWCharArray(path));
  // Fail closed even if production accidentally changes its destination.
  if (!directory.startsWith(s.root + '/')) qFatal("Test attempted a non-temporary write");
  return ok("mkdir") && QDir().mkpath(directory);
}
BOOL GetSecurityDescriptorDacl(auto, LPBOOL, PACL *, LPBOOL) { return TRUE; }
DWORD SetNamedSecurityInfoW(auto, auto, auto, auto, auto, auto, auto) {
  return ok("set-acl") ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}
HDEVINFO SetupDiGetClassDevsW(auto, auto, auto, DWORD) {
  return ok("enumerate") ? handle : INVALID_HANDLE_VALUE;
}
BOOL SetupDiEnumDeviceInfo(HDEVINFO, DWORD index, PSP_DEVINFO_DATA d) {
  d->DevInst = index; return index == 0 && s.device;
}
BOOL SetupDiGetDeviceRegistryPropertyW(HDEVINFO, auto, DWORD, auto, PBYTE bytes, DWORD, auto) {
  const wchar_t ids[] = L"UNRELATED\0ROOT\\ALLPOWERSUPS\0";
  memcpy(bytes, ids, sizeof(ids)); return ok("hardware-id");
}
HKEY SetupDiOpenDevRegKey(auto, auto, DWORD, DWORD, DWORD, DWORD) {
  return ok("registry") ? reinterpret_cast<HKEY>(handle) : reinterpret_cast<HKEY>(INVALID_HANDLE_VALUE);
}
LSTATUS RegQueryValueExW(auto, auto, auto, auto, LPBYTE bytes, LPDWORD) {
  const auto inf = s.inf.toStdWString(); memcpy(bytes, inf.c_str(), (inf.size()+1)*sizeof(wchar_t));
  return ok("inf-path") ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}
LSTATUS RegCloseKey(HKEY) { return ERROR_SUCCESS; }
BOOL SetupGetInfDriverStoreLocationW(auto, auto, auto, PWSTR path, DWORD, auto) {
  wcscpy_s(path, 32768, L"C:\\fake-driver-store\\previous.inf"); return ok("old-inf");
}
BOOL SetupDiCallClassInstaller(DI_FUNCTION action, auto, auto) {
  return ok(action == DIF_REMOVE ? "remove-device" : "register-device");
}
BOOL SetupDiDestroyDeviceInfoList(HDEVINFO) { return ok("close-devices"); }
BOOL SetupUninstallOEMInfW(LPCWSTR, DWORD, auto) { return ok("uninstall-inf"); }
BOOL SetupCopyOEMInfW(auto, auto, DWORD, DWORD, PWSTR path, DWORD capacity, auto, auto) {
  wcscpy_s(path, capacity, L"oem42.inf"); return ok("stage-inf");
}
BOOL SetupDiCreateDeviceInfoW(auto, auto, auto, auto, auto, DWORD, auto) { return ok("create-device"); }
BOOL SetupDiSetDeviceRegistryPropertyW(auto, auto, DWORD, const BYTE *, DWORD) { return ok("set-hardware-id"); }
BOOL UpdateDriverForPlugAndPlayDevicesW(auto, LPCWSTR id, LPCWSTR inf, DWORD, auto) {
  Q_ASSERT(!_wcsicmp(id, UpsHardwareId));
  const bool previous = QString::fromWCharArray(inf).endsWith("previous.inf");
  return ok(previous ? "restore-driver" : "update-driver") && !(previous && s.rollbackFails);
}
HRESULT CoInitializeEx(auto, DWORD) { ok("com-init"); return S_OK; }
void CoUninitialize() { ok("com-close"); }
BOOL ShellExecuteExW(SHELLEXECUTEINFOW *info) {
  Q_ASSERT(QString::fromWCharArray(info->lpVerb) == "runas");
  Q_ASSERT(QString::fromWCharArray(info->lpParameters).startsWith("--integration-action "));
  info->hProcess = s.processHandle ? handle : nullptr; return ok("elevate");
}
DWORD WaitForSingleObject(HANDLE, DWORD) { ok("wait-process"); return WAIT_OBJECT_0; }
BOOL GetExitCodeProcess(HANDLE, LPDWORD code) { *code = s.exitCode; return TRUE; }
BOOL CloseHandle(HANDLE) { return ok("close-process"); }
} // namespace Fake

// All Win32 calls in integration.cpp are redirected, including read-only calls.
#include "integration_win32_redirect.h"
#include "../../apps/windows/integration.cpp"
#include "integration_win32_restore.h"

QString serviceDataDirectory() { return Fake::s.root + "/service-data"; }

class IntegrationContract : public QObject {
  Q_OBJECT
  QTemporaryDir temporary;
  QByteArray oldProgramFiles;
private slots:
  void init() {
    Fake::s = {};
    Fake::s.root = temporary.path() + '/' + QUuid::createUuid().toString(QUuid::Id128);
    QVERIFY(QDir().mkpath(Fake::s.root));
    oldProgramFiles = qgetenv("ProgramFiles");
    qputenv("ProgramFiles", Fake::s.root.toUtf8());
  }
  void cleanup() { qputenv("ProgramFiles", oldProgramFiles); }
  void guards() {
    QVERIFY(integrationAction("bogus").contains("Unknown"));
    QVERIFY(Fake::s.calls.isEmpty());
    Fake::s.vm = false; QVERIFY(integrationAction("install").contains("test VM"));
    Fake::s.vm = true; Fake::s.modern = false;
    QVERIFY(integrationAction("install").contains("Windows 11"));
    Fake::s.modern = true; Fake::s.signing = false;
    QVERIFY(integrationAction("install").contains("test signing"));
    QVERIFY(!Fake::s.calls.contains("update-driver"));
    QVERIFY(!integrationInstalled());
    Fake::s.service = true; QVERIFY(integrationInstalled());
    const auto status = integrationStatus();
    QVERIFY(status["virtual_machine"].toBool());
    QVERIFY(status["embedded_driver"].toBool());
    QVERIFY(!status["test_signing"].toBool());
  }
  void serviceIdentity() {
    Fake::s.service = true; Fake::s.status = SERVICE_RUNNING;
    QVERIFY(isServicePipe(1));
    Fake::s.status = SERVICE_START_PENDING; QVERIFY(isServicePipe(1));
    Fake::s.status = SERVICE_STOPPED; QVERIFY(!isServicePipe(1));
    Fake::s.status = SERVICE_RUNNING; Fake::s.pid = 43; QVERIFY(!isServicePipe(1));
    Fake::s.fail = "pipe-pid"; QVERIFY(!isServicePipe(1));
  }
  void startStop() {
    QVERIFY(integrationAction("start").contains("not installed"));
    QVERIFY(integrationAction("stop").contains("not installed"));
    Fake::s.service = true;
    QCOMPARE(integrationAction("start"), QString());
    QCOMPARE(Fake::s.status, DWORD(SERVICE_RUNNING));
    QCOMPARE(integrationAction("stop"), QString());
    QCOMPARE(Fake::s.status, DWORD(SERVICE_STOPPED));
    Fake::s.fail = "startup-exit";
    QVERIFY(integrationAction("start").contains("startup (17)"));
  }
  void alreadyRunningAndStoppedAreIdempotent() {
    Fake::s.service = true;
    Fake::s.status = SERVICE_RUNNING;
    Fake::s.fail = "start"; Fake::s.error = ERROR_SERVICE_ALREADY_RUNNING;
    QCOMPARE(integrationAction("start"), QString());
    Fake::s.status = SERVICE_STOPPED;
    Fake::s.fail = "stop"; Fake::s.error = ERROR_SERVICE_NOT_ACTIVE;
    QCOMPARE(integrationAction("stop"), QString());
    Fake::s.fail = "query";
    QVERIFY(!isServicePipe(1));
  }
  void upgradeCannotLoseItsRecoveryPackage_data() {
    QTest::addColumn<QString>("failure");
    for (const auto *f : {"enumerate", "registry", "inf-path", "old-inf"})
      QTest::newRow(f) << QString(f);
  }
  void upgradeCannotLoseItsRecoveryPackage() {
    QFETCH(QString, failure);
    Fake::s.service = Fake::s.device = true;
    Fake::s.status = SERVICE_RUNNING; Fake::s.fail = failure;
    QVERIFY(!integrationAction("install").isEmpty());
    QCOMPARE(Fake::s.status, DWORD(SERVICE_RUNNING));
    QVERIFY(!Fake::s.calls.contains("stage-inf"));
    QVERIFY(!Fake::s.calls.contains("update-driver"));
    QVERIFY(!Fake::s.calls.contains("delete-service"));
  }
  void extractionRejectsUnwritableDestinations() {
    QFile blocker(Fake::s.root + "/file");
    QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.write("preserve"); blocker.close();
    QVERIFY(!extractDriverPackage(blocker.fileName()).isEmpty());
    const auto destination = Fake::s.root + "/payload";
    QVERIFY(QDir().mkpath(destination + "/AllpowersUps.sys"));
    QVERIFY(extractDriverPackage(destination).contains("Could not extract"));
    QVERIFY(blocker.open(QIODevice::ReadOnly)); QCOMPARE(blocker.readAll(), QByteArray("preserve"));
    QVERIFY(Fake::s.calls.isEmpty());
  }
  void installAndRemove() {
    QCOMPARE(integrationAction("install"), QString());
    QVERIFY(Fake::s.calls.contains("register-device"));
    QVERIFY(Fake::s.calls.contains("sid"));
    QVERIFY(Fake::s.calls.indexOf("trust-close") < Fake::s.calls.indexOf("stage-inf"));
    QVERIFY(Fake::s.calls.indexOf("update-driver") < Fake::s.calls.indexOf("create-service"));
    const auto executable = installDirectory() + "/AllpowersMonitor.exe";
    QVERIFY(QFile::exists(executable));
    Fake::s.service = true; Fake::s.device = true;
    QCOMPARE(integrationAction("remove"), QString());
    QVERIFY(QFile::exists(executable));
    QVERIFY(!QFile::exists(installDirectory() + "/driver/AllpowersUps.sys"));
    QVERIFY(Fake::s.calls.contains("uninstall-inf"));
  }
  void rollback_data() {
    QTest::addColumn<QString>("failure");
    for (const auto *failure : {"stage-inf", "create-device", "set-hardware-id", "register-device",
          "update-driver", "create-service", "sid", "recovery", "start", "query"})
      QTest::newRow(failure) << QString(failure);
  }
  void rollback() {
    QFETCH(QString, failure);
    Fake::s.fail = failure;
    QVERIFY(!integrationAction("install").isEmpty());
    QVERIFY(!QFile::exists(installDirectory() + "/AllpowersMonitor.exe"));
    QVERIFY(Fake::s.calls.contains("close-devices"));
    if (Fake::s.calls.contains("create-service") && failure != "create-service")
      QVERIFY(Fake::s.calls.contains("delete-service"));
    if (Fake::s.calls.contains("update-driver"))
      QVERIFY(Fake::s.calls.contains("remove-device"));
  }
  void upgradeRollback() {
    QVERIFY(QDir().mkpath(installDirectory()));
    const auto path = installDirectory() + "/AllpowersMonitor.exe";
    QFile old(path); QVERIFY(old.open(QIODevice::WriteOnly)); old.write("previous binary"); old.close();
    Fake::s.service = true; Fake::s.device = true; Fake::s.fail = "recovery";
    QVERIFY(integrationAction("install").contains("recovery"));
    QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(), QByteArray("previous binary")); old.close();
    QVERIFY(Fake::s.calls.contains("restore-driver"));
    QVERIFY(!Fake::s.calls.contains("remove-device"));
    QVERIFY(!Fake::s.calls.contains("delete-service"));
    QCOMPARE(Fake::s.status, DWORD(SERVICE_RUNNING));
    Fake::s.fail.clear(); QCOMPARE(integrationAction("install"), QString());
    QVERIFY(!QFile::exists(path + ".previous"));
  }
  void failBeforeMutation_data() {
    QTest::addColumn<QString>("failure");
    for (const auto *f : {"firmware", "procedure", "scm", "trust", "parse-acl", "mkdir", "set-acl", "enumerate"})
      QTest::newRow(f) << QString(f);
  }
  void failBeforeMutation() {
    QFETCH(QString, failure); Fake::s.fail = failure;
    QVERIFY(!integrationAction("install").isEmpty());
    QVERIFY(!Fake::s.calls.contains("create-service"));
    QVERIFY(!Fake::s.calls.contains("update-driver"));
    if (failure == "trust") QVERIFY(Fake::s.calls.contains("trust-close"));
  }
  void directoryAndRecoveryGuards() {
    Fake::s.reparse = true; QVERIFY(integrationAction("install").contains("reparse"));
    Fake::s.reparse = false; QVERIFY(QDir().mkpath(installDirectory()));
    Fake::s.trusted = false; QVERIFY(integrationAction("install").contains("owned"));
    Fake::s.trusted = true; Fake::s.service = true; Fake::s.device = true;
    Fake::s.inf = "../../foreign.inf";
    QVERIFY(integrationAction("install").contains("rollback"));
    QCOMPARE(Fake::s.status, DWORD(SERVICE_RUNNING));
    QVERIFY(!Fake::s.calls.contains("stage-inf"));
    Fake::s.inf = "oem12.inf"; Fake::s.fail = "recovery"; Fake::s.rollbackFails = true;
    QVERIFY(integrationAction("install").contains("rollback also failed"));
  }
  void removalErrors_data() {
    QTest::addColumn<QString>("failure");
    for (const auto *f : {"stop", "query", "enumerate", "remove-device", "uninstall-inf", "delete-service"})
      QTest::newRow(f) << QString(f);
  }
  void removalErrors() {
    QFETCH(QString, failure); Fake::s.service = true; Fake::s.device = true; Fake::s.fail = failure;
    QVERIFY(!integrationAction("remove").isEmpty());
    if (failure == "stop" || failure == "query") QVERIFY(!Fake::s.calls.contains("enumerate"));
  }
  void elevation() {
    QVERIFY(!elevateIntegrationAction("bogus")); QVERIFY(Fake::s.calls.isEmpty());
    QVERIFY(elevateIntegrationAction("install"));
    QVERIFY(Fake::s.calls.contains("close-process")); QVERIFY(Fake::s.calls.contains("com-close"));
    Fake::s.exitCode = 5; QVERIFY(!elevateIntegrationAction("remove"));
    Fake::s.calls.clear(); Fake::s.fail = "elevate";
    QVERIFY(!elevateIntegrationAction("start"));
    QVERIFY(Fake::s.calls.contains("com-close")); QVERIFY(!Fake::s.calls.contains("wait-process"));
  }
  void missingSystemModule() {
    Fake::s.fail = "module";
    QVERIFY(!supportedWindows());
    QVERIFY(!testSigning());
    QVERIFY(!Fake::s.calls.contains("procedure"));
  }
  void elevationWithoutProcessHandle() {
    Fake::s.processHandle = false;
    QVERIFY(!elevateIntegrationAction("install"));
    QVERIFY(Fake::s.calls.contains("com-close"));
    QVERIFY(!Fake::s.calls.contains("wait-process"));
    QVERIFY(!Fake::s.calls.contains("close-process"));
  }
};
QTEST_GUILESS_MAIN(IntegrationContract)
#include "integration_contract_test.moc"
