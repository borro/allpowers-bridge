# Building

## Tools and presets

Git, CMake 3.26+, Ninja and a C++17 compiler are required; CMake 3.31.6 and Ninja
1.11.1 were tested locally. No Python or uv wrapper drives the build, tests or
packaging. The optional USB maintenance tools are separate; upstream Arduino
SDK tools may internally use their own Python.
Git is also used by the publication regression tests when rebuilding a source kit
without its original `.git` directory.

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

Presets explicitly select the shared `build/deps` cache and `dist` output folder.
Without a preset, `cmake -S <source> -B <binary> -G Ninja` keeps dependency caches
and release output under the binary directory. `ALLPOWERS_DEPS_DIR` and
`ALLPOWERS_RELEASE_DIR` override those locations. In-source builds are rejected.
`BUILD_TESTING=OFF` disables native/Qt test targets and the Qt Test dependency.
The ordinary default build includes the Windows applications when
`ALLPOWERS_BUILD_WINDOWS=ON`; firmware remains explicitly selected through the
board targets or `products`. Release/source-kit validation is an explicit action.
Ordinary builds compile and stage binaries; tests run through CTest. The `release`
target additionally requests the external project's test step when testing is
enabled, and always rebuilds/tests the corresponding-source archive before CPack.

## Static Qt dependency

The windows target uses ExternalProject for Qt Base and Qt Connectivity 6.8.3,
verified with SHA256 from cmake/Dependencies.cmake. It builds Release static
libraries, bundled third-party libraries and the /MT C++ runtime. WinRT BLE is
required; the MinGW Bluetooth dummy backend is rejected. No QML or web runtime
is used. The first build takes longer; subsequent builds reuse the static SDK.

ALLPOWERS_QT_ROOT selects a compatible existing static MSVC Qt installation.
ALLPOWERS_QT_SOURCE_CACHE stores verified upstream archives and extracted notices.
Generated resources and staging remain in the selected binary directory. With
the presets, both EXEs are installed in `dist/windows/AllpowersMonitor/`. Windows system DLLs
remain OS dependencies. No bundled DLLs are unpacked at startup.

CTest runs native UI/history/graph tests and checks the EXE's PE imports. It then
copies only the EXE to an isolated directory, clears Qt search paths, uses a
system-only PATH and runs the offline UI/SQLite test. BLE hardware tests are
explicit manual commands, never CTest steps.

The managed Qt install lives in the selected binary directory and retains its
ExternalProject producer targets after installation. `ALLPOWERS_QT_ROOT` instead
selects an explicitly supplied SDK. CMake 3.26 is the minimum because installed
ExternalProject outputs use `INSTALL_BYPRODUCTS`. WDK preparation and missing Qt
source downloads run as build steps, not during configuration. Configuring the
standalone application/driver therefore requires their dependencies to be
prepared first; the root build performs that preparation automatically.

MSVC header tracking uses Ninja's normal compiler dependency database. A small
probe in `WindowsCompiler.cmake` preserves the raw `/showIncludes` prefix on
localized Windows, where CMake's automatic output decoding can corrupt it.
Header changes no longer alter artificial compile definitions or trigger CMake
reconfiguration and recompilation of every application source.

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

The release target builds/stages Windows, runs its enabled tests and invokes
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

## GitHub Actions and publication

`.github/workflows/build.yml` checks every push, pull request and manual run.
The audit job checks the index and working tree, rejects unexpected public files,
scans reachable history, and runs checksum-pinned Gitleaks. The build matrix uses
Windows Server 2025 (MSVC x64, SDK 10.0.26100.0) and Ubuntu 24.04. Both compile
the core and both firmware profiles, and run CTest. Windows bootstraps static Qt,
builds the driver/monitor/widget and verifies the CPack source-kit rebuild.

Actions are pinned to commit hashes; Dependabot proposes monthly updates. The
workflow has read-only repository permissions, does not persist Git credentials,
and requires no user secrets. Only dependency downloads/Arduino packages are
cached; installed Qt is rebuilt so cached absolute paths cannot break another
runner. Qt defaults to two parallel compiler jobs (`ALLPOWERS_QT_JOBS`).
Artifacts contain the two firmware packages and, on Windows, runtime/source ZIPs
and SHA256 files. They expire after 14 days. No automatic GitHub Release, push,
driver/service installation or device access is part of this workflow.

Local publication checks (use `build/native` on Linux):

```sh
cmake --build build/windows --target audit-worktree
# Review and stage only intended public files, then:
cmake --build build/windows --target audit audit-history audit-secrets
git diff --cached --check
```

`audit` reads staged contents, `audit-worktree` includes untracked public files,
and `audit-history` covers all reachable branches/tags including deleted files.
`audit-secrets` downloads a checksum-verified Gitleaks and scans history, staged
changes and unstaged tracked changes locally. Untracked source files must be
reviewed and staged before the final scan. No source data is uploaded by these
checks. Automated scans reduce risk; inspect new binary assets and personal
content manually as well.

Source ZIPs are assembled from the Git public-file inventory; ignored files are
never copied. Unknown file types/locations fail publication checks. `SOURCE_FILES`
and `VERSION` are generated only inside source kits, allowing them to be rebuilt
and repackaged without `.git`. Their contents are checked again after extraction.
Local `.local/`, SDKs, reports, firmware outputs and recovery storage are not part
of the published repository. Keep private recovery material; do not use a blanket
`git clean -xfd` to prepare a publication.
