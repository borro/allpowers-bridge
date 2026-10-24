# Experimental Windows UPS integration

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

## Deferred VM procedure

The scripts are preparation tools; their syntax is tested, their actual VM
operations have not yet been verified. Take a checkpoint before installation.
Use a Windows 11 x64 guest (4 vCPU, 8 GB RAM, 80 GB disk). Built-in Hyper-V is an
option on Windows Pro. `tools/Prepare-UpsLab.ps1` can inspect/enable Hyper-V without
rebooting, create the dedicated VM and create a checkpoint. Enabling a host feature
requires a separate explicit operation and may require a later host reboot.
VM preparation is currently deferred by the user.

Hyper-V does not provide ordinary raw USB Bluetooth passthrough. Enhanced-session
device redirection is not proof that a LocalSystem service can use BLE before
login. Real BLE testing needs a demonstrated guest-visible BLE adapter or a
different VM solution with full USB passthrough. Synthetic driver testing can
proceed independently. Do not mark the real BLE path verified if this is blocked.

For later guest tests only:

1. Disable Secure Boot on the **guest** and explicitly enable guest test signing;
   never change the host's boot options. Reboot the guest as required.
2. Copy the package and `tools/Sign-UpsLabPackage.ps1` to the guest. In an elevated
   guest shell, sign the package with its generated non-exportable guest test key.
   The script validates Microsoft SDK tool signatures, trusts the test certificate
   only in the guest, signs SYS, regenerates CAT, signs CAT and verifies both.
3. Copy only the three signed package files back to a private build directory.
   Reconfigure with `-DALLPOWERS_DRIVER_PACKAGE_DIR=<signed-package-directory>`,
   rebuild the EXE, and copy it to the guest. Do not copy/import the private key or
   trust the guest certificate on the host.
4. Run the GUI's integration action in the guest, or elevate
   `AllpowersMonitor.exe --integration-action install`. Signature verification
   precedes installation. The installer uses exact ALLPOWERS hardware/service IDs,
   avoids duplicate devices, preserves the previous executable/driver for rollback,
   and configures service restart on failure. Test update and rollback errors too.
5. Prepare a synthetic scenario with `tools/Test-UpsLab.ps1 -Monitor <exe>
   -Stage PrepareReplay`. It deliberately ends at 0% battery: first disable critical
   power actions **in the guest**, take a checkpoint, then test power policy separately.
   `-Stage Probe` records native battery IOCTLs, `GetSystemPowerStatus` and power
   notifications. `-Stage RestoreRealBle` returns the service to its normal mode.
6. Exercise 100/50/5/0%, mains→battery→mains, unknown time, stale data, service
   stop/crash/hang, station change, sleep, Bluetooth restart, and multiple viewers.
   Test Driver Verifier, uninstall/reinstall and native policy shutdown/hibernate
   only inside the checkpointed guest. Verify standalone mode after removal.
7. Separately verify real BLE from LocalSystem before login, after logout and
   resume. Interactive GUI BLE success is not a substitute for that check.

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
