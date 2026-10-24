# ALLPOWERS

ALLPOWERS BLE telemetry: one portable C++17 core, two USB UPS boards and a
native Windows monitor. [Русская версия](README.ru.md).

| Product | Connection | Features |
| --- | --- | --- |
| Waveshare ESP32-S3-LCD-1.47 | BLE Central → USB Device | HID UPS, display, history, diagnostics, RGB |
| Pocket-Dongle-S3 0.96 N16R8 | BLE Central → USB Device | HID UPS, compact display, history, diagnostics |
| Windows Monitor | Computer Bluetooth / Qt WinRT | Telemetry, interactive graphs and diagnostics; no OS UPS driver |

The core recognizes 17 model names and three protocol families. USB UPS reporting
is implemented for R2500 V2; other profiles expose only their known fields.
Some stations accept one BLE client: disconnect a board before connecting the PC.

![Windows monitor with synthetic data](docs/images/windows-monitor.png)

The Windows runtime is **one EXE**: Qt, SQLite, platform plugins, the protocol core
and the C++ runtime are statically linked. No Qt/Python installation is needed.
Graphs support wheel zoom, panning, crosshair values, series visibility,
linear/logarithmic time, source-change markers and CSV export. Disk work runs
outside the UI thread. History/settings stay in `%LOCALAPPDATA%/AllpowersMonitor`.

## Build

Use CMake 3.25+ and Ninja (tested with CMake 3.31.6 / Ninja 1.11.1). Windows also
needs MSVC 2022 C++ Build Tools and Windows 11 SDK. Run Windows commands in an
**x64 Developer PowerShell**; Linux needs a C++17 compiler. Python and uv are not
used by the project build, tests or packaging.

```sh
# Windows: core, native tests and the single-EXE monitor
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
cmake --build --preset release

# Linux: portable core/tests (no Linux desktop app yet)
cmake --preset native
cmake --build --preset native
ctest --preset native
```

The first Windows build downloads checksum-pinned Qt 6.8.3 source and builds the
static MSVC/WinRT SDK with ExternalProject. Later builds reuse it. Existing static
Qt can be selected with `-DALLPOWERS_QT_ROOT=/path/to/qt`.

Firmware targets are available in either preset:

```sh
cmake --build build/windows --target arduino-setup
cmake --build build/windows --target waveshare-147 pocket-096
```

Use `build/native` on Linux. Setup pins Arduino CLI 1.5.1 and ESP32 Core 3.3.11.
Use `cmake --build --preset all-windows` or `all-linux` for all supported products.
Each board has independent sketch/build staging; no source is modified. Build,
CTest and packaging never flash devices or connect a station.

Outputs: `build/` for intermediate files, `dist/firmware/<board>/` for images,
`dist/windows/AllpowersMonitor/AllpowersMonitor.exe` for the Windows runtime.
The `release` target uses CPack to create runtime and corresponding-source ZIPs
with SHA256 files. The source ZIP includes complete Qt archives and relinking
instructions; keep it with binary distributions.

## Versions and layout

Product versions come from Git tags such as `v0.4.0`, using `git describe`.
Intermediate builds include commit/dirty information. The source kit carries a
generated `VERSION` file so an extracted archive retains its version without Git.
There is no hand-maintained product version in CMake or a package manifest.

`core/` owns protocol/model state and C ABI v1; `firmware/` shares logic between
board profiles; `apps/windows/` is the native Qt monitor; `tests/` contains C++
tests and sanitized fixtures; `cmake/` owns build/dependency/package rules.
`tools/` contains optional explicit Python USB maintenance only. Real identities,
ports and rollback paths are ignored local settings, never public defaults.

Boards remember the selected station in NVS. Without one, the device picker opens.
BOOT changes pages; double BOOT opens the picker. RESET remains a hardware reset.
Read [recovery](docs/recovery.md) before writing any firmware. C ABI, USB descriptors,
flash partitions and the existing maintenance command remain compatible.

[Build details](docs/building.md) · [Architecture](docs/architecture.md) ·
[Boards](docs/boards.md) · [Validation](docs/validation.md) · [Relinking](docs/relinking.md).

Project code is MIT; [third-party notices](THIRD_PARTY_NOTICES.md) retain their
licenses. Factory images, raw captures and user data are kept outside Git.
