# Building

## Tools and presets

Git, CMake 4.4+, Ninja and a C++23 compiler are required. The pinned build tools
are CMake 4.4.4 and Ninja 1.13.2. No Python or uv wrapper drives the build, tests or
packaging. The optional USB maintenance tools are separate; upstream Arduino
SDK tools may internally use their own Python.
Git is also used by the publication regression tests when rebuilding a source kit
without its original `.git` directory.

On Windows install MSVC 2026 x64 C++ Build Tools with Windows 11 SDK and open
an x64 Developer PowerShell. CI pins MSVC 19.51 (toolset 14.51) / SDK 10.0.26100.0.
On Linux use a C++23 compiler (GCC 13 or newer). Do not cross-build the Windows desktop.
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

The windows target uses ExternalProject for Qt Base and Qt Connectivity 6.12.0,
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
selects an explicitly supplied SDK. The build requires CMake 4.4 or newer. WDK preparation and missing Qt
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

The local `release` target builds/stages Windows, runs its enabled tests and invokes
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

`.github/workflows/build.yml` checks pushes to `main`, all tags, pull requests and
manual runs. Feature branches are checked through PR events, avoiding a second
matrix build for the same branch push. Before opening a PR, use workflow dispatch
to build a feature branch. A merge into `main` still gets its own integration build.
New branch/PR runs cancel superseded runs; tag runs do not interrupt an active
release for the same tag. Shared build commands use Bash with fail-fast behavior
on both platforms; Windows-specific scripts and the release job use PowerShell.
The audit job checks the index and working tree, rejects unexpected public files,
scans reachable history, and runs checksum-pinned Gitleaks. The build matrix uses
Windows Server 2025 (Visual Studio 2026, MSVC 14.51 x64, SDK 10.0.26100.0) and
Ubuntu 24.04. `cmake/SetupMsvc.ps1` locates Visual Studio with Microsoft's installed
`vswhere`, initializes `vcvarsall.bat` with the toolset/SDK pins from
`Dependencies.cmake`, and exports the verified environment through `GITHUB_ENV`.
This setup runs in PowerShell and has no JavaScript action runtime dependency.
CI validates the selected toolset before configuring Qt; it must
not silently switch to a newer Visual Studio installed on the hosted image. Both compile
the core and both firmware profiles, and run CTest. Windows bootstraps static Qt,
builds the driver/monitor/widget and verifies the CPack source-kit rebuild.
Before configuration, `cmake/SetupNinja.cmake` installs the exact Ninja version
from `cmake/Dependencies.cmake` into the runner's temporary directory, verifies
the platform archive's pinned SHA256 and the executable's reported version,
then adds that directory to `GITHUB_PATH`. Windows and Linux builds therefore
use the same pinned Ninja release instead of the runner image's bundled version.

Actions are pinned to commit hashes; Dependabot proposes monthly updates. The
build/test jobs have read-only repository permissions, do not persist Git credentials,
and require no user secrets. Dependency downloads/Arduino packages and the installed
static Qt SDK are cached separately. `cmake/QtCache.ps1` computes an exact SDK key
from the dependency pins and Qt recipe/check scripts, actual compiler SHA256 and
path, full MSVC toolset and Windows SDK/UCRT versions, architecture, CMake/Ninja
versions and absolute SDK install path. There are no partial restore keys. Including
paths avoids treating an SDK built in a different location as relocatable.

Before selecting `ALLPOWERS_QT_ROOT`, CI compares the SDK's compatibility manifest,
checks the exact Qt version, static modules and Core/Bluetooth WinRT features,
then compiles/links a small `/MT` consumer. It checks PE imports and runs offline
Widgets/SQLite/plugin checks with a system-only PATH; it never scans or connects
Bluetooth. A missing, incomplete or rejected cache falls back to the normal Qt
source build. Rejected installs are moved aside inside the build directory.
Only a successfully checked install is saved, before the application/firmware
build, so unrelated later failures do not discard a usable SDK. Object directories
and ExternalProject stamps are never cached. An invalid existing exact-key entry
cannot be overwritten by Actions: remove that entry in GitHub's cache settings
to let the next successful build replace it. Qt source archives remain available
through the separate download cache for notices and source packaging.

Qt defaults to two parallel compiler jobs (`ALLPOWERS_QT_JOBS`).
Qt Core C++/WinRT and the Qt Bluetooth WinRT backend are required explicitly;
unsupported compiler/SDK combinations fail configuration instead of disabling
WinRT and failing later on a missing private Qt header.
Artifacts contain the two firmware packages and, on Windows, runtime/source ZIPs
and SHA256 files. They expire after 14 days. The tag-only release job publishes
the checked files as described below. No build installs a driver/service or accesses devices.

## Tagged releases

A push of a SemVer tag (`v1.2.3`, `v1.2.3-alpha.1`) containing this workflow
publishes a GitHub Release after the audit and both platform builds/tests pass.
Manual workflow dispatch against such a tag uses the same checks. Branch/PR
events cannot publish. Only the release job receives `contents: write`, using
the built-in `GITHUB_TOKEN`; no personal token is needed. The official
`download-artifact` action retrieves artifacts from the same workflow run without
an additional GitHub token or `actions: read` permission.

Tag builds pass `ALLPOWERS_RELEASE_TAG` to CMake. It must resolve to the actual
clean checkout's HEAD and be a valid version fitting Windows VERSIONINFO.
`ALLPOWERS_RELEASE_COMMIT` pins the checkout to the push/dispatch event's Git object,
including when an annotated tag object is supplied.
The requested tag takes precedence over other tags on that commit. The `v`
prefix is removed for the application version, PE ProductVersion/FileVersion,
firmware manifests, filenames and corresponding-source VERSION. Before upload,
both EXEs' complete prerelease strings/numeric versions, source VERSION, both
board manifests, maintenance/partition contracts and all artifact hashes are checked.

Assets include the combined Windows ZIP, two standalone EXEs, separate board
firmware ZIPs and the complete application/Qt source ZIP. Each has a `.sha256`
file; `SHA256SUMS` covers all six payloads. The release starts as a draft and
becomes public only after every uploaded asset's size and server SHA256 match.
Prerelease tags set GitHub's prerelease flag; stable tags become Latest.
SemVer build metadata remains in the embedded version; `+` is encoded as `_`
only in uploaded asset filenames to avoid GitHub filename normalization.

An interrupted draft can be retried for the same commit. Unexpected draft files
or a moved tag stop publication. Published files are never replaced: a rerun
accepts identical assets, and rejects different bytes under an existing release.
Use a new version tag for changed binaries. Existing historical tags are not
rewritten or retroactively published. Pushing a new tag is the explicit publication action.

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

C++23 is selected through CMake compile features and the ESP32 compiler flag.
MSVC currently spells its dedicated C++23 mode `/std:c++23preview`; the
compiler compatibility module selects it instead of enabling draft features
through `/std:c++latest`. Qt uses the same language mode, including its
C++/WinRT configuration probes. C ABI v1 and the firmware USB/flash contracts
remain unchanged.
