# ALLPOWERS

BLE telemetry for ALLPOWERS power stations, shared between two USB UPS adapters
and a Windows desktop monitor. [Русская версия](README.ru.md).

| Product | Transport | Output |
| --- | --- | --- |
| Waveshare ESP32-S3-LCD-1.47 | BLE Central → USB Device | HID UPS, display, history, diagnostics, RGB |
| Pocket-Dongle-S3 0.96 N16R8 | BLE Central → USB Device | HID UPS, compact display, history, diagnostics |
| Windows Monitor | Computer's Bluetooth / Qt WinRT | Dashboard, history and diagnostics; no UPS driver |

The portable core recognizes 17 model names in three protocol families. R2500 V2
has the implemented USB UPS mapping; other profiles expose the fields they can
decode, without inventing unsupported values. Some stations accept only one BLE
client: disconnect the adapter before connecting the desktop monitor.

![Windows monitor with synthetic data](docs/images/windows-monitor.png)

The products are C++17. Python is only used for build/test/maintenance tooling;
the Windows package contains no Python runtime. The native interface uses Qt
Widgets, a dedicated SQLite worker and screen-width-bounded graph rendering.
Graphs support wheel zoom, time panning, exact hover values, independent series,
linear/log time and a return-to-live action.

## Layout

```text
core/           Portable C++ protocol/session core and stable C ABI (v1)
firmware/       Common ESP32-S3 firmware; two BoardProfile configurations
apps/windows/   Native C++17/Qt direct-BLE desktop monitor
tests/          Native/Python tests, sanitized packet and USB-contract fixtures
tools/          Build, packaging and explicit device maintenance
config/         Public configuration examples; real identities stay in .local/
docs/           Architecture, board wiring, recovery and validation notes
```

## Build on Windows or Linux

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), Git and
Python 3.11 for build tooling and MSVC 2022 C++ Build Tools + Windows 11 SDK on Windows. Then, from the repository root:

```sh
uv sync --locked
uv run --locked python tools/build.py test
uv run --locked python tools/build.py setup
uv run --locked python tools/build.py all
```

Individual targets: `core`, `waveshare-147`, `pocket-096`, `windows`.
`all` builds the core and both boards; on Windows it also packages the monitor.
`setup` downloads a checksum-verified Arduino CLI 1.5.1 and installs ESP32 Core
3.3.11, and Qt 6.8.3 MSVC on Windows. To reuse an installation, copy `config/arduino.example.json` to
`.local/arduino.json` and supply the executable and Arduino data directory.
Automatic CLI installation supports Windows/Linux x86-64.

CMake Presets + Ninja build the native core, with Zig 0.13 as the portable
compiler. CMake/Ninja/Python dependencies are locked. The build command configures
the compiler; afterwards `uv run cmake --build --preset dev` and
`uv run ctest --preset dev` also work. Every board gets an isolated staging tree.
No build opens a USB device, connects BLE or modifies product sources.

Outputs: `build/` for intermediates; `dist/firmware/<board>/` for checked images
and manifests; `dist/windows/AllpowersMonitor/` and a checksummed ZIP on Windows.
`uv run --locked python tools/build.py clean` removes only build outputs.
Windows EXEs are built on Windows. A Linux desktop app is not implemented.

## Run and maintain

On Windows, run `dist/windows/AllpowersMonitor/AllpowersMonitor.exe`. Connection is explicit.
Settings, history and rotating logs live in `%LOCALAPPDATA%/AllpowersMonitor`.
`ALLPOWERS_DATA_DIR` overrides this for diagnostics/tests.

Firmware remembers the selected station in NVS. Without a saved target it opens
the device picker. BOOT cycles screens; double BOOT opens the picker. In the
picker, single BOOT advances and double/held BOOT selects. RESET remains hardware reset.

Flashing is a separate, explicit operation with local device identity checks.
Read [recovery](docs/recovery.md) before writing. Existing USB descriptors,
partition offsets and `BOOT-R2500!!` maintenance remain unchanged.

## Development

[Build prerequisites](docs/building.md) · [Architecture](docs/architecture.md) · [Boards](docs/boards.md) ·
[Validation](docs/validation.md) · [Contributing](CONTRIBUTING.md).
CI defines core + both firmware builds on Windows and Ubuntu, and packaging on
Windows. Local Windows and Ubuntu (WSL) builds passed; see the scope and hardware
limits in [validation](docs/validation.md).

MIT; see [third-party notices](THIRD_PARTY_NOTICES.md). Factory images, device
identities, private telemetry logs and historical research are not distributed.
