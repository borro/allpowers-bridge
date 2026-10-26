# Experimental Windows UPS integration

[Documentation](README.md)

This is an optional, **lab-only implementation**, not a validated replacement for
a certified UPS. The standalone monitor still works without any service or
driver. Native installation and end-to-end tests are deferred until a disposable
Windows 11 x64 VM is available. No ALLPOWERS service or driver was installed on
the development host. Its Secure Boot, boot options and certificate stores were
not changed.

## Components

```text
Standalone: station → WinRT BLE → shared decoder → Monitor / per-user history
Integrated: station → WinRT BLE → Windows service → KMDF battery miniclass → OS
                                     └→ local named pipe → one or more monitors
```

The same statically linked EXE has GUI and QCoreApplication/SCM service modes.
The LocalSystem service owns the sole BLE connection and common configuration,
SQLite history and rotating logs in `%PROGRAMDATA%/AllpowersMonitor`. Closing a
viewer does not stop it. Window preferences and legacy history remain under
`%LOCALAPPDATA%/AllpowersMonitor`. Combined charts/exports deduplicate overlapping
points, with service history taking priority. Removing integration preserves
both histories, settings and the standalone monitor.

If the service is installed, the monitor uses IPC exclusively. A stopped or
unreachable service produces an error and management controls; the monitor never
silently competes for BLE. The GUI's **ИБП Windows** dialog exposes install/update,
start, stop and remove. For this experimental version, administrative operations
are restricted to a VM and installation also requires guest test signing and a
trusted signed payload. The default build embeds an **unsigned** package; it can
be inspected/extracted, but cannot be installed by this installer.

All local interactive users can view data, scan and change the one shared station.
Selection changes apply to the whole computer. The byte-stream named pipe is
local-only, version 1, limited to 8 viewers, 64 KiB frames, 128 history points per
page and 64 requests per client per second. Clients verify the server PID against
SCM. There is no network listener, file-path IPC or IPC command for synthetic
telemetry. The common directory allows writes only by System/Administrators.

## Battery contract

`core/src/UpsState.h` is shared by the real driver and native tests. It does not
change telemetry C ABI v1 or any firmware contract. The private driver bridge is
a fixed 40-byte, pointer-free version-1 state with generation, sequence and TTL.
Only the exclusive System writer can submit updates; ordinary users can read
driver diagnostics. Invalid sizes, versions, ranges, old generations and repeated
sequence numbers are rejected. Closing the publisher invalidates its epoch.

The Battery Class miniclass reports a system battery, short-term reserve and
relative capacity: full=100, current=station percentage. Watts are never used as
relative-capacity rate. Voltage/rate and unavailable runtime use native unknown
sentinels. A known runtime is in seconds. AC output being off does not hide the
station. No critical/shutdown bit or station control command is synthesized.
The Windows power policy owns battery actions.

Both SOC and a decoded mains/battery source must be fresh. Runtime is optional.
R2500 V2 obtains source from a separate settings packet, so both packet ages are
checked; other profiles qualify only when their decoder explicitly supplies the
same capability. Zero charging watts is not evidence of missing mains. Today,
only R2500 V2 has a confirmed source decoder. There is no battery before valid
data, or once required data reaches 15 seconds old. Returning data gets a new
battery tag; switching station advances the connection generation. A passive
one-shot KMDF watchdog rearms every second and expires data even if the service
hangs. Missing data never becomes an artificial 0% or power-loss event.

The driver is adapted from Microsoft's SimBatt registration pattern and is
distributed under **MS-PL**; see `drivers/windows/LICENSE.MS-PL.txt`. Other original
project code remains MIT.
References: [Microsoft battery samples](https://learn.microsoft.com/en-us/windows-hardware/drivers/samples/battery-driver-samples),
[Battery information](https://learn.microsoft.com/en-us/windows/win32/power/battery-information-str),
[KMDF timer requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-timers).

## Build and offline checks

Use x64 MSVC and **Windows SDK 10.0.26100.0**. WDK NuGet
10.0.26100.6584 is downloaded with a pinned SHA256; KMDF is 1.31. Versions/hashes
are in `cmake/Dependencies.cmake`. No driver is installed by these commands:

```powershell
cmake --preset windows
cmake --build build/windows --target ups-driver
cmake --build --preset windows
ctest --preset windows
cmake --build --preset release
```

`ups-driver` produces `build/windows/ups-driver/package`. InfVerif and Inf2Cat
run during the build. PE checks enforce x64/native kernel imports. The Windows
EXE embeds SYS/INF/CAT as resources; extracting them during CTest must reproduce
the package hashes. Static-EXE checks use an isolated directory/system-only PATH
and also verify that `--service` refuses an ordinary console launch before any
BLE backend is created.

Portable tests exercise the same driver-state validator/cache: percent and source,
unknown runtime, required-field freshness, generation/tag changes, replay rejection,
and expiry. Qt tests use a fake offline transport with real local pipes, workers
and SQLite; they cover multiple viewers, continued history after window close,
invalid IPC and merging legacy/service history. These tests do not load kernel
code and do not prove native Battery Class behavior or service-mode WinRT BLE.

## Deferred VM validation

The driver is experimental. VM runtime validation remains outstanding; unverified
VM provisioning, test-signing and replay scripts are not shipped with the public
repository. Building the driver and parsing helper scripts do not prove that it
works inside the Windows kernel.

Further validation requires a disposable Windows 11 x64 guest and a checkpoint,
Microsoft SDK signing tools, a guest-only test certificate and a signed driver
package. Keep guest test-signing changes and certificates inside that guest.
A privately signed payload can be selected with
-DALLPOWERS_DRIVER_PACKAGE_DIR=<signed-package-directory> before rebuilding the
monitor. Do not copy the guest private key or trust its certificate on the host.

The remaining checks are installation/update/rollback/removal, Battery Class
IOCTLs, unknown and stale readings, connection loss, sleep/resume, Driver Verifier
and Windows power policy. Synthetic replay support in the application is a test
facility, not a verified test procedure. Real service BLE also requires a proven
guest-visible adapter: Hyper-V device redirection alone is not evidence of BLE
access from LocalSystem. Verify that path separately before login and after
logout/resume. No provisioning or hardware test runs as part of CMake or CI.

CLI reference (administrative actions are lab-gated):

```text
--extract-driver <directory>               export embedded payload only
--integration-action install|remove|start|stop
--service                                  SCM entry point
--service --lab-replay                      guest/test-signing-gated synthetic input
--battery-probe <1..120 seconds> --report <file>
```

The read-only probe does not open BLE. It records unknown/native fields rather
than pretending a driver is installed. Installation/update/removal, live battery
IOCTLs, Windows policy behavior, Driver Verifier and real service BLE are all
**unverified pending VM tests**. Microsoft signing for normal Secure Boot PCs is
a subsequent release step; this implementation never disables security or imports
a test certificate on the host automatically.
