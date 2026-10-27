# CI builds and dependency caches

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

After the audit, portable tests, sanitizers, Linux coverage, Mull, Valgrind, fuzzing,
firmware and Windows SDK preparation run independently. Windows Release and Windows
coverage depend on the SDK producer, not on the Linux analyzers. All mandatory
results join at **CI result**; unexpected skips, failures or cancellations fail it.
Codecov service availability does not replace the local coverage requirement.

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

## Images and cache ownership

Linux uses `ubuntu-24.04` with GCC 13 (LLVM 18 for Mull/fuzz); Windows uses
`windows-2025-vs2026`, x64 MSVC, static CRT
and WinRT. `cmake/Dependencies.cmake` supplies the pinned CMake, Ninja, toolset and
SDK versions. Job summaries record the actual image and toolchain versions.
Hosted image labels are maintained images, not immutable VM snapshots.

The Windows SDK job is the only writer of the Qt binary, Qt source archive and WDK
caches within a run. Firmware is the only writer of Arduino and firmware legal
source caches. Component fingerprints avoid invalidating Qt when analyzer
versions change. Qt additionally hashes the installed compiler tools, headers and
libraries, records absolute installation paths and validates static linkage,
WinRT features, source identity and offline execution.

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
