# Validation

Run `uv run --locked python tools/build.py test`: C++ decoder tests plus Python
tests of C ABI, recorded packet fixtures, board identity and recovery contracts.
QtTest additionally checks session generations, native controls,
bounded compatible history, graph interactions, source transitions and rendering.
Tests use recorded packets and an offline UI; they never connect a real station.

`all` builds both firmware variants and, on Windows, the desktop package. Build
manifests contain source hashes; builds check that product sources are unchanged.
Partition offsets, descriptor hash and maintenance command are build gates.

`uv run --locked python tools/audit.py` checks the Git index for outputs/private
directories, machine-specific paths and locally registered device identities.
It does not replace review of newly introduced data before publication.

GitHub Actions defines Windows and Ubuntu native/firmware jobs plus Windows
packaging. A workflow file is not evidence of successful Linux execution. The
local restructuring report records which jobs actually ran. No flash or BLE
test is included in CI. Linux desktop, macOS and OS UPS drivers are out of scope.

## Local release checks — 2026-10-24

| Check | Windows 11 x64 | Ubuntu x64 (WSL) |
| --- | --- | --- |
| Native core and C ABI / recorded-packet tests | Passed | Passed |
| Waveshare 1.47 firmware compilation and USB/partition gates | Passed | Passed |
| Pocket 0.96 firmware compilation and USB/partition gates | Passed | Passed |
| Native Qt controls, history, settings, graph tests and packaging | Passed | Not applicable |

The Qt WinRT backend also discovered nearby BLE advertisements without connecting.
A real station GATT session in the rewritten Qt application remains untested;
the existing station connection was left running. Neither board was flashed.
Compilation does not replace a hardware test of the newly built firmware.

An offscreen software-rendering benchmark used 6,480 history samples in a
1,120 × 820 window: a complete plot redraw took about 22 ms, while a cached
redraw took about 0.03 ms on the development machine. These are local measurements,
not a frame-rate guarantee. SQLite, CSV export, logs and settings writes run
outside the UI thread. Closing the window waits for queued settings writes.

Original research and Git history, factory material and verified application
rollback images were preserved privately before cleanup. Existing desktop history
and settings were migrated to the user-data directory and checked for integrity.
