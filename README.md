# ALLPOWERS

ALLPOWERS BLE telemetry: one portable C++23 core, two USB UPS boards and a
native Windows monitor. [Русская версия](README.ru.md).

| Product | Connection | Features |
| --- | --- | --- |
| Waveshare ESP32-S3-LCD-1.47 | BLE Central → USB Device | HID UPS, display, history, diagnostics, RGB |
| Pocket-Dongle-S3 0.96 N16R8 | BLE Central → USB Device | HID UPS, compact display, history, diagnostics |
| Windows Monitor | Computer Bluetooth / Qt WinRT or USB CDC board | Telemetry, graphs, diagnostics; optional experimental Windows UPS integration |
| Windows Widget | Computer Bluetooth or USB CDC board | Standalone desktop card with telemetry and an hourly power graph |

The core recognizes 17 model names and three protocol families. USB UPS reporting
is implemented for R2500 V2; other profiles expose only their known fields.
Some stations accept one BLE client: disconnect a board before connecting the PC.
Alternatively select **USB · Waveshare / Pocket** to read the board's connection.
The app remembers the transport, USB serial and Bluetooth station separately.
USB disconnect closes only the PC port. Live readings share charts, local history
and CSV; board diagnostics appear in the table. No flashing or extra driver is
required. The board archive automatically fills gaps in the main USB history
chart; its records remain labelled separately because they lack station identity.
See [USB mode](docs/usb-monitor.md).

**Interface examples**

<table>
  <tr>
    <th width="33%">Windows Widget</th>
    <th width="34%">Waveshare · 1.47″</th>
    <th width="33%">Pocket · 0.96″</th>
  </tr>
  <tr>
    <td align="center" valign="middle"><a href="docs/images/windows-widget.png"><img src="docs/images/windows-widget.png" alt="Desktop widget with telemetry and one-hour power history" width="260"></a></td>
    <td align="center" valign="middle"><a href="docs/images/waveshare-display.png"><img src="docs/images/waveshare-display.png" alt="Waveshare display — telemetry overview" width="172"></a></td>
    <td align="center" valign="middle"><a href="docs/images/pocket-display.png"><img src="docs/images/pocket-display.png" alt="Pocket display — telemetry overview" width="240"></a></td>
  </tr>
</table>

<table>
  <tr>
    <th width="50%">Monitor · Overview</th>
    <th width="50%">Monitor · History</th>
  </tr>
  <tr>
    <td align="center"><a href="docs/images/windows-monitor.png"><img src="docs/images/windows-monitor.png" alt="Windows Monitor — telemetry overview" width="560"></a></td>
    <td align="center"><a href="docs/images/windows-history.png"><img src="docs/images/windows-history.png" alt="Windows Monitor — telemetry history" width="560"></a></td>
  </tr>
</table>

The Windows package includes **AllpowersMonitor.exe** and **AllpowersWidget.exe**.
Each application runs from its own EXE: Qt, SQLite, platform plugins, the protocol core
and the C++ runtime are statically linked. No Qt/Python installation is needed.
Graphs support wheel zoom, panning, crosshair values, series visibility,
linear/logarithmic time, source-change markers and CSV export. Disk work runs
outside the UI thread. History/settings stay in `%LOCALAPPDATA%/AllpowersMonitor`.
The widget runs without the monitor and keeps separate settings/history in
`%LOCALAPPDATA%/AllpowersWidget`. On first launch, select Bluetooth or USB,
discover a device and save. The rectangular 300 × 210 card uses the device name
as its heading and displays charge, input/output, remaining time in
days/hours/minutes and two power traces for the last hour, with gaps for missing
data. Drag anywhere on the card except its menu button to move it; right-click or use the tray icon for settings,
connect/disconnect, position locking and exit. Optional login startup is off by default.
Normal windows cover the card; Win+D leaves it on the desktop. If Explorer
attachment is unavailable, no floating window appears: the tray reports the
reason and attachment is retried automatically.
The monitor and widget take turns owning a direct connection: disconnect in one
before connecting in the other. Ownership protection requires the new versions
of both applications; disconnect an already running older monitor manually.
With the UPS service installed, the widget reads the service's Bluetooth data
and never stops it on exit. The widget owns its USB port directly.
On USB connection/reconnection it automatically reads the board archive and merges
it with current samples into one line per metric, preserving genuine data gaps.
Its logarithmic time axis gives recent seconds more space within the one-hour
history; a marker identifies the latest fresh sample.
Run `AllpowersWidget.exe --demo` to preview without hardware; demo uses temporary
storage and does not change user settings or login startup.
Diagnostics includes a live event log that follows new entries. Scrolling up
pauses following; “К новым записям” resumes it. The view retains 2000 lines;
the disk log rotates independently.
Current diagnostic state is a grouped parameter/value table; full JSON can still
be copied for troubleshooting.
Experimental driver management is inside Diagnostics → **ИБП Windows…**.
On normal startup, the monitor automatically searches for and connects to the
last saved station, using its Bluetooth address and saved protocol profile.
If it is unavailable, discovery is retried; Disconnect cancels waiting/retries.
Demo and automated tests do not use real Bluetooth connections.
F11 toggles fullscreen and restores the previous normal/maximized window mode.
Overview cards and text scale to the available window space, including fullscreen.

An optional LocalSystem service owns BLE and shared history and feeds a KMDF
Battery Class miniclass. Multiple monitors then share its connection. The EXE
embeds the driver package, but the default payload is unsigned and installation
is restricted to a test VM with a trusted signed payload. **Native driver/service
BLE and Windows power actions are not yet validated; VM tests are deferred.**
Standalone monitoring remains available without integration. See
[Windows UPS implementation and test status](docs/windows-ups.md).

## Build

Use Git, CMake 4.4+ and Ninja (pinned CMake 4.4.4 / Ninja 1.13.2). Windows also
needs MSVC 2026 C++ Build Tools and Windows 11 SDK 10.0.26100.0. Run Windows commands in an
**x64 Developer PowerShell**; Linux needs a C++23 compiler. Python and uv are not
used by the project build, tests or packaging.

```sh
# Windows: core, native tests, monitor and widget
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
cmake --build --preset release

# Linux: portable core/tests (no Linux desktop app yet)
cmake --preset native
cmake --build --preset native
ctest --preset native
```

The first Windows build downloads checksum-pinned Qt 6.12.0 source and builds the
static MSVC/WinRT SDK with ExternalProject. Later builds reuse it. Existing static
Qt can be selected with `-DALLPOWERS_QT_ROOT=/path/to/qt`.
GitHub Actions also caches the installed static SDK with an exact compatibility
key covering Qt/build inputs, MSVC, Windows SDK, architecture, tools and install
path. Restored SDKs must pass a compile/link and offline runtime check; a missing
or rejected cache falls back to building Qt from source.
The Windows target also builds/embeds the driver using checksum-pinned WDK
10.0.26100.6584 (KMDF 1.31), with INF/catalog/kernel-PE checks. `ups-driver` builds
only that package. Building or testing does not install it.

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
`dist/windows/AllpowersMonitor/` for `AllpowersMonitor.exe` and `AllpowersWidget.exe`.
The `release` target uses CPack to create runtime and corresponding-source ZIPs
with SHA256 files. The source ZIP includes complete Qt archives and relinking
instructions; keep it with binary distributions.
Each firmware package also contains its LGPL corresponding sources, license
notices and a tested object kit for relinking. See [firmware licenses](docs/firmware-licenses.md).
Windows publishers need a valid Visual Studio license; qualifying personal and
open-source development can use Community. See [runtime terms](docs/windows-runtime-terms.md).

## GitHub builds and publication checks

GitHub Actions runs on pushes to `main`, tags, pull requests and manual requests.
Feature branches use PR checks instead of a duplicate push build; before opening
a PR, a build can be started manually. CI installs checksum-verified Ninja 1.13.2
on both platforms. A publication gate checks the index, working tree and reachable
history, including a pinned
Gitleaks secret scan. Windows Server 2025 builds the static monitor/widget and
driver; Ubuntu 24.04 builds the portable core. Both jobs compile both board
profiles and run CTest. Windows also rebuilds/tests the source kit and creates
CPack ZIPs with checksums. Successful jobs retain build artifacts for 14 days.
Tagged Windows builds additionally rebuild and run both applications with a
modified Qt from the published source kit (`qt-relink-check`).
Pushing a version tag such as `v1.2.3` or `v1.2.3-alpha.1` additionally publishes
a [GitHub Release](https://github.com/borro/allpowers-bridge/releases) after both
platforms pass. It includes standalone Monitor/Widget EXEs, the Windows ZIP, both
board firmware ZIPs, complete application/Qt sources and SHA256 checksums.
The application and Windows EXE properties use the tag version without `v`.
Prerelease tags produce GitHub prereleases. Ordinary branch/PR builds produce
artifacts. Builds never install or flash anything; no personal tokens are required.
See [release safeguards and reruns](docs/building.md#tagged-releases).

Before committing, run `audit-worktree`; after reviewing/staging files, run
`audit`, `audit-history` and `audit-secrets` in the configured build directory.
Source packages use a reviewed file inventory, never a directory-wide copy.
Private configuration, recovery images and local build caches stay outside Git.
See [building and publication](docs/building.md) for the commands.

## Versions and layout

Product versions come from Git tags such as `v0.4.0`, using `git describe`.
Tagged CI selects the triggering tag exactly and verifies its commit.
Intermediate builds include commit/dirty information. The source kit carries a
generated `VERSION` file so an extracted archive retains its version without Git.
There is no hand-maintained product version in CMake or a package manifest.

`core/` owns protocol/model state and C ABI v1; `firmware/` shares logic between
board profiles; `apps/windows/` is the native Qt monitor; `tests/` contains C++
tests and sanitized fixtures; `cmake/` owns build/dependency/package rules.
`drivers/windows/` owns the optional battery miniclass. `tools/` contains explicit
board maintenance only (updating, identity and image-integrity checks). Real identities,
ports and rollback paths are ignored local settings, never public defaults.

Boards remember the selected station in NVS. Without one, the device picker opens.
BOOT changes pages; double BOOT opens the picker. RESET remains a hardware reset.
Read [recovery](docs/recovery.md) before writing any firmware. C ABI, USB descriptors,
flash partitions and the existing maintenance command remain compatible.

[Build details](docs/building.md) · [Architecture](docs/architecture.md) ·
[Boards](docs/boards.md) · [Validation](docs/validation.md) · [Relinking](docs/relinking.md).

Original project code is MIT; the adapted Windows driver is MS-PL.
[Third-party notices](THIRD_PARTY_NOTICES.md) retain their
licenses. Factory images, raw captures and user data are kept outside Git.
