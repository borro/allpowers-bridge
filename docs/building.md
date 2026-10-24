# Building

## Tools and presets

CMake 3.25+, Ninja and a C++17 compiler are required; CMake 3.31.6 and Ninja
1.11.1 were tested locally. No Python or uv wrapper drives the build, tests or
packaging. The optional USB maintenance tools are separate; upstream Arduino
SDK tools may internally use their own Python.

On Windows install MSVC 2022 x64 C++ Build Tools with Windows 11 SDK and open
an x64 Developer PowerShell. Local verification used MSVC 19.44 / SDK 10.0.26100.
On Linux use the system C++17 compiler. Do not cross-build the Windows desktop.
The driver additionally pins SDK 10.0.26100.0, WDK NuGet 10.0.26100.6584 and
KMDF 1.31. `ups-driver` runs InfVerif/Inf2Cat and kernel-import checks; the normal
Windows target embeds that unsigned package and tests its extraction hashes.
No build/test operation installs a service or driver. A private VM-signed package
may be supplied with `ALLPOWERS_DRIVER_PACKAGE_DIR`; see [Windows UPS](windows-ups.md).

```sh
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
cmake --build --preset release
```

`native` configures core/tests plus available firmware targets without a desktop.
`windows` also configures the static Qt superbuild. `all-windows` / `all-linux`
build all products supported by that host. `native-tests` builds the C++ tests;
`core` builds the actual shared C ABI library. There is no Linux desktop yet.

Use CMakeUserPresets.json or cache arguments for local paths. Arduino can also
read the ignored .local/arduino.<platform>.json / .local/arduino.json settings.
Nothing identifying a device belongs in a public preset.

## Static Qt dependency

The windows target uses ExternalProject for Qt Base and Qt Connectivity 6.8.3,
verified with SHA256 from cmake/Dependencies.cmake. It builds Release static
libraries, bundled third-party libraries and the /MT C++ runtime. WinRT BLE is
required; the MinGW Bluetooth dummy backend is rejected. No QML or web runtime
is used. The first build takes longer; subsequent builds reuse the static SDK.

ALLPOWERS_QT_ROOT selects a compatible existing static MSVC Qt installation.
ALLPOWERS_QT_SOURCE_CACHE stores verified upstream archives and extracted notices.
Generated resources and all staging remain in build/. The installed runtime is
only dist/windows/AllpowersMonitor/AllpowersMonitor.exe. Windows system DLLs
remain OS dependencies. No bundled DLLs are unpacked at startup.

CTest runs native UI/history/graph tests and checks the EXE's PE imports. It then
copies only the EXE to an isolated directory, clears Qt search paths, uses a
system-only PATH and runs the offline UI/SQLite test. BLE hardware tests are
explicit manual commands, never CTest steps.

## Firmware

```sh
cmake --build build/windows --target arduino-setup
cmake --build build/windows --target waveshare-147 pocket-096
```

Use build/native on Linux. Setup downloads a checksum-verified Arduino CLI 1.5.1
and installs ESP32 Core 3.3.11. ARDUINO_CLI and ARDUINO_DATA_DIR are cache paths;
ALLPOWERS_ARDUINO_JOBS controls compiler jobs per board (default 4). Each board
owns its sketch copy and compiler directory. Build outputs include a manifest of
source/artifact hashes. USB descriptors, partitions and updater are build gates.
No setup/build/test target opens a USB device or connects BLE.

Keep WSL checkouts and SDKs on the Linux filesystem for faster repeated header
reads; Windows and Linux build/SDK directories are separate.

## Packaging and versions

The release target builds Windows, runs its tests, installs the EXE and invokes
CPack. It creates a runtime ZIP and complete application/Qt source ZIP with SHA256
files. See relinking.md for library replacement/rebuild instructions. CPack never
includes user settings/history or local device configuration.

Version comes from git describe --tags --match v[0-9]* --dirty. Fetch tags in CI.
A clean tagged build displays that tag's numeric version; intermediate builds
carry commit/dirty metadata. The source ZIP receives a generated VERSION fallback,
so it rebuilds without .git. No product version is duplicated in tracked files.

Ninja clean removes compiled outputs; build/ and dist/ are disposable. Neither
contains user history or private recovery storage. Do not delete .local/ or the
external preservation folders when cleaning build output.
