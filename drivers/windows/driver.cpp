// Battery Class/KMDF registration follows Microsoft's SimBatt sample:
// Windows-driver-samples bb68ed5e770bce04dc5395ec9258dafab034184d (MS-PL).
// Copyright (c) Microsoft Corporation. Copyright (c) ALLPOWERS contributors.
// This adapted file is distributed under LICENSE.MS-PL.txt.
#include <ntddk.h>
#include <wdf.h>
extern "C" {
#include <batclass.h>
}
#include "bridge.h"
// clang-format off
#include <wmistr.h>
#include <wmilib.h>
// clang-format on

struct Context {
  AllpowersUps::Cache cache;
  WDFWAITLOCK stateLock, classLock;
  PVOID batteryClass;
  WDFTIMER timer;
  WDFFILEOBJECT publisher;
  WMILIB_CONTEXT wmi;
  BOOLEAN wmiRegistered, stopping;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(Context, context);
static uint64_t nowMs() {
  return KeQueryInterruptTime() / 10000;
}
static void notify(Context *c) {
  WdfWaitLockAcquire(c->classLock, nullptr);
  if (c->batteryClass)
    BatteryClassStatusNotify(c->batteryClass);
  WdfWaitLockRelease(c->classLock);
}
BCLASS_QUERY_TAG_CALLBACK queryTag;
BCLASS_QUERY_INFORMATION_CALLBACK queryInformation;
BCLASS_QUERY_STATUS_CALLBACK queryStatus;
BCLASS_SET_INFORMATION_CALLBACK setInformation;
BCLASS_SET_STATUS_NOTIFY_CALLBACK setNotify;
BCLASS_DISABLE_STATUS_NOTIFY_CALLBACK disableNotify;
_Use_decl_annotations_ NTSTATUS queryTag(PVOID value, PULONG tag) {
  auto *c = static_cast<Context *>(value);
  WdfWaitLockAcquire(c->stateLock, nullptr);
  *tag = c->cache.present(nowMs()) ? c->cache.tag : BATTERY_TAG_INVALID;
  WdfWaitLockRelease(c->stateLock);
  return *tag ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}
_Use_decl_annotations_ NTSTATUS queryStatus(PVOID value, ULONG tag, PBATTERY_STATUS status) {
  auto *c = static_cast<Context *>(value);
  WdfWaitLockAcquire(c->stateLock, nullptr);
  NTSTATUS result = STATUS_NO_SUCH_DEVICE;
  if (tag && tag == c->cache.tag && c->cache.present(nowMs())) {
    const auto &s = c->cache.state;
    *status = {};
    if (s.flags & AllpowersUps::Mains)
      status->PowerState |= BATTERY_POWER_ON_LINE;
    if (s.flags & AllpowersUps::Charging)
      status->PowerState |= BATTERY_CHARGING;
    if (s.flags & AllpowersUps::Discharging)
      status->PowerState |= BATTERY_DISCHARGING;
    status->Capacity = s.percent;
    status->Voltage = BATTERY_UNKNOWN_VOLTAGE;
    status->Rate = BATTERY_UNKNOWN_RATE;
    result = STATUS_SUCCESS;
  }
  WdfWaitLockRelease(c->stateLock);
  return result;
}
_Use_decl_annotations_ NTSTATUS queryInformation(PVOID value, ULONG tag,
                                                 BATTERY_QUERY_INFORMATION_LEVEL level, LONG,
                                                 PVOID buffer, ULONG length, PULONG returned) {
  auto *c = static_cast<Context *>(value);
  *returned = 0;
  WdfWaitLockAcquire(c->stateLock, nullptr);
  if (!tag || tag != c->cache.tag || !c->cache.present(nowMs())) {
    WdfWaitLockRelease(c->stateLock);
    return STATUS_NO_SUCH_DEVICE;
  }
  const auto state = c->cache.state;
  WdfWaitLockRelease(c->stateLock);
  BATTERY_INFORMATION info{};
  info.Capabilities = BATTERY_SYSTEM_BATTERY | BATTERY_IS_SHORT_TERM | BATTERY_CAPACITY_RELATIVE;
  info.Technology = 1;
  info.DesignedCapacity = info.FullChargedCapacity = 100;
  info.DefaultAlert1 = 20;
  info.DefaultAlert2 = 5;
  BATTERY_REPORTING_SCALE granularity{1, 100};
  ULONG time =
      state.flags & AllpowersUps::RuntimeKnown ? state.runtimeSeconds : BATTERY_UNKNOWN_TIME;
  const wchar_t name[] = L"ALLPOWERS virtual UPS";
  const wchar_t manufacturer[] = L"ALLPOWERS Monitor";
  // Generation identity changes with station selection, never a physical MAC.
  wchar_t identity[32] = L"ALLPOWERS-";
  for (int i = 0; i < 16; ++i)
    identity[10 + i] = L"0123456789ABCDEF"[(state.generation >> ((15 - i) * 4)) & 15];
  const void *source = nullptr;
  switch (level) {
  case BatteryInformation:
    source = &info;
    *returned = sizeof(info);
    break;
  case BatteryGranularityInformation:
    source = &granularity;
    *returned = sizeof(granularity);
    break;
  case BatteryEstimatedTime:
    source = &time;
    *returned = sizeof(time);
    break;
  case BatteryDeviceName:
    source = name;
    *returned = sizeof(name);
    break;
  case BatteryManufactureName:
    source = manufacturer;
    *returned = sizeof(manufacturer);
    break;
  case BatteryUniqueID:
    source = identity;
    *returned = sizeof(identity);
    break;
  default:
    return STATUS_INVALID_DEVICE_REQUEST;
  }
  if (length < *returned)
    return STATUS_BUFFER_TOO_SMALL;
  RtlCopyMemory(buffer, source, *returned);
  return STATUS_SUCCESS;
}
_Use_decl_annotations_ NTSTATUS setInformation(PVOID, ULONG, BATTERY_SET_INFORMATION_LEVEL, PVOID) {
  return STATUS_NOT_SUPPORTED; // No control commands to station or power policy.
}
_Use_decl_annotations_ NTSTATUS setNotify(PVOID value, ULONG tag, PBATTERY_NOTIFY) {
  ULONG current = 0;
  return NT_SUCCESS(queryTag(value, &current)) && current == tag ? STATUS_SUCCESS
                                                                 : STATUS_NO_SUCH_DEVICE;
}
_Use_decl_annotations_ NTSTATUS disableNotify(PVOID) {
  return STATUS_SUCCESS;
}

EVT_WDF_TIMER watchdog;
_Use_decl_annotations_ void watchdog(WDFTIMER timer) {
  auto *c = context(static_cast<WDFDEVICE>(WdfTimerGetParentObject(timer)));
  WdfWaitLockAcquire(c->stateLock, nullptr);
  const bool expired = c->cache.expire(nowMs());
  if (!c->stopping)
    WdfTimerStart(timer, WDF_REL_TIMEOUT_IN_MS(1000));
  WdfWaitLockRelease(c->stateLock);
  if (expired)
    notify(c);
}
EVT_WDF_DEVICE_FILE_CREATE openFile;
_Use_decl_annotations_ void openFile(WDFDEVICE, WDFREQUEST request, WDFFILEOBJECT) {
  // Opening a standard Battery Class handle does not claim publication.
  WdfRequestComplete(request, STATUS_SUCCESS);
}
EVT_WDF_FILE_CLOSE closeFile;
_Use_decl_annotations_ void closeFile(WDFFILEOBJECT file) {
  auto *c = context(WdfFileObjectGetDevice(file));
  WdfWaitLockAcquire(c->stateLock, nullptr);
  const bool released = c->publisher == file;
  if (released) {
    c->publisher = nullptr;
    c->cache.release();
  }
  WdfWaitLockRelease(c->stateLock);
  if (released)
    notify(c);
}
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ioctl;
_Use_decl_annotations_ void ioctl(WDFQUEUE queue, WDFREQUEST request, size_t, size_t inputLength,
                                  ULONG code) {
  auto *c = context(WdfIoQueueGetDevice(queue));
  NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
  ULONG_PTR information = 0;
  if (code == UpsSetState && inputLength == sizeof(AllpowersUps::State)) {
    AllpowersUps::State *s = nullptr;
    status =
        WdfRequestRetrieveInputBuffer(request, sizeof(*s), reinterpret_cast<void **>(&s), nullptr);
    if (NT_SUCCESS(status)) {
      WdfWaitLockAcquire(c->stateLock, nullptr);
      const auto file = WdfRequestGetFileObject(request);
      // A standard Battery Class client can also open a read/write handle.
      // Only our first valid private update claims the publisher role.
      if (c->publisher && c->publisher != file)
        status = STATUS_SHARING_VIOLATION;
      else if (!file || !c->cache.accept(*s, nowMs()))
        status = STATUS_INVALID_PARAMETER;
      else {
        c->publisher = file;
        status = STATUS_SUCCESS;
      }
      WdfWaitLockRelease(c->stateLock);
      if (NT_SUCCESS(status))
        notify(c);
    }
  } else if (code == UpsGetState) {
    AllpowersUps::State *out = nullptr;
    status = WdfRequestRetrieveOutputBuffer(request, sizeof(*out), reinterpret_cast<void **>(&out),
                                            nullptr);
    if (NT_SUCCESS(status)) {
      WdfWaitLockAcquire(c->stateLock, nullptr);
      *out = c->cache.state;
      if (!c->cache.present(nowMs()))
        out->flags = 0;
      WdfWaitLockRelease(c->stateLock);
      information = sizeof(*out);
    }
  }
  WdfRequestCompleteWithInformation(request, status, information);
}
EVT_WDFDEVICE_WDM_IRP_PREPROCESS preprocess;
_Use_decl_annotations_ NTSTATUS preprocess(WDFDEVICE device, PIRP irp) {
  auto *c = context(device);
  NTSTATUS status = STATUS_NOT_SUPPORTED;
  WdfWaitLockAcquire(c->classLock, nullptr);
  if (c->batteryClass)
    status = BatteryClassIoctl(c->batteryClass, irp);
  WdfWaitLockRelease(c->classLock);
  if (status == STATUS_NOT_SUPPORTED) {
    IoSkipCurrentIrpStackLocation(irp);
    return WdfDeviceWdmDispatchPreprocessedIrp(device, irp);
  }
  return status;
}
WMI_QUERY_REGINFO_CALLBACK wmiInfo;
_Use_decl_annotations_ NTSTATUS wmiInfo(PDEVICE_OBJECT object, PULONG flags, PUNICODE_STRING,
                                        PUNICODE_STRING *path, PUNICODE_STRING,
                                        PDEVICE_OBJECT *pdo) {
  auto device = WdfWdmDeviceGetWdfDeviceHandle(object);
  static UNICODE_STRING registry;
  RtlInitUnicodeString(&registry, WdfDriverGetRegistryPath(WdfGetDriver()));
  *path = &registry;
  *flags = WMIREG_FLAG_INSTANCE_PDO;
  *pdo = WdfDeviceWdmGetPhysicalDevice(device);
  return STATUS_SUCCESS;
}
EVT_WDFDEVICE_WDM_IRP_PREPROCESS systemControl;
_Use_decl_annotations_ NTSTATUS systemControl(WDFDEVICE device, PIRP irp) {
  auto *c = context(device);
  SYSCTL_IRP_DISPOSITION disposition = IrpForward;
  NTSTATUS status = STATUS_NOT_SUPPORTED;
  WdfWaitLockAcquire(c->classLock, nullptr);
  if (c->batteryClass)
    status = BatteryClassSystemControl(c->batteryClass, &c->wmi,
                                       WdfDeviceWdmGetDeviceObject(device), irp, &disposition);
  WdfWaitLockRelease(c->classLock);
  if (disposition == IrpNotCompleted)
    IoCompleteRequest(irp, IO_NO_INCREMENT);
  else if (disposition != IrpProcessed) {
    IoSkipCurrentIrpStackLocation(irp);
    status = WdfDeviceWdmDispatchPreprocessedIrp(device, irp);
  }
  return status;
}
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT initialize;
_Use_decl_annotations_ NTSTATUS initialize(WDFDEVICE device) {
  auto *c = context(device);
  BATTERY_MINIPORT_INFO_V1_1 info{};
  info.MajorVersion = BATTERY_CLASS_MAJOR_VERSION;
  info.MinorVersion = BATTERY_CLASS_MINOR_VERSION_1;
  info.Context = c;
  info.QueryTag = queryTag;
  info.QueryInformation = queryInformation;
  info.SetInformation = setInformation;
  info.QueryStatus = queryStatus;
  info.SetStatusNotify = setNotify;
  info.DisableStatusNotify = disableNotify;
  info.Pdo = WdfDeviceWdmGetPhysicalDevice(device);
  info.Fdo = WdfDeviceWdmGetDeviceObject(device);
  WdfWaitLockAcquire(c->classLock, nullptr);
  auto status = BatteryClassInitializeDevice(reinterpret_cast<PBATTERY_MINIPORT_INFO>(&info),
                                             &c->batteryClass);
  WdfWaitLockRelease(c->classLock);
  if (!NT_SUCCESS(status))
    return status;
  c->wmi.QueryWmiRegInfo = wmiInfo;
  c->wmiRegistered = NT_SUCCESS(
      IoWMIRegistrationControl(WdfDeviceWdmGetDeviceObject(device), WMIREG_ACTION_REGISTER));
  WdfTimerStart(c->timer, WDF_REL_TIMEOUT_IN_MS(1000));
  return STATUS_SUCCESS;
}
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP cleanup;
_Use_decl_annotations_ void cleanup(WDFDEVICE device) {
  auto *c = context(device);
  WdfWaitLockAcquire(c->stateLock, nullptr);
  c->stopping = TRUE;
  WdfWaitLockRelease(c->stateLock);
  WdfTimerStop(c->timer, TRUE);
  if (c->wmiRegistered)
    IoWMIRegistrationControl(WdfDeviceWdmGetDeviceObject(device), WMIREG_ACTION_DEREGISTER);
  WdfWaitLockAcquire(c->classLock, nullptr);
  if (c->batteryClass)
    BatteryClassUnload(c->batteryClass);
  c->batteryClass = nullptr;
  WdfWaitLockRelease(c->classLock);
}
EVT_WDF_DRIVER_DEVICE_ADD addDevice;
_Use_decl_annotations_ NTSTATUS addDevice(WDFDRIVER, PWDFDEVICE_INIT init) {
  WdfDeviceInitSetDeviceType(init, FILE_DEVICE_BATTERY);
  WdfDeviceInitSetCharacteristics(init, FILE_DEVICE_SECURE_OPEN, FALSE);
  DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GR;;;BA)(A;;GR;;;BU)");
  auto status = WdfDeviceInitAssignSDDLString(init, &sddl);
  if (!NT_SUCCESS(status))
    return status;
  DECLARE_CONST_UNICODE_STRING(name, L"\\Device\\AllpowersUps");
  status = WdfDeviceInitAssignName(init, &name);
  if (!NT_SUCCESS(status))
    return status;
  WDF_FILEOBJECT_CONFIG files;
  WDF_FILEOBJECT_CONFIG_INIT(&files, openFile, closeFile, WDF_NO_EVENT_CALLBACK);
  WdfDeviceInitSetFileObjectConfig(init, &files, WDF_NO_OBJECT_ATTRIBUTES);
  WDF_PNPPOWER_EVENT_CALLBACKS pnp;
  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
  pnp.EvtDeviceSelfManagedIoInit = initialize;
  pnp.EvtDeviceSelfManagedIoCleanup = cleanup;
  WdfDeviceInitSetPnpPowerEventCallbacks(init, &pnp);
  status = WdfDeviceInitAssignWdmIrpPreprocessCallback(init, preprocess, IRP_MJ_DEVICE_CONTROL,
                                                       nullptr, 0);
  if (!NT_SUCCESS(status))
    return status;
  status = WdfDeviceInitAssignWdmIrpPreprocessCallback(init, systemControl, IRP_MJ_SYSTEM_CONTROL,
                                                       nullptr, 0);
  if (!NT_SUCCESS(status))
    return status;
  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, Context);
  attributes.ExecutionLevel = WdfExecutionLevelPassive;
  WDFDEVICE device;
  status = WdfDeviceCreate(&init, &attributes, &device);
  if (!NT_SUCCESS(status))
    return status;
  auto *c = context(device);
  c->cache.release();
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = device;
  status = WdfWaitLockCreate(&attributes, &c->stateLock);
  if (!NT_SUCCESS(status))
    return status;
  status = WdfWaitLockCreate(&attributes, &c->classLock);
  if (!NT_SUCCESS(status))
    return status;
  WDF_IO_QUEUE_CONFIG queue;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue, WdfIoQueueDispatchSequential);
  queue.PowerManaged = WdfFalse; // Never pend unhandled battery requests during stop/sleep.
  queue.EvtIoDeviceControl = ioctl;
  status = WdfIoQueueCreate(device, &queue, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
  if (!NT_SUCCESS(status))
    return status;
  WDF_TIMER_CONFIG timer;
  // Passive KMDF timers cannot be periodic. Rearm the one-shot watchdog
  // under stateLock; cleanup disables rearming before waiting for it.
  WDF_TIMER_CONFIG_INIT(&timer, watchdog);
  timer.AutomaticSerialization = FALSE;
  attributes.ExecutionLevel = WdfExecutionLevelPassive;
  status = WdfTimerCreate(&timer, &attributes, &c->timer);
  if (!NT_SUCCESS(status))
    return status;
  DECLARE_CONST_UNICODE_STRING(link, L"\\DosDevices\\AllpowersUps");
  return WdfDeviceCreateSymbolicLink(device, &link);
}
extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" _Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT object,
                                                       PUNICODE_STRING path) {
  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, addDevice);
  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ExecutionLevel = WdfExecutionLevelPassive;
  return WdfDriverCreate(object, path, &attributes, &config, WDF_NO_HANDLE);
}
