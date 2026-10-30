# Building

[Documentation](README.md)

See [CMake architecture](cmake.md) for subsystem ownership, script entry points,
cache contracts and regression checks.

## Tools and presets

Optional parallel commit/push checks are described in [local Git hooks](ci.md#local-git-hooks).

## Complete local verification

From the repository root, including when `build/verify` does not exist:

```sh
cmake --workflow --preset verify
```

Use an x64 Developer PowerShell on Windows, or a Linux compiler environment.
CMake 4.4+, Git, Python 3.10+ and the host compiler must be installed. The workflow
prepares pinned Ninja and build dependencies itself; uncached downloads need
network access on the host performing them, including WSL. It uses `build/verify`, the
shared `build/deps` downloads, and separate result directories for each mode;
existing normal builds are not cleaned. `ALLPOWERS_QT_ROOT` may select an existing
static Qt SDK through a user preset or an initial configure.

The `verify` target builds the portable library/tests, both board firmwares and,
on Windows, the monitor, widget and UPS driver. `check-all` runs the current
configuration's CTest suite (including Qt and build contracts), Python maintenance
tests, formatting controls, working-tree/history/secret audits and whitespace
checks. Separate preset builds run coverage with the existing thresholds,
sanitizers, Valgrind and its failure control, static analysis, mutation controls
and a 30-second fuzz budget per harness. Test binaries are dependencies of
`check`; successful checks are never cached as stamps. Child builds use at most
two concurrent configurations per host with two compiler jobs each.

On Linux install GCC 13, Clang 18, clang-tidy-18, Valgrind, Python's venv support
and dpkg-deb for the complete matrix. The existing setup routines supply gcovr,
Cppcheck, Mull, actionlint and ShellCheck. Missing host tools are listed as
`Unavailable` during configuration; a selected check that fails is a failure,
not a skip. Windows-only checks remain in CI on Linux.

Windows also runs the Linux workflow in WSL Ubuntu when CMake 4.4+ is available
there. The existing optional `.local/hooks.json` supplies the distribution,
Linux CMake and existing analysis/coverage tool paths; hook installation is not required. Without WSL,
Linux coverage, sanitizers, Valgrind, mutation, fuzz and workflow lint remain
unavailable locally. No machine-wide packages are installed automatically.

Windows source archives, their rebuild tests and CPack run only after all selected
checks and products succeed. Firmware builds prepare their existing firmware/source
packages. The workflow never publishes, flashes hardware or installs drivers.
For a single already-configured mode use `cmake --build <binary> --target check`.

## Individual builds

Git, CMake 4.4+, Ninja and a C++23 compiler are required. The pinned build tools
are CMake 4.4.4 and Ninja 1.13.2. No Python or uv wrapper drives the build, tests or
packaging. The optional USB maintenance tools are separate; upstream Arduino
SDK tools may internally use their own Python.
Firmware packaging also needs Python 3.10+ for the offline component/notice
inventory, invoked by CMake. It does not drive compilation or access devices.
Release publication and its offline API regression tests use Python 3.10+ as well.
Qt SDK preparation and cache validation use CMake; PowerShell is only needed for
the Windows executable VERSIONINFO check, not for hook orchestration.
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

On Linux, build and test the portable core with:

```sh
cmake --preset native
cmake --build --preset native
ctest --preset native
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
For WSL firmware builds, keep the binary directory and Arduino SDK/cache on the
Linux filesystem. Windows drive mounts can dominate SDK scanning and compiler
startup even when the source checkout itself remains shared with Windows.
`BUILD_TESTING=OFF` disables native/Qt test targets and the Qt Test dependency.
The ordinary default build includes the Windows applications when
`ALLPOWERS_BUILD_WINDOWS=ON`; firmware remains explicitly selected through the
board targets or `products`. Release/source-kit validation is an explicit action.
Ordinary builds compile and stage binaries; tests run through CTest. The `release`
target additionally requests the external project's test step when testing is
enabled, and always rebuilds/tests the corresponding-source archive before CPack.

Both CTest presets run the complete suite with four parallel jobs. Override with
`ctest --preset windows --parallel 1` (or `native`) for a sequential top-level run; no tests
are filtered out. The nested Windows CTest uses four jobs and reserves four parent
slots. The Windows identity graph reserves four slots; native Windows UI tests share
a desktop resource lock. Source-kit validation uses four portable test jobs and
four desktop test jobs as well. The CLI contract suite is split into independent
groups with separate reports, preserving its real five/ten-second deadlines and
all scenarios while allowing CTest to schedule those waits concurrently.

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

## Test coverage

Every push and PR measures **line coverage** with GCC/gcovr on Linux and
MSVC/OpenCppCoverage on Windows. Codecov merges repository-relative files and
lines under the `linux` and `windows` flags. The README badge shows `main`; PR
statuses are informational; mandatory thresholds are enforced locally by CTest,
independently of Codecov availability. Notifications wait for
both reports; old flag results are not carried forward.

The required minimums apply separately to each measured source area:

| Source area | Linux | Windows |
| --- | ---: | ---: |
| `core/` | 100% | 100% |
| `firmware/R2500Adapter/` in host tests | 100% | 98% |
| `apps/windows/` | Not measured | 90% |

`tools/quality/coverage_report.py` owns these thresholds. Exact covered/total counts are
compared before rounding; one area cannot compensate for another. Missing areas,
failed tests and coverage below a threshold fail `coverage-report` and therefore
`CI result`. A threshold failure retains the normalized XML and a PASS/FAIL table
in the summary for diagnosis. Direct report processing requires `--platform linux`
or `--platform windows` so the policy cannot silently be omitted.

The denominator contains product lines represented in measured host binaries:
`core/`, firmware logic included by host tests, and Windows/Qt code linked into
test binaries. Unexecuted instrumented lines count as uncovered. Tests, frozen
fixtures, generated files, Qt and SDKs are excluded. Device-only firmware, kernel
driver execution, code absent from tested binaries and hardware behavior are
**not measured**. This is not a whole-device coverage claim.

Windows coverage also runs six offline contract suites: integration installation
and rollback, serial-port configuration and worker lifetime, battery sampling,
service lifecycle, CLI reports, and service IPC. The native-boundary suites compile
the original production `.cpp` files against test-only Win32 substitutes; the CLI
suite supplies synthetic transports. This executes the production control flow
and counts its original source lines without copying or excluding product code.
It does not validate Windows driver installation, signatures, real SCM operation,
physical serial I/O or Bluetooth interoperability. File writes use temporary
directories; IPC tests use uniquely named local pipes. No test flashes a board,
installs a service/driver, elevates privileges or connects to a station.

Coverage is opt-in (`ALLPOWERS_COVERAGE=ON`) and isolated under `build/coverage-*`.
GCC coverage and sanitizers use separate builds. Windows retains Release static
Qt, `/MT` and WinRT; measured product targets have PDBs and no optimization,
inlining or folding. Ordinary release flags are unchanged. Report processing
needs Python 3.10+; Linux also needs Python's `venv` module. CMake/CTest drive
compilation and tests.

Linux:

```sh
cmake -DWORK=build/coverage-linux/tools/coverage -P cmake/scripts/sdk/SetupCoverage.cmake
cmake --preset coverage-linux
cmake --build --preset coverage-linux
ctest --preset coverage-linux --verbose
```

Windows (x64 Developer PowerShell with Python on PATH):

```powershell
cmake -DWORK=build/coverage-windows/tools/coverage -P cmake/scripts/sdk/SetupCoverage.cmake
cmake --preset coverage-windows
cmake --build --preset coverage-windows
ctest --preset coverage-windows --verbose
```

Pass `-DALLPOWERS_QT_ROOT=<verified-static-Qt-install>` when configuring Windows
to reuse an SDK. CI uses the existing verified Qt cache; a cache miss builds Qt
from source and takes considerably longer.

The `coverage-report` CTest test resets counters, runs suites with the `coverage`
label (including Qt child processes on Windows), and writes `coverage/coverage.xml`,
`coverage/summary.md` and detailed HTML in the build directory. Do not run another
test invocation concurrently in that directory. Coverage uses two test jobs on
Linux and four on Windows. Counter/report discovery skips only the build's root
tool cache; product targets in other directories remain included.

Actions shows covered/measured line counts and saves reports and diagnostics for
14 days. Test/report errors fail CI. Codecov upload failures produce a warning;
reports remain available as artifacts. Failed-test reports are marked incomplete
and never uploaded to Codecov. Branch coverage is not the shared metric.

For initial hosted setup, enable `borro/allpowers-bridge` in Codecov and grant its
GitHub App repository access for PR reporting. Uploads use OIDC (`id-token: write`
only in coverage jobs), without a stored secret. Public fork PRs use tokenless
uploads; enable public tokenless uploads in Codecov if the organization requires
it. Confirm both flags on the first `main` run, a numeric badge and a PR report
before considering hosted setup verified.

## Formatting

CI's mandatory audit job checks every public `apps/windows/*.cpp` and `*.h`
(including subdirectories) against `.clang-format`, using clang-format 22.1.3.
Core, firmware, drivers and test fixtures are outside this initial formatting
scope. The checker uses `--dry-run --Werror`; it never edits sources. CI also
checks that deliberately malformed input fails and valid input passes.

```sh
cmake --preset native
cmake --build --preset native --target format-check
```

The `format-check` target includes the passing and failing controls. The first run downloads the
platform's version- and SHA256-pinned binary into the build's `format/` directory; later runs verify and
reuse that archive. No pip environment is installed. Versions and hashes
are maintained in `cmake/Dependencies.cmake`.

## Mutation testing

Mull 0.35.0 with Clang/LLVM 18 runs on Ubuntu 24.04 amd64 (including WSL).
The official package URL and SHA256 are pinned in `cmake/Dependencies.cmake`;
setup verifies and extracts it into the build directory without running an installer.
Install the Ubuntu toolchain packages once, then use the same CMake entry points as CI:

```sh
sudo apt-get update
sudo apt-get install -y clang-18 libclang-cpp18 llvm-18-dev libclang-18-dev
cmake -DWORK=build/mutation-linux/tools/mull -P cmake/scripts/sdk/SetupMutation.cmake
cmake --preset mutation-linux
cmake --build --preset mutation-linux --parallel 2
```

The preset builds and runs the `mutation-report` target, using an isolated tree
with coverage and sanitizers disabled. Ordinary builds do not require LLVM or Mull.
Five native test programs exercise the core, host firmware logic and portable
`ups_state.h` adapter. The shared core library is scanned separately with both
its test programs. Qt, test sources, dependencies and hardware execution are outside
this scope. Built-in `selfTest()` routines remain included because they also run
in the product; a detected mutation there can reflect the built-in check itself.

Operators cover comparisons, boundaries, arithmetic, bitwise operations and logical
negation. The complete selected scope is checked, including uncovered mutations.
Original tests must pass first. Runs use two workers and a 3-second minimum timeout per
mutant. The CI job has a 30-minute limit; partial runs are failures, not valid scores.

Every invocation starts a fresh SQLite report. Mull merges repeated mutations
across test programs. Both `--timeout` and `--minimum-timeout` are explicit:
the mutant deadline has a 3-second floor (1 second for the synthetic controls).
A fast baseline must not reduce it to tens of milliseconds on a CI runner.
The delayed-failure control checks that a failure is classified as Killed,
while the infinite-loop control must still time out. Results are merged
across test programs before generating `build/mutation-linux/mutation/`:
an offline HTML source report, JSON, SQLite results, logs and `summary.md`. The score is
`(killed + timeout) / all mutants`; timeouts and uncovered mutants are also listed
separately. Empty results or missing core/firmware mutations fail validation.

The `mutation` GitHub Actions job runs on push/PR and manual dispatch. Its summary
and `mutation-linux` artifact (retained 14 days) need no external reporting account.
The reviewed survivors are recorded in `tests/mutation/accepted.json`, with a
reason and category for each mutation. The job fails for any unreviewed survivor,
any uncovered mutation, or a baseline/tool/report error. There is no minimum
percentage: acceptance leaves the original statuses and mutation score unchanged.
The mutation job gates CI result and release publication.

An acceptance matches the exact file, Mull mutation ID (operator and coordinates),
replacement and SHA-256 of the entire source file, normalized to LF. Changing
surrounding source invalidates the acceptance; moving the same operator or losing
coverage cannot silently inherit it. `review.json` and the job summary distinguish
accepted, unexpected and inactive entries. An accepted mutation that is now killed
or removed is inactive, not a failure. Remove obsolete entries after review.
To accept a new survivor, inspect its source and tests, document the invariant or
accepted limitation, and deliberately update its entry and file digest. There is
no automatic baseline refresh. Use survivors to find missing behavioral tests;
do not change production code merely to raise the score.

The initial run found 771 unique mutants (76.91% detected). Tests for exact
freshness/buffer boundaries, independent output flags, unknown output power and
the runtime limit raised detection to 78.86% without changing product code.
Some remaining survivors are equivalent: replacing `>` with `>=` when selecting
the larger of two equal ages in `ups_state.h` leaves the result unchanged.
The ABI boundary at 512 bytes initially looked equivalent because both versions
reject the packet, but changes the observable invalid-frame counter; the review
added a test for that counter instead of classifying it as equivalent.
Equivalent mutations remain visible, not suppressed. A survivor is a review
candidate, not proof of a product bug or of equivalence.

The follow-up review adds behavioral checks for independent legacy output bits,
UPS state combinations, frozen history ordering, a fixed FNV digest vector,
graph age boundaries, picker pages, optional-output draining, STATUS fairness,
strict export deadlines and delimiter recovery after a blocked write. These use
the public portable interfaces; no product implementation or mutation exclusions
are changed. They also run under ASan/UBSan in a separate native build.

With the same 771 mutants, the review detected 104 additional mutations:
**92.35%** (709 killed, 3 timeouts, 59 survivors). The remaining reviewed groups
are 11 equivalent under valid-input invariants, 35 in built-in diagnostics/RAM
fault simulators, 9 defensive branches, and 4 mutants introducing out-of-bounds
accesses. These are analysis categories, not automatic suppressions or a promise
that every survivor outside the equivalent group is harmless.

The subsequent defensive/bounds review adds permanent tests for a flash that
claims to erase but retains occupied slots, STATUS lines of 3071/3072/3073 bytes,
and an exactly 80-byte unterminated command buffer. The ordinary Mull result is
now **92.74%** (712 killed, 3 timeouts, 56 survivors). Seven formatting-boundary
survivors are equivalent under the current formats: record output is bounded by
303 bytes, the export prefix by 175, and all queued export lines by 477, below
the 512/768-byte buffers. The fixed nonempty formats cannot return zero; STATUS
uses only valid row indices. These invariants must be reviewed if formats change.

All four previously identified unsafe mutations were also rebuilt individually
in isolated header copies with Clang 18, `-O0 -g`,
`-fsanitize=address,undefined,bounds`, `-fno-sanitize-recover=all`, and
`-fsanitize-address-field-padding=2`. The unmutated control passed; every mutant
produced a sanitizer diagnostic. The field-padding option is essential for the
export self-test mutation that writes from `input[3]` into the adjacent `memory`
member: ordinary ASan/UBSan missed it. This is a separate local replay, not part
of the Mull score or an automated CI sanitizer mutation pass. Three of those
mutations remain ordinary Mull survivors; the other remaining groups are 18
equivalent mutations (including the seven formatting guards) and 35 mutations
in built-in diagnostics/RAM fault simulators. No exclusions were added.

Review surviving mutations separately from ordinary missing scenarios:

- Equivalent changes need a concrete invariant, such as equal operands in a
  min/max selection or a value already handled by an earlier return.
- Mutations inside startup `selfTest()` routines and their RAM fault simulators
  can weaken the diagnostic while still succeeding on a correct implementation.
  Checking their detection ability requires deliberately defective subjects;
  asserting their loop counts is not a useful product contract.
- Defensive formatting/storage branches may need malformed-provider or failing
  flash scenarios. Their survival is not proof of equivalence.
- A mutant that introduces an out-of-bounds access is unsafe, not equivalent.
  ASan/UBSan passing on the original program does not validate those mutants;
  detecting them reliably needs separate sanitized mutation replay.

## Firmware

```sh
cmake --build build/windows --target arduino-setup
cmake --build build/windows --target waveshare-147 pocket-096
```

Use build/native on Linux. Setup downloads a checksum-verified Arduino CLI 1.5.1
and installs ESP32 Core 3.3.11. ARDUINO_CLI and ARDUINO_DATA_DIR are cache paths;
ALLPOWERS_ARDUINO_JOBS controls compiler jobs per board (default 2).
ALLPOWERS_FIRMWARE_PARALLEL_BOARDS bounds concurrent board builds through a Ninja
pool (default 2 locally and in CI, with two compiler jobs per board).
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

CPack and the isolated EXE tests read binaries from the selected build's
`windows-monitor` directory. The shared `dist/windows/AllpowersMonitor/` directory
is an export destination: another build may overwrite it without changing which
binaries and embedded driver payload the selected build packages or tests.

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
clean Р Р†РІР‚В РІР‚в„ў source-only dirty Р Р†РІР‚В РІР‚в„ў clean revert without a manual configure.

Ninja clean removes compiled outputs; build/ and dist/ are disposable. Neither
contains user history or private recovery storage. Do not delete .local/ or the
external preservation folders when cleaning build output.

## Parser fuzzing

The `fuzz` job exercises the portable BLE notification decoder and CDC command
collector/controller with Clang 18 libFuzzer plus fatal ASan/UBSan. It runs on
full push/PR and manual runs and gates release publication. Each target
runs for 120 seconds, with a 5-second per-input timeout and a 1024 MiB RSS limit;
CTest runs both targets in parallel with a 180-second outer timeout per target.
The CI job has a 20-minute limit including dependency installation. Crashes,
failed invariants, sanitizer findings, input timeouts and memory-limit failures
produce a nonzero test result. Successful bounded fuzzing is not proof that all
inputs or device behavior are correct.
A separate `fuzz-policy` control requires a good input to pass and a deliberate
division by zero to fail with a UBSan diagnostic and an exact saved reproducer.

BLE checks all four model selections, unchanged output on rejection, and decoded
field invariants. CDC covers direct commands and byte-by-byte collection, blocked
and short writes, and expiration with an empty in-memory archive. It does not
exercise BLE/USB hardware, Windows/Qt, or arbitrary archive/storage histories.

Install Ubuntu's `clang-18` and `libclang-rt-18-dev`, then run:

```sh
cmake --preset fuzz-linux
cmake --build --preset fuzz-linux --parallel 2
ctest --preset fuzz-linux --verbose
```

Only fuzz targets receive libFuzzer/ASan/UBSan flags. Coverage, mutation and ordinary
sanitizer builds must use separate directories. `tests/fuzz/seed_corpus.py` creates
reviewable protocol seeds (valid packets/commands, overflow and malformed cases)
under `build/fuzz-linux/fuzz/{ble,cdc}/corpus`; it does not delete local discoveries.
CI starts from these seeds on each fresh checkout and uses seed `20261027`.
Elapsed-time scheduling can still change the number of explored inputs.

The `fuzz-linux` artifact retains corpus, `crashes/` inputs and verbose CTest logs
for 14 days, including on failure. Investigate an input before adding it to the
seed generator or making it a permanent regression test. To reproduce a saved
CDC failure after rebuilding the same revision:

```sh
build/fuzz-linux/tests/fuzz/fuzz_cdc path/to/crash-input -runs=1
```

For a BLE failure use `fuzz_ble`. Replay uses the exact saved bytes rather than
depending on timing or re-running the entire random search.

## Static analysis

On Linux x64, install Clang/clang-tidy 18, GCC 13, Python 3, Ninja and the pinned
CMake. CMake builds checksum-pinned Cppcheck 2.22.0 on first use:

```sh
cmake --preset static-analysis-linux
cmake --build --preset static-analysis-linux --parallel 4
cmake --build build/static-analysis-linux --target workflow-lint
```

The `static-analysis` target always reruns both analyzers on `core/native/api.cpp`
and the five native test translation units. Their included product headers cover
host firmware logic. Test harness findings and external headers are not product
findings; parse/tool failures still fail the run. A tidy failure does not skip
Cppcheck. Cppcheck uses its own parser, C++23/unix64 and the actual compilation
database, with error/warning/portability diagnostics and exhaustive branch analysis
of that configuration, without enumerating other macro configurations. Style,
performance and inconclusive checks are disabled. Exact tidy rules live in
`.clang-tidy`; no automatic fixes run. Temporary Cppcheck analysis data is discarded.

In an MSVC developer shell, enable analysis in the ordinary Windows build:

```powershell
cmake --preset windows -DALLPOWERS_STATIC_ANALYSIS=ON
cmake --build --preset windows --parallel 2
cmake --build build/windows --target static-analysis
# Validate existing reports without rebuilding (also usable after a build failure):
cmake --build build/windows --target static-analysis-report
ctest --test-dir build/windows -R static-analysis-controls --output-on-failure
```

The option defaults to OFF, propagates to apps/driver and cannot combine with
coverage, sanitizers, mutations or fuzzing. MSVC uses NativeMinimumRules with
C26819 disabled and C26800 enabled. Only first-party source files receive analysis
flags; generated Qt code and dependencies do not. Per-target/per-source SARIF
files avoid parallel write races, and changing analysis rules recompiles affected
sources. The report gate rejects missing target manifests or unit reports;
ordinary compiler warnings remain warnings. Linux always reanalyzes every unit;
MSVC uses normal incremental compilation and validates all expected reports.

`workflow-lint` is available in any Linux root build (use WSL on Windows), and
downloads checksum-pinned actionlint/ShellCheck. Missing tools are errors. Its
control fixtures verify a valid workflow, invalid `needs` and an unsafe Bash
command substitution at warning severity. C++ controls verify valid code and a
null dereference; MSVC controls additionally test parallel reports, changed rules
and damaged/missing SARIF. Parser/CI-policy tests run with the maintenance suite.

Reports are in the build's `analysis/` or `workflow-lint/` directory. These checks
do not cover device-only ESP32 execution or hardware, and do not enable Clazy,
CodeQL, PVS-Studio or Coverity. Any suppression must name one rule at its exact
source location and explain why the reported path is impossible.

## Memory checking with Valgrind

The `valgrind` job runs the five portable native tests through CTest Memcheck on
Ubuntu 24.04, in full push/PR and manual runs. It uses a separate Debug build,
without ASan/UBSan, coverage or Mull instrumentation. It checks the portable
core/shared ABI, UPS logic and host firmware, not Windows/Qt or physical devices.
The supported Valgrind version is recorded in `cmake/Dependencies.cmake` and
verified before testing. Install the Ubuntu `valgrind` package for local use.

```sh
cmake --preset valgrind-linux
cmake --build build/valgrind-linux --target native-tests valgrind_control --parallel 2
cmake --build build/valgrind-linux --target valgrind-policy-check
ctest --test-dir build/valgrind-linux -T memcheck -L coverage \
  --output-on-failure --timeout 180 --parallel 2 \
  --overwrite MemoryCheckCommand=/usr/bin/valgrind \
  --overwrite "MemoryCheckCommandOptions=--tool=memcheck --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=99 --num-callers=30"
```

The `coverage` label selects the existing native test suite; this invocation
does not measure coverage. Memory errors and definite/indirect/possible leaks
fail the job. Still-reachable allocations are reported for review. No project
suppression file is used. A deliberately uninitialised read must be diagnosed
with exit code 99, while its ordinary execution returns zero; this verifies that
Memcheck findings cannot silently pass. The control is excluded from normal
builds and from the five passing tests.

Logs and CTest XML are retained for 14 days in the `valgrind-linux` Actions
artifact, including on failure. The Valgrind job gates release publication.

## GitHub Actions and publication

The complete job graph, event selection, runner images and cache ownership are
specified in [CI builds and dependency caches](ci.md). Every full run builds the
Windows applications and both Linux firmware profiles alongside portable tests,
sanitisers, coverage, Mull, Valgrind and fuzzing. Maintenance tooling tests run
with the portable job using only Python's standard library; they install no
HID/serial/esptool backend and perform no device discovery.

The audit checks tracked/working files and reachable history with checksum-pinned
Gitleaks. Product builds and analyzers then run in parallel. **CI result** requires
all planned checks, including Windows and Linux coverage, before publication.
Documentation-only runs permit explicit skips; unrelated unexpected skips fail.

Windows uses `windows-2025-vs2026`, MSVC 14.51 x64, SDK 10.0.26100.0 and static
Qt with the WinRT backend. Linux uses `ubuntu-24.04`. CMake/Ninja and dependencies
are pinned in `cmake/Dependencies.cmake`, and actual image/toolchain versions are
recorded in job summaries. Actions are pinned to commit hashes; Dependabot proposes
monthly updates. Product jobs have read-only repository access and no user secrets.

The Windows SDK producer owns the Qt binary, Qt source and WDK archive caches.
Its verified SDK artifact feeds the Release and coverage jobs. Firmware owns the
Arduino and legal-source caches. Component-specific fingerprints and explicit
save steps prevent a faster job from publishing an incomplete shared cache.
Qt fingerprints include actual compiler, header and library contents and absolute
paths; SDKs are validated by source identity and static compile/link/offline probes.
A rejected SDK is quarantined and rebuilt. Objects and ExternalProject stamps are
never shared. Qt keeps its two-process default, and firmware uses two boards with
two compiler processes each. No build installs a driver/service or accesses devices.

Version-tag runs additionally create and verify distribution packages, including
an independent build of the Windows source kit. The release job combines the
Windows applications and Linux firmware from that same run. Diagnostic and release transport artifacts expire after 14 days; the temporary
SDK transport expires after one day. Branch/PR runs do not upload product
distribution packages. See below for release notes and the public asset contract.

## Tagged releases

Before creating a release tag, write `docs/releases/<tag>.md` with the exact first
line `# ALLPOWERS Bridge <tag>`, a bullet list of changes since the previous
release and a GitHub comparison link. The publisher uses this reviewed text above
the shared download information and refuses to create or publish a draft if the
notes are missing, empty or identify another version. A draft retry uses the current
notes; an already published release and its assets remain unchanged on a rerun.

A push of a SemVer tag (`v1.2.3`, `v1.2.3-alpha.1`) containing this workflow
publishes a GitHub Release after the audit and both platform builds/tests pass.
Manual workflow dispatch runs checks without publication. Branch/PR
events cannot publish. Only the release job receives `contents: write`, using
the built-in `GITHUB_TOKEN`; no personal token is needed. The official
`download-artifact` action retrieves artifacts from the same workflow run without
an additional GitHub token or `actions: read` permission.

Tag builds pass `ALLPOWERS_RELEASE_TAG` to CMake. It must resolve to the actual
clean checkout's HEAD and be a valid version fitting Windows VERSIONINFO.
`ALLPOWERS_RELEASE_COMMIT` pins the checkout to the tag-push event's Git object,
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
Firmware ZIPs contain installation images, a matching manifest, a `licenses/`
folder and `FIRMWARE-SOURCES.md`. `licenses/README.md` maps linked components to
unique license texts and retained copyright/NOTICE records; all these files are
covered by the manifest hashes. The large generated source/object kit remains an
internal build byproduct. Public source directions link the exact GitHub release
commit and pinned upstream source archives with SHA256 checksums; use the tagged
Git clone for rebuilding. GitHub's source snapshot alone does not contain the
external Arduino/ESP-IDF sources or Git version/inventory metadata. See
[firmware rebuilding and modification](firmware-licenses.md).
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

See [CI selection, runner images and cache ownership](ci.md) for branch/PR deduplication, documentation-only checks and release gates.
