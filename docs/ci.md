# CI builds and dependency caches

## Local Git hooks

The optional hooks use CMake for Git snapshots and CTest for parallel scheduling,
failure handling, logs and timings. Install with
`cmake -P cmake/scripts/hooks/InstallGitHooks.cmake` after preparing the coverage,
static-analysis and mutation toolchains for the host.
The installer discovers their paths from the existing CMake caches and writes
ignored `.local/hooks.json`; `-DCONFIG=<file>` can supply explicit local paths.
It sets this checkout's `core.hooksPath` to `.githooks` and refuses to replace
another configured hook setup. It records the native CMake executable in local
Git configuration. Windows uses WSL for Linux checks and a CMake adapter calling
the vendor's MSVC environment batch file; a Developer PowerShell session is unnecessary. All host-specific
integration is confined to `.githooks/`. Linux runs natively without PowerShell
and leaves Windows units/coverage to CI. No Python build wrapper is used.

Check modes come from `CMakePresets.json`; hooks override isolated directories,
local tool paths, concurrency and fuzz duration. Entry points now live under
`cmake/scripts/hooks`, reusable functions under `cmake/modules/hooks`, and the
CTest scheduler under `cmake/projects/HookChecks`. See [CMake structure](cmake.md).

- **Pre-commit**, in parallel: index audit, Windows application formatting,
  secret scanning, staged whitespace checks, portable Windows and Linux units,
  Clang-Tidy/Cppcheck, Linux coverage (100% for core and host-tested firmware),
  ASan/UBSan, and Valgrind including its failing control.
- **Pre-push**, in parallel: mutation tests including their control and survivor
  review, both fuzzers with **30 seconds each** and their fatal-UB control, and
  Windows coverage with its existing per-area thresholds.

Commit builds use a snapshot of the index, so partially staged changes are
checked exactly as prepared. The working tree and index are never stashed,
reset or rewritten. A concurrent index/HEAD change rejects the result. Push
checks use the commits supplied by Git, including non-HEAD branches and tags;
duplicate tips are checked once and ref deletions need no build. Multiple
distinct tips are checked sequentially, with selected checks parallel per tip.
Any failed or unavailable selected check blocks the operation; checks are not skipped
based on changed paths. Audit and secret scanning also retain their original
repository context, including local identity guards and secret history.

The persistent snapshot and isolated build directories live in
`build/git-hooks/`. A lock prevents overlapping hook invocations. Source files
are refreshed only when their bytes change, preserving incremental builds.
Each task uses two build workers; analyzer/runtime tools retain their own worker
limits. CTest logs and JUnit timings are in `build/git-hooks/logs/`. First runs
build the dedicated targets and take longer than warm runs. Manual commands:

```sh
cmake -DPHASE=commit -P cmake/scripts/hooks/GitHooks.cmake
cmake -DPHASE=push -DREVISION=HEAD -P cmake/scripts/hooks/GitHooks.cmake
```

Existing project builds also expose `pre-commit`, `pre-push` (HEAD), and
`install-git-hooks` targets, for example `cmake --build --preset native --target pre-commit`.

CI keeps its full checks and the default **120 seconds per fuzzer**. The local
30-second override is confined to the hook build. To remove the local hook
configuration, run `git config --local --unset core.hooksPath`.

## CI execution

`Build and test` runs on every branch push, pull request, tag and manual dispatch.
The required aggregate check is **CI result**. Branch protection is configured
separately; do not require individual conditionally skipped jobs.

## Selection and concurrency

- `main` is checked after push. An active run finishes; only the newest pending
  run is retained. This lets a cold Qt build finish and populate its cache.
- Other branches run on push unless an open PR covers that exact head commit.
  Delegation requires confirmed mergeability; conflicting or unknown PR states
  retain the branch checks. The PR checks GitHub's merge commit. Obsolete
  branch/PR runs are cancelled.
- README and ordinary Markdown/image documentation changes run the audit and CI
  policy tests. License documents, runtime terms and relinking instructions are
  build inputs and require the full pipeline.
- Push classification compares with the last successful **CI result** on that
  branch, not merely the preceding push. Missing history, a force-push or API
  failure selects full checks. Tags and manual dispatch always run full checks.
- Only a pushed, valid `v` release tag publishes. Manual dispatch never publishes;
  rerunning the original tag workflow retains its release behavior.

After the audit (including actionlint/ShellCheck and pinned clang-format for the
Windows application sources), static analysis, portable tests,
sanitizers, Linux coverage, Mull, Valgrind, fuzzing,
firmware and Windows SDK preparation run independently. Windows Release and Windows
coverage depend on the SDK producer, not on the Linux analyzers. All mandatory
results join at **CI result**; unexpected skips, failures or cancellations fail it.
Codecov service availability does not replace the local coverage requirement.
Coverage also enforces per-area minimums: core 100% on both hosts, host-tested
firmware 100% on Linux and 98% on Windows, and Windows applications 90%. Rounded
percentages do not determine acceptance. Threshold failures preserve reports and
fail the mandatory coverage job; Codecov statuses remain informational.

The `static-analysis` job runs clang-tidy 18 and Cppcheck 2.22.0 sequentially,
each with at most four workers. MSVC `/analyze` runs inside the existing Windows
Release compilation; SDK preparation and Windows coverage do not enable it.
The audit runs actionlint 1.7.12 with an explicit ShellCheck 0.11.0 executable,
including on documentation-only changes. ShellCheck reports errors and warnings;
PowerShell is outside this check. Analyzer versions, completed units, durations
and up to ten unique findings appear in summaries. MSVC compilation and analysis
share a build timer; report validation has a separate timer. Raw logs, XML, SARIF
and structured reports are retained for 14 days, also on failure. Missing,
truncated or incomplete reports fail the checks. Code scanning permissions are
not requested. See [local analysis commands](building.md#static-analysis).

Every executed job publishes a result in the run's **Summary** tab, including
failures and cancellations when the runner can still execute its final step.
Test summaries show CTest check counts and failed names, Python test results,
coverage, mutation scores and reviewed survivors, Valgrind defects, or completed
fuzzer input counts as applicable. CTest counts are registered checks, which may
wrap a nested suite. Missing or partial reports are explicitly marked unavailable,
never interpreted as zero failures. Uploaded reports are linked directly;
environment details and step statuses are collapsed. **CI result** lists every
required job, including jobs skipped by policy or blocked by a failed dependency.
Jobs skipped before starting cannot publish their own summary.
CTest summaries also list up to three slow checks (at least five seconds).
SDK preparation reports validation and cache restores; firmware reports each
board's version and application size; release publication lists prepared assets.

Firmware is built only on Linux, with both boards sharing one installed Arduino
SDK and compiling in separate directories, two compiler processes per board.
Arduino CLI's explicit `--build-path` keeps the complete compilation in that
board's directory instead of using the global core cache.
Windows Release runs desktop tests and integration/build contracts. On release
tags it also packages applications and independently rebuilds the corresponding
source archive; its release target supplies the desktop unit-test run instead of
running that same suite twice. Publication combines these Windows packages with
the Linux firmware from the same workflow run, checking tag, commit, host and
checksums. Nothing in CI flashes a device or opens a station connection.

Cold Qt builds use four compiler processes; Qt components remain sequential.
Ordinary Linux test builds and nested Windows product builds also use four
compiler processes. The outer Windows build keeps two slots because its product
builds have their own parallelism. Firmware keeps two boards with two compiler
processes each, and Mull retains its existing limits for nested workers. Test
execution limits are independent of compilation limits. The Qt job count is a
CI invocation option, so changing it does not invalidate a compatible SDK cache.

The Windows identity regression keeps independent root and standalone builds
through clean, dirty and reverted states. Its standalone fixture builds only the
two product executables it exercises; the ordinary Windows job still builds and
runs the full desktop test suite. Monitor and Widget packaging smoke checks run
in parallel in separate directories. Failed standalone checks report the exact
language, exit status and diagnostic JSON; reports are retained with the Windows
test artifact.

## C/C++ dependency graph

The separate `C/C++ dependency graph` workflow validates snapshots on pull requests
and pushes to `main` that change the dependency catalog, exporter, generator,
tests or workflow. It can also be dispatched manually. Only a push or manual
dispatch on the repository's default branch submits to the Dependency Submission
API; PRs and manual runs on other branches only validate. The submission job
alone has `contents: write`, using the run's `GITHUB_TOKEN` without extra secrets.
API errors or a non-success response fail the job. This workflow has its own
status and does not change the required **CI result** build gate.

`ExportDependencies.cmake` evaluates `Dependencies.cmake` with CMake, including
variable expansion; `dependency_snapshot.py` converts those values to a snapshot
associated with the run's exact SHA and ref. Stable detector/correlator values
replace the previous inventory instead of creating independent graphs on reruns.
The JSON snapshot and a Markdown inventory with clickable repository/package,
source and notice links are retained for 14 days as `cpp-dependency-snapshot`.
The same inventory appears in the workflow job summary.

The inventory covers Qt Base/Connectivity/Tools, Windows SDK/WDK, all firmware
source archives, and binary library identities from the pinned license-source
URLs. SDKs, Qt Tools and the ESP32 library builder have development scope; linked
libraries have runtime scope. Firmware children describe containment in the
Arduino SDK, rather than a reconstructed upstream dependency tree. GitHub source
packages use `pkg:github` with the actual source tag/revision. WDK retains the
`pkg:nuget/Microsoft.Windows.WDK.x64` identity actually downloaded by the build;
the Windows SDK and Espressif registry component use `pkg:generic`. Versions and revisions are not
duplicated in the generator. Unsupported source URLs, duplicate components or
Arduino/ESP-IDF version drift fail validation instead of silently omitting entries.

This is an inventory of explicitly pinned components, not a complete binary SBOM:
it does not enumerate every bundled Qt/ESP-IDF library or host compiler runtime.
Build executables and analyzers such as CMake, Ninja and Cppcheck are excluded.
Binary license-source revisions identify the catalog's attribution evidence.
Graph visibility does not guarantee Dependabot alerts or automatic CMake updates;
those depend on ecosystem and advisory support.

All supported snapshot fields are populated where evidence is available:
commit/ref, detector, run/attempt and run URL, manifest location, package identity,
relationship, scope and child dependencies. Snapshot/manifest metadata records
the repository, workflow, platforms, inventory scope and dependency-file checksum.
Package metadata adds the upstream repository or registry/documentation URL,
pinned source location/ref, source or license checksum, evidence kind, component
kind, target platform and commit-specific notice URL. Each metadata object is
validated against the API's limit of eight scalar fields. Windows SDK has no
fabricated source checksum; binary library license hashes remain explicitly
identified as license evidence. The API has no writable package repository or
license field: custom metadata does not set GraphQL's `repository` mapping or
populate native license columns. GitHub determines those mappings and displays
the `pkg:github`/`pkg:generic` ecosystems under `other`.

Local validation (no API request):

```sh
python3 -I -S -B -m unittest discover -s tests/maintenance -p test_dependency_snapshot.py -v
```

To generate the JSON locally, supply `GITHUB_SHA` (full 40-character commit),
`GITHUB_REF`, `GITHUB_REPOSITORY`, and `GITHUB_RUN_ID`, then run
`python3 -I -S -B tools/ci/dependency_snapshot.py --output build/dependency-graph/snapshot.json`.
CMake 3.25 or later must be on PATH (or supplied through `--cmake`; tests also
accept `CMAKE_COMMAND`). Generation does not need a token or download SDKs.

## Images and cache ownership

Only `static-analysis` writes the separate Cppcheck tool cache. Its exact key
includes Linux/architecture, compiler version, CMake, pinned source hash and build
recipe. The source archive is checksum-verified; restored/built executables must
pass version and memory-defect controls before saving. Concurrent workflow runs
use the normal first-successful-save policy. Analysis results and Cppcheck work
data are never cached. Existing Qt/Arduino/WDK cache keys are unchanged.

Linux uses `ubuntu-24.04` with GCC 13 (LLVM 18 for Mull/fuzz); Windows uses
`windows-2025-vs2026`, x64 MSVC, static CRT
and WinRT. `cmake/Dependencies.cmake` supplies the pinned CMake, Ninja, toolset and
SDK versions. Job summaries record the actual image and toolchain versions.
Hosted image labels are maintained images, not immutable VM snapshots.

The Windows SDK job is the only writer of the Qt binary, Qt source archive and WDK
caches within a run. Firmware is the only writer of Arduino and firmware legal
source caches. The Qt key uses the computed Qt component fingerprint rather
than the entire shared fingerprint helper. Firmware dependency versions and
firmware-only helper recipes, as well as analyzer versions, do not invalidate
the Qt SDK. Qt cache schema v4 records SHA256 hashes of `cl.exe` and
`link.exe`, MSVC toolset, Windows SDK/UCRT, CMake/Ninja, absolute toolchain and
installation paths, and static CRT/architecture/build settings. Pinned Qt source
checksums and build recipes remain part of the key. It does not scan the entire
toolchain tree: replacing an SDK header/library without changing its version is
outside this identity policy. The cache is intended for managed GitHub-hosted
toolchains. Restore still checks Qt binary/source hashes, static linkage, WinRT,
compile/link compatibility and offline execution. Key computation time appears
in logs and the SDK job summary. The v4 transition creates a new Qt cache once;
source and WDK archive caches keep their existing keys.

Arduino cache schema v3 stores only installed Linux package data and Arduino CLI.
Downloaded installation archives in `arduino/Linux/downloads` are excluded: a
matching installed SDK can build both boards without them. The v3 transition
populates a new cache once; old entries can expire normally.

Consumers download this run's complete Windows SDK artifact into `build/` and
validate it again. A runner toolchain mismatch rebuilds Qt locally without writing
the producer's caches. Objects, CMake caches and ExternalProject stamps are never
transported. Cross-run caches use exact keys, with the normal GitHub branch/PR
visibility rules. A cache hit inherited from `main` does not create another copy.

Caches are saved explicitly only after validation; failed or cancelled preparation
cannot save a partial SDK. Separate runs may race to save the same complete key:
the first save wins, and each run continues using its own verified files. Caches
are optional, not an inter-job communication channel. Corrupt Qt caches are
quarantined and rebuilt. Existing immutable keys cannot be repaired in place;
bump the relevant cache schema or remove that exact corrupt entry after diagnosis.
The previous mixed `downloads` keys are no longer read and can expire normally.
SDK transport artifacts are retained for one day; reports and release transport
artifacts for 14 days. An expired SDK
artifact requires rerunning its producer as well as failed consumers.

`cmake -P cmake/scripts/sdk/PrepareWindowsSdk.cmake` restores and validates the SDK, or builds
and seals it. Cold CI builds stream Qt's inner Ninja compilation progress directly
(`ALLPOWERS_QT_LOG_BUILD=OFF`); download, configure and install logs remain in
ExternalProject directories. Local builds retain captured compilation logs by default.
SDK diagnostics are uploaded separately. Qt cache schema v4 hashes the actual
compiler/linker binaries and the CMake cache recipe; the migration fills a new
cache once. No PowerShell progress observer or cache wrapper is required.

Release publication uses the portable `tools/release/publish_release.py` tool after CMake
has prepared all assets. Its offline fake-API regressions run on Windows and Linux,
including interrupted uploads, immutable published releases and tag/asset mismatches.
The only maintained `.ps1` is `tools/windows/CheckExeVersion.ps1`, a small Windows
VERSIONINFO reader used when checking Windows binaries.

## Local entry points

```sh
cmake --preset native
cmake --build --preset native
ctest --preset native

cmake --preset sanitizers-linux
cmake --build --preset sanitizers-linux
ctest --preset sanitizers-linux

cmake --preset firmware-linux
cmake --build --preset firmware-linux --parallel 2
```

The `firmware` aggregate and individual board targets depend on `arduino-setup`;
an installed matching SDK is reused without updating the network index. Runtime
tests use the `runtime` label, build-graph checks use `build-contract`, and coverage
selection retains its existing `coverage` label. The `native`, `windows`,
`products` and `release` entry points remain available.

For local WSL builds, put the binary directory and `ALLPOWERS_DEPS_DIR` on the
Linux filesystem. Repeated compiler header lookups through a Windows-mounted
SDK can dominate build time. Hosted Linux jobs already use a native filesystem.
