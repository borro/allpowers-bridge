# Validation

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
| GitHub workflow, Actionlint 1.7.12 | Passed; remote execution pending publication |

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
from verified cached downloads. Windows and Ubuntu CI jobs are configured, but
have not run remotely because the repository has not been pushed.

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
