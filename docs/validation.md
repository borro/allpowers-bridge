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

Firmware targets check USB descriptors, partition offsets, ABI/updater/startup
contracts and source/artifact hashes. The Git-index audit checks private identities,
absolute user paths, generated outputs and staged contents. CI builds Windows and
Ubuntu, fetches tags and packages Windows. CI never opens USB or BLE connections.

The Windows external project runs CTest before installation. Single-EXE tests
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
| New CMake Linux firmware orchestration | Not completed: SDK index HEAD request to espressif.github.io timed out |

Both firmwares were previously compiled successfully on Linux before the CMake
orchestration replacement. That earlier result is not proof of the new Linux
setup/build path; its CI run is still required. Windows and Ubuntu CI jobs are
configured, but have not run remotely because the repository has not been pushed.

A manual test, explicitly authorized after Waveshare was unplugged, connected the
static WinRT app to the real R2500 V2: one connection, 12 valid packets, zero invalid
packets/drops, fresh telemetry and a confirmed subscription. It reported 100%,
0 W input, 208 W output and mains power. The test then disconnected automatically.
No board was flashed and no station control/settings commands were sent.

An offscreen software-rendering benchmark used 6,480 history samples in a
1,120 × 820 window: full plot redraws were about 20–30 ms; cached redraws about
0.03–0.1 ms on the development machine. These are local measurements, not an FPS
guarantee. SQLite, CSV, logs and settings writes run outside the UI thread.

Original research/Git history, factory material and verified application rollback
images remain privately preserved. Existing user history/settings were retained.
The Pocket has no complete factory backup; partial reads are not called backups.
