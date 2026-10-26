# Building

## Tools and presets

Git, CMake 4.4+, Ninja and a C++23 compiler are required. The pinned build tools
are CMake 4.4.4 and Ninja 1.13.2. No Python or uv wrapper drives the build, tests or
packaging. The optional USB maintenance tools are separate; upstream Arduino
SDK tools may internally use their own Python.
Git is also used by the publication regression tests when rebuilding a source kit
without its original `.git` directory.

On Windows install licensed Visual Studio 2026 with the x64 C++ tools and Windows 11 SDK and open
an x64 Developer PowerShell. CI pins MSVC 19.51 (toolset 14.51) / SDK 10.0.26100.0.
On Linux use a C++23 compiler (GCC 13 or newer). Do not cross-build the Windows desktop.
Visual Studio Community is available for qualifying personal/open-source work;
Build Tools alone does not establish the required developer license. See
[runtime terms](windows-runtime-terms.md) and [firmware source kits](firmware-licenses.md).
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

Both CTest presets run the complete suite with four parallel jobs. Override with
`ctest --preset windows --parallel 1` (or `native`) for a sequential run; no tests
are filtered out. The nested Windows CTest uses two jobs and reserves two parent
slots. The Windows identity graph reserves four slots; native Windows UI tests share
a desktop resource lock. Source-kit validation uses four portable test jobs and
two desktop test jobs as well.

`ALLPOWERS_BUILD_JOBS` bounds each nested application/driver compilation (default
4, CI 2), independently of the outer `cmake --build --parallel N` setting. Qt keeps
its separate two-job default. The generated Windows version header is included
only by its actual consumers, so version changes do not recompile unrelated
backend, widget and test sources. The identity regression retains root and
standalone clean/dirty/revert coverage, verifies unchanged objects, and reports
configure/build/check durations with detailed logs on CI failure.

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
ALLPOWERS_ARDUINO_JOBS controls compiler jobs per board (default 2).
ALLPOWERS_FIRMWARE_PARALLEL_BOARDS bounds concurrent board builds through a Ninja
pool (default 2 locally, 1 in CI; CI also uses two compiler jobs per board).
These limits are independent of application/Qt compilation. Each board owns its
sketch copy and compiler directory. Staging copies only changed contents, removes
deleted inputs and retains timestamps of unchanged inputs and generated identity;
it never clears the compiler cache. Build outputs include a manifest of
source/artifact hashes. USB descriptors, partitions and updater are build gates.
No setup/build/test target opens a USB device or connects BLE.

Keep WSL checkouts and SDKs on the Linux filesystem for faster repeated header
reads; Windows and Linux build/SDK directories are separate.

## Packaging and versions

The local `release` target builds/stages Windows, runs its enabled tests and invokes
CPack. It creates a runtime ZIP and `AllpowersMonitor-<version>-sources-with-qt.zip`
with complete application/Qt sources and SHA256 files. See relinking.md for library
replacement/rebuild instructions. CPack never includes user settings/history or
local device configuration.

Application and managed Qt Release builds optimize size without IPO/LTO, keeping
ordinary relinks fast. CPack compresses only its staged copies of the two Windows
EXEs with checksum-pinned UPX 5.2.1 `-9`; normal build/install outputs remain raw.
The UPX archive is cached in `ALLPOWERS_DEPS_DIR/downloads`, and the executable is
restored from that verified archive for each packaging run. Packed files must
pass UPX integrity, PE/CLI version, system-only import and isolated offline
UI/SQLite/language/license checks, plus exact embedded driver extraction for the
monitor. Failure stops packaging. Compression adds roughly nine seconds per EXE
on the development machine; it never runs after an ordinary relink. UPX packing
must precede any future executable signing. Antivirus false positives are possible.

Version comes from git describe --tags --match v[0-9]* --dirty. Fetch tags in CI.
A clean tagged build displays that tag's numeric version; intermediate builds
carry commit/dirty metadata. The source ZIP receives a generated VERSION fallback,
so it rebuilds without .git. No product version is duplicated in tracked files.
Identity is refreshed during each product build; generated headers/resources are
rewritten only when their contents change. The chosen tag/version is passed to
the desktop and driver projects. Firmware stages an exact source inventory,
keeping its compiler cache separately. Packaging rechecks the current identity
against the configured one and requires reconfiguration after an identity change.
Firmware's generated sorted relative inventory is itself a build input, so a
deletion-only change runs the actual board target even while the version remains
dirty. Unchanged inputs keep their no-op build. Isolated EXE checks read expected
version from that artifact's generated identity at test execution, covering
clean → source-only dirty → clean revert without a manual configure.

Ninja clean removes compiled outputs; build/ and dist/ are disposable. Neither
contains user history or private recovery storage. Do not delete .local/ or the
external preservation folders when cleaning build output.

## GitHub Actions and publication

`.github/workflows/build.yml` checks all branch pushes, tags, pull requests and
manual runs. This also verifies the final commit pushed to a working branch
without requiring a release tag. A PR and its branch push can each produce a run.
After the audit, a portable tier runs core/ABI/UPS tests, RAM-only firmware
self-tests and build regressions on Linux, both normally and with ASan/UBSan.
`-DALLPOWERS_SANITIZERS=ON` adds fatal UBSan (`-fno-sanitize-recover=undefined`)
and ASan. A deliberate recoverable-UB control must fail under those production
flags while retaining its diagnostic; ordinary tests must pass. CI leaves
LeakSanitizer enabled.

An independent `maintenance` job runs
`python3 -I -S -B -m unittest discover -s tests/maintenance -p "test_*.py" -v` with only the
standard library. It installs no HID/serial/esptool backend and performs no
discovery. This checks synthetic image ranges, schema/checksum, history metadata
and rollback rejection before device access. It is separate from the Python-free
CMake product build. Both portable and maintenance jobs gate the complete
Windows/Linux product matrix and therefore tag publication.
New branch/PR runs cancel superseded runs; tag runs do not interrupt an active
release for the same tag. Shared build commands use Bash with fail-fast behavior
on both platforms; Windows-specific scripts and the release job use PowerShell.
The audit job checks the index and working tree, rejects unexpected public files,
scans reachable history, and runs checksum-pinned Gitleaks. The build matrix uses
Windows Server 2025 (Visual Studio 2026, MSVC 14.51 x64, SDK 10.0.26100.0) and
Ubuntu 24.04. CI validates the selected toolset before configuring Qt; it must
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

Before creating a release tag, write `docs/releases/<tag>.md` with the exact first
line `# ALLPOWERS Bridge <tag>`, a bullet list of changes since the previous
release and a GitHub comparison link. The publisher uses this reviewed text above
the shared download information and refuses to create or publish a draft if the
notes are missing, empty or identify another version. A draft retry uses the current
notes; an already published release and its assets remain unchanged on a rerun.

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

Public assets include two standalone EXEs, separate board firmware ZIPs and the
complete application/Qt source ZIP (`AllpowersMonitor-<version>-sources-with-qt.zip`).
One `SHA256SUMS` file covers these five payloads.
The runtime ZIP and checksum sidecars remain internal build/CI artifacts; they
are not uploaded alongside the EXEs and consolidated checksums.
GitHub also provides its own source-code ZIP/tarball from the tag. Those do not
include the bundled Qt sources and source-kit metadata in the full source ZIP.
Firmware packages retain their own sources, relinking kits and license notices
so each board download is self-contained.
The release starts as a draft and
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

## Dependency updates and reproducibility

Review non-Action dependencies monthly alongside Dependabot: Qt, Arduino CLI,
ESP32 core and its transitive tools, MSVC/SDK/WDK, CMake/Ninja, and the optional
maintenance packages. Update the central versions/checksums and relevant licenses
together; inspect upstream release notes and the actual resolved tool/package
inventory. A live Arduino package index alone is not an immutable toolchain lock.
Each update needs both board builds, portable and Qt tests, package/source-kit
rebuilds and a review of cached SDK compatibility. Keep changes reviewable per
dependency instead of silently changing versions during a normal build.

The source-kit checks establish that the product can be rebuilt from its sources.
They do not establish identical ZIP/PE bytes: compiler timestamps and archive
metadata can differ. Reuse the original checked artifacts for a publication retry;
different bytes under an already published version must still be rejected.

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
