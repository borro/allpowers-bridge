# CMake architecture

The root `CMakeLists.txt` declares the project, initializes its subsystems and
connects product targets. CMake remains the build entry point. Preset names,
public target names and `ALLPOWERS_*` cache options are unchanged.

## Ownership

| Directory | Responsibility |
| --- | --- |
| `cmake/modules/common` | Argument validation, JSON, public source inventory, bounded filesystem operations and verified downloads |
| `cmake/modules/project` | Version resolution, build identity, compiler policy, parallelism and size optimization |
| `cmake/modules/sdk` | Qt, Arduino and build-tool setup, with selective dependency fingerprints |
| `cmake/modules/products` | Firmware board targets, Windows monitor and driver ExternalProjects |
| `cmake/modules/quality` | Exclusive instrumentation modes, target flags and analysis/report targets |
| `cmake/modules/release` | Package registration, staging, licenses and release validation |
| `cmake/modules/hooks` | Git snapshots and concurrent check execution |
| `cmake/scripts` | Executable `cmake -P` entry points, grouped by the same responsibilities; CI adapters live under `scripts/ci` |
| `cmake/templates` | Generated Qt cache probe and CPack scripts |
| `cmake/projects/HookChecks` | Small CTest project scheduling independent hook tasks |
| `tests/cmake`, `tests/native`, `tests/desktop`, `tests/fuzz` | Test registration and implementation |
| `tools/quality`, `tools/ci`, `tools/release` | Python analysis, reporting and publication tools invoked by CMake or CI |
| `tools/windows` | Existing Windows executable version check |

Including a reusable module defines functions and constants only: it must not
discover tools, run processes, fetch dependencies, create targets or set directory
compiler flags. Entry points explicitly call the `ap_*` functions they need.
Functions use named arguments and reject unknown or missing required arguments.
`ROOT` denotes the source root, `BUILD_ROOT` the owning binary directory, `WORK`
a dedicated scratch directory, `DEPS_DIR`/`SOURCE_CACHE` a dependency cache and
`OUTPUT` the result location. Options that describe a configured build remain
CMake cache variables. Modules do not infer output locations from the shell's
working directory.

## Products and identity

Qt, the Windows monitor and the UPS driver retain their ExternalProject graph.
The two child projects can also be configured and built independently. Windows
test registration is separate from product creation and CPack registration.
Arduino configuration, board targets and firmware source/license packages have
separate owners. No build target flashes hardware or installs a driver.

`ap_read_version(ROOT ...)` resolves tags, dirty state, worktrees and archive
`VERSION`. `scripts/project/BuildIdentity.cmake` writes the generated identity;
`ap_add_build_identity(ROOT ... BUILD_ROOT ... RESOURCE_SOURCE ...)` registers
its refresh. Unchanged identity files keep their timestamps. A child respects
the parent's chosen version and commit, while detecting subsequent dirty inputs.

`cmake/Dependencies.cmake` is the only source of pinned versions and checksums.
Consumer fingerprints include the recipes and shared helpers that affect that
consumer, without hashing unrelated dependency declarations or subsystems.
Downloads hold an artifact lock, use a temporary file and publish to the cache
only after hash verification. Each caller explicitly retains its existing
reject-or-repair policy for a damaged cached archive. Qt SDK source identity and
compiled-library rejection checks remain separate from archive validation.

Tool bootstrap is also an explicit library API: `ap_setup_ninja(DESTINATION ...
OUTPUT ...)`, `ap_setup_upx(DEPS_DIR ... WORK ... OUTPUT ...)`,
`ap_setup_wdk(DEPS_DIR ... OUTPUT ...)` and `ap_setup_clang_format(WORK ...
OUTPUT ...)`. The functions return the executable or SDK directory and load
their pinned dependency constants in their own scope. An installation lock
covers archive validation, extraction and any executable version check.
Ninja, UPX and clang-format restore their executable from the verified archive;
WDK reuses a completed installation. The existing archive policies are retained:
UPX and WDK reject corruption; Ninja and clang-format may replace a corrupt
download only with checksum-verified bytes.

The corresponding `scripts/sdk/Setup*.cmake` files are command-line adapters.
Only the Ninja adapter updates `GITHUB_PATH`; calling its library function does
not change CI environment files. Reusable modules must not include executable
scripts, even from inside functions. Packaging and formatting call the tool APIs
directly.

Coverage, Mull and analysis tools follow the same contract through
`ap_setup_coverage(WORK ... OUTPUT ...)`, `ap_setup_mutation(WORK ... OUTPUT ...)`
and `ap_setup_analysis_tools(WORK ... KIND cppcheck|workflow OUTPUT ...)`.
Their installation locks cover environment creation, extraction, compilation
and control checks, in addition to the per-download lock. Coverage and Mull
retain corrupt-archive rejection; analysis tools retain verified replacement.
The workflow result contains the actionlint and ShellCheck executable paths.
The coverage command-line adapter alone exports the Codecov version to CI.
The Cppcheck cache key includes its setup module and all common helpers it uses.

## Quality modes and hooks

`cmake --workflow --preset verify` configures the root verification build and
builds its `verify` target. `modules/quality/Verification.cmake` connects existing
product/check targets and isolated preset configurations; it does not duplicate
the check implementations. Tool preparation precedes child configuration, tests
depend on their binaries, and Windows packaging depends on the complete check
graph. `.githooks/Host.cmake` owns the optional Windows-to-WSL bridge. This workflow
checks working-tree sources; Git hooks retain their separate snapshot semantics.
See [complete local verification](building.md#complete-local-verification) for
prerequisites and unavailable-platform reporting.

`ap_configure_quality(ROOT ... BUILD_ROOT ...)` validates coverage, sanitizers,
mutation, fuzzing and static analysis centrally. These modes use separate build
directories. Instrumentation is enabled on selected targets; ordinary targets
do not inherit sanitizer or coverage flags. Deliberately defective control tests
retain their special instrumentation settings.

Hook tasks configure through the existing presets, overriding only their build
and result directories, local tool locations, parallelism and the 30-second fuzz
budget. They reuse `check` / `check-memcheck`; the unit-only hook tasks keep their
portable test filter. Platform adapters live in `.githooks`; the scheduler and check runners
are CMake. On Linux, Windows checks remain the responsibility of CI.

Examples from the source root:

```sh
cmake --preset native
cmake --build --preset native
ctest --preset native
cmake -P cmake/scripts/hooks/InstallGitHooks.cmake
cmake --build --preset native --target pre-commit
cmake --build --preset native --target pre-push
cmake --build --preset native --target audit-worktree workflow-lint format-check
cmake -DGROUP=qt -P cmake/scripts/ci/DependencyKeys.cmake
```

`pre-commit` checks the index snapshot; `pre-push` checks the selected committed
revision. Their task lists and coverage thresholds are documented in
[local Git hooks](ci.md#local-git-hooks). The old flat script paths are removed;
Normal CI checks use targets, so their script paths remain implementation details.
Standalone `cmake -P` entries remain for bootstrap and operations without a configured
build: reading pinned versions, installing Ninja, preparing SDKs/tools needed by
configuration, computing cache keys, recording the host environment, and assembling
or validating downloaded release assets. `CiTools.cmake` and dependency export run
on the runner's CMake 3.25+; actual builds use the pinned version.

`cppcheck-setup` prepares and validates the analysis tool before CI saves its cache;
`static-analysis` depends on it. `valgrind-policy-check` builds its defective control
before checking Memcheck's failure policy. On MSVC, `static-analysis-report` only
reads reports: it never starts a product build and honors `ANALYSIS_BUILD_STATUS`
from CI. A failed compilation therefore cannot be hidden by a report-only rerun.

## Regression checks

`module-boundaries` checks that every module is inert when included.
`sdk-tools` exercises explicit outputs, caller-scope preservation, CI adapter
behavior, concurrent tool restoration, WDK cache reuse and corruption rejection
using local archives in paths with spaces, without downloading dependencies.
`infrastructure` checks concurrent cache publication, hash rejection, safe
workspace boundaries, JSON escaping and unchanged-file timestamps.
`coverage-policy` checks all incompatible mode pairs and target-local flags.
Other build contracts cover incremental source/header changes, version refresh,
cache invalidation, SDK rejection, paths with spaces, CPack staging, source kits
without Git and publication against an offline API. Run them with
`ctest --test-dir <build> -L build-contract --output-on-failure`.
