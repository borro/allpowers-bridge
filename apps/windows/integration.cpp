#include "integration.h"
#include "../../drivers/windows/bridge.h"
#include "service_host.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <wintrust.h>
#include <aclapi.h>
#include <newdev.h>
#include <objbase.h>
#include <sddl.h>
#include <shellapi.h>
#include <softpub.h>
// clang-format on

namespace {
const GUID batteryClass = {
    0x72631e54, 0x78a4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};
const wchar_t serviceName[] = L"AllpowersMonitor";
struct ScHandle {
  SC_HANDLE value = nullptr;
  ~ScHandle() {
    if (value)
      CloseServiceHandle(value);
  }
};
QString winError(QString operation) {
  return operation + QString(" (Windows %1)").arg(GetLastError());
}
QString installDirectory() {
  return QDir(qEnvironmentVariable("ProgramFiles", "C:/Program Files"))
      .filePath("AllpowersMonitor");
}
bool signature(const QString &path) {
  const auto wide = QDir::toNativeSeparators(path).toStdWString();
  WINTRUST_FILE_INFO file{sizeof(file), wide.c_str(), nullptr, nullptr};
  WINTRUST_DATA trust{};
  trust.cbStruct = sizeof(trust);
  trust.dwUIChoice = WTD_UI_NONE;
  trust.fdwRevocationChecks = WTD_REVOKE_NONE;
  trust.dwUnionChoice = WTD_CHOICE_FILE;
  trust.pFile = &file;
  trust.dwStateAction = WTD_STATEACTION_VERIFY;
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  auto result = WinVerifyTrust(nullptr, &action, &trust);
  trust.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &action, &trust);
  return result == ERROR_SUCCESS;
}
bool labMachine() {
  const DWORD size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
  QByteArray smbios(int(size), '\0');
  if (!size || GetSystemFirmwareTable('RSMB', 0, smbios.data(), size) != size)
    return false;
  auto lower = smbios.toLower();
  return lower.contains("virtual machine") || lower.contains("vmware") ||
         lower.contains("virtualbox") || lower.contains("qemu");
}
bool testSigning() {
  struct Info {
    ULONG length, options;
  } info{sizeof(Info), 0};
  using Query = LONG(WINAPI *)(ULONG, PVOID, ULONG, PULONG);
  const auto module = GetModuleHandleW(L"ntdll.dll");
  if (!module)
    return false;
  auto query = reinterpret_cast<Query>(
      GetProcAddress(module, "NtQuerySystemInformation"));
  return query && query(103, &info, sizeof(info), nullptr) >= 0 && (info.options & 2);
}
bool supportedWindows() {
  using GetVersion = LONG(WINAPI *)(OSVERSIONINFOW *);
  const auto module = GetModuleHandleW(L"ntdll.dll");
  if (!module)
    return false;
  auto getVersion =
      reinterpret_cast<GetVersion>(GetProcAddress(module, "RtlGetVersion"));
  OSVERSIONINFOW version{sizeof(version)};
  return sizeof(void *) == 8 && getVersion && getVersion(&version) >= 0 &&
         version.dwMajorVersion >= 10 && version.dwBuildNumber >= 22000;
}
QString stopService(SC_HANDLE service) {
  SERVICE_STATUS status{};
  if (!ControlService(service, SERVICE_CONTROL_STOP, &status) &&
      GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
    return winError("StopService");
  for (int i = 0; i < 150; ++i) {
    SERVICE_STATUS_PROCESS state{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&state),
                              sizeof(state), &bytes))
      return winError("QueryServiceStatus");
    if (state.dwCurrentState == SERVICE_STOPPED)
      return {};
    QThread::msleep(100);
  }
  return "Service did not stop within 15 seconds";
}
QString waitForService(SC_HANDLE service) {
  for (int i = 0; i < 150; ++i) {
    SERVICE_STATUS_PROCESS state{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&state),
                              sizeof(state), &bytes))
      return winError("QueryServiceStatus");
    if (state.dwCurrentState == SERVICE_RUNNING)
      return {};
    if (state.dwCurrentState == SERVICE_STOPPED)
      return QString("Service exited during startup (%1)").arg(state.dwWin32ExitCode);
    QThread::msleep(100);
  }
  return "Service did not become ready within 15 seconds";
}
QString secureDirectory(const QString &path) {
  const auto wide = QDir::toNativeSeparators(path).toStdWString();
  DWORD attributes = GetFileAttributesW(wide.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return "Refusing a reparse-point installation directory";
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    auto result =
        GetNamedSecurityInfoW(const_cast<LPWSTR>(wide.c_str()), SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd);
    BYTE admin[SECURITY_MAX_SID_SIZE], system[SECURITY_MAX_SID_SIZE];
    DWORD a = sizeof(admin), b = sizeof(system);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin, &a);
    CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &b);
    const bool trusted =
        result == ERROR_SUCCESS && (EqualSid(owner, admin) || EqualSid(owner, system));
    if (sd)
      LocalFree(sd);
    if (!trusted)
      return "Existing installation directory is not owned by Administrators/System";
  }
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)", SDDL_REVISION_1, &sd, nullptr))
    return winError("Directory ACL");
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  if (attributes == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(wide.c_str(), &sa)) {
    LocalFree(sd);
    return winError("CreateDirectory");
  }
  BOOL present = FALSE, defaulted = FALSE;
  PACL acl = nullptr;
  GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
  auto result =
      SetNamedSecurityInfoW(const_cast<LPWSTR>(wide.c_str()), SE_FILE_OBJECT,
                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, acl, nullptr);
  LocalFree(sd);
  return result == ERROR_SUCCESS ? QString() : QString("Directory ACL error %1").arg(result);
}
bool ownDevice(HDEVINFO list, SP_DEVINFO_DATA &device) {
  wchar_t ids[512]{};
  if (!SetupDiGetDeviceRegistryPropertyW(list, &device, SPDRP_HARDWAREID, nullptr,
                                         reinterpret_cast<PBYTE>(ids), sizeof(ids), nullptr))
    return false;
  for (const wchar_t *id = ids; *id; id += wcslen(id) + 1)
    if (!_wcsicmp(id, UpsHardwareId))
      return true;
  return false;
}
QString deviceInf(HDEVINFO list, SP_DEVINFO_DATA &device) {
  auto key = SetupDiOpenDevRegKey(list, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
  if (key == INVALID_HANDLE_VALUE)
    return {};
  wchar_t path[MAX_PATH]{};
  DWORD size = sizeof(path);
  const auto result =
      RegQueryValueExW(key, L"InfPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &size);
  RegCloseKey(key);
  QString name = QString::fromWCharArray(path);
  return result == ERROR_SUCCESS &&
                 QRegularExpression("^oem[0-9]+\\.inf$", QRegularExpression::CaseInsensitiveOption)
                     .match(name)
                     .hasMatch()
             ? name
             : QString();
}
QString storedInf(const QString &published) {
  if (published.isEmpty())
    return {};
  const auto inf = QDir::toNativeSeparators(
                       QDir(qEnvironmentVariable("SystemRoot") + "/INF").filePath(published))
                       .toStdWString();
  wchar_t result[32768]{};
  return SetupGetInfDriverStoreLocationW(inf.c_str(), nullptr, nullptr, result, 32768, nullptr)
             ? QString::fromWCharArray(result)
             : QString();
}
QString removeDevices() {
  auto list = SetupDiGetClassDevsW(&batteryClass, nullptr, nullptr, 0);
  if (list == INVALID_HANDLE_VALUE)
    return winError("Enumerate battery devices");
  QString error;
  QSet<QString> packages;
  for (DWORD i = 0;; ++i) {
    SP_DEVINFO_DATA device{sizeof(device)};
    if (!SetupDiEnumDeviceInfo(list, i, &device))
      break;
    if (ownDevice(list, device)) {
      const auto inf = deviceInf(list, device);
      if (!inf.isEmpty())
        packages.insert(inf);
      if (!SetupDiCallClassInstaller(DIF_REMOVE, list, &device)) {
        error = winError("Remove ALLPOWERS device");
        break;
      }
    }
  }
  SetupDiDestroyDeviceInfoList(list);
  if (error.isEmpty())
    for (const auto &name : packages) {
      const auto wide = name.toStdWString();
      if (!SetupUninstallOEMInfW(wide.c_str(), 0, nullptr))
        return winError("Remove ALLPOWERS driver package");
    }
  return error;
}
} // namespace
bool integrationInstalled() {
  ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
  ScHandle service{scm.value ? OpenServiceW(scm.value, serviceName, SERVICE_QUERY_STATUS)
                             : nullptr};
  return service.value || GetLastError() == ERROR_ACCESS_DENIED;
}
bool isServicePipe(qintptr handle) {
  ULONG process = 0;
  if (!GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(handle), &process))
    return false;
  ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
  ScHandle service{scm.value ? OpenServiceW(scm.value, serviceName, SERVICE_QUERY_STATUS)
                             : nullptr};
  SERVICE_STATUS_PROCESS status{};
  DWORD size = 0;
  return service.value &&
         QueryServiceStatusEx(service.value, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status), sizeof(status), &size) &&
         status.dwProcessId == process &&
         (status.dwCurrentState == SERVICE_RUNNING ||
          status.dwCurrentState == SERVICE_START_PENDING);
}
QJsonObject integrationStatus() {
  return {{"installed", integrationInstalled()},
          {"virtual_machine", labMachine()},
          {"test_signing", testSigning()},
          {"embedded_driver", QFile::exists(":/driver/AllpowersUps.sys")},
          {"data_directory", serviceDataDirectory()},
          {"test_package", true}};
}
QString extractDriverPackage(const QString &directory) {
  if (!QDir().mkpath(directory))
    return "Could not create package directory";
  for (const auto &name :
       {QString("AllpowersUps.sys"), QString("AllpowersUps.inf"), QString("AllpowersUps.cat")}) {
    QFile source(":/driver/" + name);
    if (!source.open(QIODevice::ReadOnly))
      return "Driver payload is missing: " + name;
    QFile target(QDir(directory).filePath(name));
    if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0)
      return "Could not extract " + name;
  }
  return {};
}
QString integrationAction(const QString &operation) {
  if (operation != "install" && operation != "remove" && operation != "start" &&
      operation != "stop")
    return "Unknown integration operation";
  // V1 is a lab-only package. Firmware VM detection is an additional guard,
  // not a way to weaken or change host boot/security settings.
  if (!labMachine())
    return "Experimental integration may only be changed inside the test VM";
  if (!supportedWindows())
    return "Windows 11 x64 is required for this integration package";
  ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS)};
  if (!scm.value)
    return winError("Administrator access is required");
  ScHandle service{OpenServiceW(scm.value, serviceName, SERVICE_ALL_ACCESS)};
  if (operation == "start") {
    if (!service.value)
      return "Service is not installed";
    if (!StartServiceW(service.value, 0, nullptr) &&
        GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
      return winError("StartService");
    return waitForService(service.value);
  }
  if (operation == "stop")
    return service.value ? stopService(service.value) : QString("Service is not installed");
  if (operation == "remove") {
    if (service.value) {
      auto error = stopService(service.value);
      if (!error.isEmpty())
        return error;
    }
    auto error = removeDevices();
    if (!error.isEmpty())
      return error;
    if (service.value && !DeleteService(service.value))
      return winError("DeleteService");
    // The standalone monitor, history and configuration survive removal.
    // Remove only our staged package; never recursively delete an installation folder.
    for (const auto &name : {"AllpowersUps.sys", "AllpowersUps.inf", "AllpowersUps.cat"}) {
      const auto path = QDir(installDirectory()).filePath(QString("driver/") + name);
      if (QFile::exists(path) && !QFile::remove(path))
        return "Integration removed, but a staged driver file could not be deleted";
    }
    return {};
  }
  if (!testSigning())
    return "Enable test signing in the guest Windows first; no boot settings were changed";
  QTemporaryDir verified;
  if (!verified.isValid())
    return "Could not create private verification directory";
  QString error = extractDriverPackage(verified.path());
  if (!error.isEmpty())
    return error;
  if (!signature(verified.filePath("AllpowersUps.sys")) ||
      !signature(verified.filePath("AllpowersUps.cat")))
    return "Driver package is not signed/trusted. Build the VM-signed payload; no service/device "
           "was installed";
  error = secureDirectory(installDirectory());
  if (!error.isEmpty())
    return error;
  const QString package = QDir(installDirectory()).filePath("driver");
  error = secureDirectory(package);
  if (!error.isEmpty())
    return error;
  error = secureDirectory(serviceDataDirectory());
  if (!error.isEmpty())
    return error;
  const bool existed = service.value != nullptr;
  if (existed) {
    error = stopService(service.value);
    if (!error.isEmpty())
      return error;
  }
  auto list = SetupDiGetClassDevsW(&batteryClass, nullptr, nullptr, 0);
  bool created = false;
  SP_DEVINFO_DATA device{sizeof(device)};
  if (list == INVALID_HANDLE_VALUE) {
    if (existed)
      StartServiceW(service.value, 0, nullptr);
    return winError("Enumerate devices");
  }
  bool found = false;
  for (DWORD i = 0; SetupDiEnumDeviceInfo(list, i, &device); ++i)
    if (ownDevice(list, device)) {
      found = true;
      break;
    }
  const QString previousInf = found ? storedInf(deviceInf(list, device)) : QString();
  if (found && previousInf.isEmpty()) {
    SetupDiDestroyDeviceInfoList(list);
    if (existed)
      StartServiceW(service.value, 0, nullptr);
    return "Could not preserve the installed driver package for rollback";
  }
  const QString executable = QDir(installDirectory()).filePath("AllpowersMonitor.exe");
  const QString backup = executable + ".previous";
  bool backedUp = false, copiedExecutable = false;
  if (QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath() !=
      QFileInfo(executable).canonicalFilePath()) {
    QFile::remove(backup);
    if (QFile::exists(executable) && !QFile::rename(executable, backup))
      error = "Could not preserve previous service executable";
    else
      backedUp = QFile::exists(backup);
    if (error.isEmpty()) {
      copiedExecutable = QFile::copy(QCoreApplication::applicationFilePath(), executable);
      if (!copiedExecutable)
        error = "Could not copy service executable";
    }
  }
  for (const auto &name :
       {QString("AllpowersUps.sys"), QString("AllpowersUps.inf"), QString("AllpowersUps.cat")}) {
    if (!error.isEmpty())
      break;
    const auto target = QDir(package).filePath(name);
    if (QFile::exists(target) && !QFile::remove(target))
      error = "Could not replace staged driver file";
    if (error.isEmpty() && !QFile::copy(verified.filePath(name), target))
      error = "Could not stage verified driver package";
  }
  const auto inf =
      QDir::toNativeSeparators(QDir(package).filePath("AllpowersUps.inf")).toStdWString();
  QString newPackage;
  if (error.isEmpty()) {
    wchar_t published[MAX_PATH]{};
    if (SetupCopyOEMInfW(inf.c_str(), nullptr, SPOST_PATH, SP_COPY_NOOVERWRITE, published, MAX_PATH,
                         nullptr, nullptr))
      newPackage = QFileInfo(QString::fromWCharArray(published)).fileName();
    else if (GetLastError() != ERROR_FILE_EXISTS)
      error = winError("Stage driver in Driver Store");
  }
  if (error.isEmpty() && !found) {
    if (SetupDiCreateDeviceInfoW(list, L"ALLPOWERSUPS", &batteryClass, L"ALLPOWERS Virtual UPS",
                                 nullptr, DICD_GENERATE_ID, &device)) {
      const wchar_t ids[] = L"ROOT\\ALLPOWERSUPS\0";
      created =
          SetupDiSetDeviceRegistryPropertyW(list, &device, SPDRP_HARDWAREID,
                                            reinterpret_cast<const BYTE *>(ids), sizeof(ids)) &&
          SetupDiCallClassInstaller(DIF_REGISTERDEVICE, list, &device);
    }
    if (!created)
      error = winError("Register ALLPOWERS device");
  }
  BOOL reboot = FALSE;
  if (error.isEmpty() && !UpdateDriverForPlugAndPlayDevicesW(nullptr, UpsHardwareId, inf.c_str(),
                                                             INSTALLFLAG_FORCE, &reboot))
    error = winError("Install battery driver");
  if (error.isEmpty() && !existed) {
    auto command = ("\"" + QDir::toNativeSeparators(executable) + "\" --service").toStdWString();
    service.value =
        CreateServiceW(scm.value, serviceName, L"ALLPOWERS BLE UPS", SERVICE_ALL_ACCESS,
                       SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                       command.c_str(), nullptr, nullptr, nullptr, L"LocalSystem", nullptr);
    if (!service.value)
      error = winError("CreateService");
    else {
      SERVICE_SID_INFO sid{SERVICE_SID_TYPE_UNRESTRICTED};
      if (!ChangeServiceConfig2W(service.value, SERVICE_CONFIG_SERVICE_SID_INFO, &sid))
        error = winError("Set service SID");
    }
  }
  if (error.isEmpty() && service.value) {
    SC_ACTION restart{SC_ACTION_RESTART, 5000};
    SERVICE_FAILURE_ACTIONSW actions{86400, nullptr, nullptr, 1, &restart};
    if (!ChangeServiceConfig2W(service.value, SERVICE_CONFIG_FAILURE_ACTIONS, &actions))
      error = winError("Set service recovery");
  }
  if (error.isEmpty() && service.value && !StartServiceW(service.value, 0, nullptr) &&
      GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
    error = winError("StartService");
  if (error.isEmpty())
    error = waitForService(service.value);
  if (!error.isEmpty()) {
    if (service.value)
      stopService(service.value);
    if (created)
      SetupDiCallClassInstaller(DIF_REMOVE, list, &device);
    if (!existed && service.value)
      DeleteService(service.value);
    if (!previousInf.isEmpty()) {
      const auto previous = QDir::toNativeSeparators(previousInf).toStdWString();
      if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, UpsHardwareId, previous.c_str(),
                                              INSTALLFLAG_FORCE, &reboot))
        error += "; driver rollback also failed: " + QString::number(GetLastError());
    }
    if (!newPackage.isEmpty()) {
      const auto published = newPackage.toStdWString();
      if (!SetupUninstallOEMInfW(published.c_str(), 0, nullptr))
        error += "; staged package cleanup failed: " + QString::number(GetLastError());
    }
    if (copiedExecutable && !QFile::remove(executable))
      error += "; could not remove the new service executable";
    if (backedUp && !QFile::rename(backup, executable)) {
      error += "; could not restore the previous service executable";
    }
    if (existed)
      StartServiceW(service.value, 0, nullptr);
  } else if (backedUp)
    QFile::remove(backup);
  SetupDiDestroyDeviceInfoList(list);
  return error;
}
bool elevateIntegrationAction(const QString &operation) {
  if (operation != "install" && operation != "remove" && operation != "start" &&
      operation != "stop")
    return false;
  const auto exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
  const auto args = ("--integration-action " + operation).toStdWString();
  SHELLEXECUTEINFOW info{sizeof(info)};
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb = L"runas";
  info.lpFile = exe.c_str();
  info.lpParameters = args.c_str();
  info.nShow = SW_HIDE;
  const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (!ShellExecuteExW(&info) || !info.hProcess) {
    if (SUCCEEDED(com))
      CoUninitialize();
    return false;
  }
  WaitForSingleObject(info.hProcess, INFINITE);
  DWORD result = 1;
  GetExitCodeProcess(info.hProcess, &result);
  CloseHandle(info.hProcess);
  if (SUCCEEDED(com))
    CoUninitialize();
  return result == 0;
}
