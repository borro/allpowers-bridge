# Validation

## Review-fix evidence — 2026-10-25

The review fixes were validated before committing, in a working tree based on
`79b0173`, identified as `0.5.1-3-g79b0173-dirty`. The offline results below
record that source validation; later tagged builds and deployment are separate checks.
Hardware and hosted CI rows describe earlier evidence, not a rerun of the changes.
Local build/test evidence remains in ignored storage and is not distributed in Git.

| Component | Revision / date | Environment and result | Evidence / remaining limit |
| --- | --- | --- | --- |
| Core, C ABI, UPS mapper, firmware RAM logic, build regressions | Review working tree / 2026-10-25 | Linux GCC 13.3: CTest 12/12; Windows MSVC 19.51: CTest 14/14 passed | Local `/tmp/allpowers-review-integrated-native` and `build/review-integrated-native`; real fixture bytes through C ABI to UPS cache, gaps/known metadata and staging/version/partition cases; no hardware |
| Memory/undefined-behavior checks | Same working tree / 2026-10-25 | Linux ASan + UBSan: models, ABI, UPS state and firmware host 4/4 passed | Local `/tmp/allpowers-review-sanitizers`; default sanitizer options; no hardware |
| Monitor and Widget | Same working tree / 2026-10-25 | Windows MSVC 19.51, static Qt 6.12, /MT, WinRT: Qt CTest 6/6 passed | Local `build/review-windows`; desktop 46 cases; native UI/DPI and isolated EXEs; no BLE/USB |
| Firmware and relink kits | Same working tree / 2026-10-25 | Both board targets compile, partition/USB contracts and object-kit relinking pass on Windows and Linux | Local `build/review-integrated-native` and `/tmp/allpowers-review-integrated-native`; both resulting Windows image packages pass offline maintenance validation; no activation or second-write test of these images |
| Maintenance image guards | Same working tree / 2026-10-25 | Offline Python tests 6/6 passed; existing firmware package accepted | `tests/maintenance/test_image_contract.py`; synthetic hash-consistent invalid images rejected before device access |
| Driver and release package | Same working tree / 2026-10-25 | Windows driver/INF/catalog and CPack runtime/source ZIP passed; extracted source kit rebuilt/tested without .git | Local `build/review-integrated-windows`; extracted core CTest 14/14 and desktop suites 4/4; kernel runtime remains unverified |
| Hosted product checks | `da1415b`, before review fixes / 2026-10-25 | Previously recorded hosted MSVC 19.51, Ubuntu, CTest and CPack success | Toolchain-upgrade journal below; these working-tree changes have not run on hosted CI |
| Waveshare + R2500 V2 | Firmware before review fixes; exact hardware-test revision not recorded / 2026-10-25 | Previously recorded BLE Central/GATT, fresh telemetry and physical HID UPS success | Hardware journal below; no claim of transitions, Pocket hardware or native Windows driver readiness |

Regression tests cover data validity, archive recovery, bounded work, failure
paths and build contracts. Firmware synchronization and direct-verification
ownership were also reviewed in code. The packet decoder/reducer, full-snapshot recovery deadline and
bounded history queue are shared logic. Archive AHP2 adds CRC-covered known/model
metadata without moving partitions or changing slot sizes; old AHP1 remains
readable with conservative output validity. New cursor export is additive;
legacy HISTORY-READ retains its original meaning. Firmware STATUS exposes the
product identity independently of the unchanged USB protocol version.

Driver VM lifecycle/IOCTL, LocalSystem BLE, sleep/resume and real Windows power
actions remain pending. Firmware compilation and offline contracts do not prove
an actual next write. No device was opened or flashed while applying these fixes.

The sections below are a dated journal; superseded observations are historical.

Configure/build with CMake Presets and run ctest --preset windows or native.
C++ tests exercise the actual shared C ABI, model decoding, recorded packets,
freshness, malformed data, timer wrap and bounds. QtTest covers session generations,
controls, bounded compatible history, immediate-close settings persistence,
source transitions, graph interactions and rendering.
Dropdown regression tests cover row geometry, keyboard selection/cancel and list
stability during discovery updates. Live-log tests cover tail following, paused
scrolling/selection and preservation of visible entries as the 2000-line buffer
rotates. These UI tests run offline, including at 150% display scale.
The native Windows popup regression waits beyond the animation interval and
checks the actual two-option history selector at 150%, including reopening and
the last selected item. Component geometry was also checked at 125% and 200%.
This catches Qt's delayed scroll effect restoring a popup height that omits our
layout margins; the offscreen backend alone cannot reproduce that effect.

Firmware targets check USB descriptors, partition offsets, ABI/updater/startup
contracts and source/artifact hashes. Publication audits check private identities,
absolute user paths, unexpected files, staged/working contents and reachable Git
history. Gitleaks checks credentials in history and changes. Regression tests use
temporary repositories to verify rejection of ignored-but-force-staged outputs,
unreviewed files, staged keys and secrets deleted in a later commit. CI builds
Windows and Ubuntu, fetches tags and packages Windows. CI never opens USB or BLE.

CTest and the release target run the Windows external project's tests. Single-EXE tests
inspect PE imports, reject Qt/shared CRT DLLs and run only the EXE in an isolated
folder with a system-only PATH. Embedded LGPL notices and SQLite/UI startup are
also checked. CPack release builds additionally extract their source archive and
rebuild/test the portable core without .git or Python, checking its saved version.

## Tag-driven release publication — 2026-10-25

Windows and Ubuntu checks cover exact lightweight/annotated tags, multiple tags
on one commit, the triggering Git object, invalid versions, dirty/staged/untracked
sources and attempts to publish a source archive as a Git checkout. Windows tests
compile the actual VERSIONINFO template, then reject wrong/missing EXE versions,
stale source/firmware versions and tampered package files. SemVer build metadata
is preserved inside EXEs and encoded safely in uploaded filenames.

The publisher is exercised with a mock CLI only: successful draft publication,
draft resume, interrupted/partial uploads, wrong server hashes, a moved remote
tag, a forbidden lookup, foreign/old drafts and matching/mismatching published
releases. Failure cases never publish or replace a published release.
Local Windows CTest, Linux portable checks, Actionlint and the source-kit rebuild
pass. A live GitHub Release from a new tag has not been created by these tests.

## Toolchain upgrade — 2026-10-25

Qt is pinned to 6.12.0, MSVC CI to toolset 14.51, CMake to 4.4.4 and Ninja to
1.13.2. Product targets use C++23; MSVC uses its dedicated `/std:c++23preview`
mode rather than draft-standard `/std:c++latest`. C ABI v1 is unchanged.

The Windows publication run failed with Qt 6.8.3 and the hosted runner's newer
compiler: Qt's C++17 WinRT probe selected the removed experimental coroutine
header and silently disabled Qt Core's WinRT feature. Bluetooth then failed on
the missing private factory-cache header. The new build explicitly requires
Qt Core C++/WinRT and the Bluetooth WinRT backend and configures Qt with C++23.

- Ubuntu WSL / GCC 13.3: core, both firmware profiles and all six CTest checks pass.
- Windows / locally installed MSVC 19.44: both firmware profiles and all eight
  root CTest checks pass, including the six nested Qt/UI/isolated-EXE suites.
- A clean Qt 6.12.0 static bootstrap passed the required C++/WinRT and Bluetooth
  WinRT probes. CPack and the source-kit rebuild/test without Git metadata pass.
  The hosted MSVC 19.51 build, CTest, CPack/source-kit rebuild and publication
  audits also passed on GitHub Actions (commit `da1415b`).
- USB descriptor, partition and maintenance contracts pass without changing fixtures.

These results do not replace the driver VM/runtime checks below.

## Waveshare hardware check — 2026-10-25

The user authorized flashing Waveshare 1.47 only. Its registered identity,
existing external rollback image and prior physical ROM-entry verification were
checked before writing. An attempted full flash read through the RAM stub failed;
it is not a new full backup. ROM reads of the initial region and NVS succeeded.
The existing verified external factory/rollback copies remain the recovery copies.

Two actual writes of the C++23 firmware were completed using `--rom-only`, with
all four written segments verified. The second write was entered through the
new firmware's HID maintenance command. NVS and history partitions were not
erased; the saved target loaded and the historical record count continued rising.
Real BLE Central/GATT notifications, fresh decoded telemetry, dynamic/static USB
HID UPS reports and Windows power status agreed on full charge and mains power.
Display/graph/history self-tests passed, with no HID-send or history errors.
The user confirmed live screen updates, green RGB and BOOT page switching.

The Qt 6.12 widget was restarted in its saved USB mode and passed a real native
desktop check: board archive points were imported into the combined graph,
history opened without error and the computer's BLE transport stayed unused.
The downloaded CI artifact and its runtime ZIP were checksum-verified. Both
MSVC 19.51-built EXEs passed a further real USB telemetry/archive check; the
updated widget was left running.
Pocket was compiled but not flashed. Mains/battery transitions, physical ROM
entry and factory rollback were not exercised again in this session.

## Local checks — 2026-10-24

| Check | Result |
| --- | --- |
| New CMake native core / C ABI / fixtures on Windows 11, MSVC 19.44 | Passed |
| New CMake native core / C ABI / fixtures on Ubuntu WSL, GCC 13.3 | Passed |
| Waveshare/Pocket firmware compilation and USB/partition checks through CMake on Windows | Passed |
| Static Qt UI/history/graph tests, isolated EXE, CPack/source-kit rebuild | Passed |
| Clean Qt 6.8.3 static MSVC/WinRT bootstrap, then monitor/widget tests | Passed |
| Header-only incremental rebuild and unchanged no-op build, MSVC/Ninja and GCC/Ninja | Passed |
| Separate source/binary directories, Windows `BUILD_TESTING=OFF`, source-kit packaging outside the checkout | Passed |
| Deleted firmware byproduct restored by the next Linux build | Passed |
| New CMake Linux firmware orchestration, both boards | Passed; official SHA256-verified SDK archives were downloaded through Windows because WSL could not reach the ESP32 index |
| Reachable Git history and current public sources, Gitleaks 8.30.1 | No leaks detected |
| GitHub workflow, Actionlint 1.7.12 | Passed; remote execution was pending at this stage (subsequent hosted evidence above) |

## USB monitor — 2026-10-24

USB tests cover both profiles with fake channels/sanitized records: fragmentation,
malformed/oversize lines, known-field/source mapping, stale counters, CRC rejection,
unknown dates, archive deduplication and separate station/archive retention.
UI tests cover live recording, archive selection, USB/Bluetooth choice and driver
management being located only in Diagnostics.

An earlier manual Waveshare USB-only test verified fresh telemetry and archive
import, with invalid/undated records skipped and no USB/CRC errors. Pocket's
hardware path remains unverified beyond protocol tests. Publication preparation
uses only offline tests and compilation, without contacting either board.

## Experimental Windows UPS work — offline boundary

The new UPS mapper and the exact kernel state validator/cache are tested on
synthetic snapshots for 100/50/5/0%, missing required fields, unknown runtime,
15-second expiry, replay rejection and connection-generation/tag changes.
QtTest exercises the service host with an offline BLE transport, real named pipes
and SQLite: multiple viewers, data recording after closing a window, invalid IPC,
and legacy/service history merging. No CTest test connects a station.

The x64 KMDF driver compiles and its INF/catalog pass WDK tools; PE imports and
embedded package hashes are checked. This is build validation, **not kernel runtime
validation**. VM provisioning was deferred by the user. No ALLPOWERS service/driver
has been installed on the host; Secure Boot, BCD and host certificates are unchanged.
SCM lifecycle, installation/update/rollback/removal, Battery Class IOCTL behavior,
Driver Verifier, Windows shutdown/hibernate policy and real BLE from LocalSystem
before login are **unverified** until guest tests. Hyper-V raw Bluetooth passthrough
is not assumed. See [the lab procedure](windows-ups.md).

Both firmware targets now pass the CMake build/contract path on Linux. This does
not verify WSL's failed network/index-update path: the pinned SDK was installed
from verified cached downloads. At this earlier stage Windows and Ubuntu CI
jobs had not run remotely; subsequent hosted results are recorded above.

An earlier authorized manual WinRT test verified a real Central/GATT connection,
subscription and fresh R2500 V2 telemetry. That result is separate from BLE
observation, hardware USB UPS reporting and the unverified kernel-driver path.

An offscreen software-rendering benchmark used 6,480 history samples in a
1,120 × 820 window: full plot redraws were about 20–30 ms; cached redraws about
0.03–0.1 ms on the development machine. These are local measurements, not an FPS
guarantee. SQLite, CSV, logs and settings writes run outside the UI thread.

Original research/Git history, factory material and verified application rollback
images remain privately preserved. Existing user history/settings were retained.
The Pocket has no complete factory backup; partial reads are not called backups.
