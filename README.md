<p align="center">
  <strong>English</strong> &nbsp; · &nbsp; <a href="README.ru.md">Русский</a> &nbsp; · &nbsp; <a href="README.zh-CN.md">简体中文</a> &nbsp; · &nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>

<h1 align="center">ALLPOWERS</h1>

<p align="center">Power-station telemetry on your desktop. USB UPS reporting through a supported board.</p>

<p align="center">
  <strong><a href="https://github.com/borro/allpowers-bridge/releases">Download</a> &nbsp; · &nbsp; <a href="docs/getting-started.md">Getting started</a> &nbsp; · &nbsp; <a href="docs/README.md">Documentation</a></strong>
</p>

<p align="center"><sub>Windows Monitor and Widget · Waveshare and Pocket · One portable C++23 core</sub></p>

<p align="center">
  <a href="docs/images/windows-monitor-en.png"><img src="docs/images/windows-monitor-en.png" alt="Monitor · live telemetry" width="820"></a>
</p>
<p align="center">
  <a href="docs/images/windows-widget-en.png"><img src="docs/images/windows-widget-en.png" alt="Desktop Widget" width="260"></a>
  &nbsp;&nbsp;
  <a href="docs/images/waveshare-display.png"><img src="docs/images/waveshare-display.png" alt="Waveshare · 1.47″" width="116"></a>
  &nbsp;&nbsp;
  <a href="docs/images/pocket-display.png"><img src="docs/images/pocket-display.png" alt="Pocket · 0.96″" width="160"></a>
</p>

## Choose your setup

A power cable can keep a computer running, but it does not tell the computer how
much energy is left. ALLPOWERS reads the station over Bluetooth Low Energy and
makes its readings available on the desktop or through a USB UPS board.

| Setup | What you get |
| --- | --- |
| **Windows Monitor / Widget** | Charge, input/output power, remaining runtime, graphs and CSV export. Connect directly over PC Bluetooth or read an existing board over USB. |
| **USB UPS board** | BLE Central/GATT → standard USB HID UPS, display and history. The board works independently of the desktop viewer; Windows apps can read its detailed telemetry over USB. |

The station determines electrical backup and transfer behavior. OS power settings
determine battery actions; successful telemetry or HID reporting alone does not
validate automatic shutdown.

## Get started

1. Download **AllpowersMonitor.exe** or **AllpowersWidget.exe** from
   [Releases](https://github.com/borro/allpowers-bridge/releases). Each is a
   standalone Windows 10/11 x64 application; no Qt/Python installation is needed.
2. Select **Bluetooth** for a direct PC connection, or **USB · Waveshare / Pocket**
   for a board already connected to the station. Discover and select your device.
3. Check that readings are fresh. Missing or stale readings do not mean an empty battery.

Some stations accept only one BLE client. Disconnect the other viewer or board
before using PC Bluetooth, or use USB to keep the board's connection.
Monitor and Widget take turns owning a direct connection. Standalone viewing
does not require a service or UPS driver.

For a new board, follow [Updating and recovery](docs/recovery.md) before writing
firmware. Boards remember their station; BOOT changes pages and double BOOT opens
the picker. Try either Windows app with `--demo` to preview without hardware.

## Compatibility

| Device / path | Current evidence |
| --- | --- |
| **R2500 V2 + Waveshare ESP32-S3-LCD-1.47** | BLE/GATT and USB HID UPS on Windows verified on 2026-10-25; see the [validation record and limits](docs/validation.md). |
| **Pocket-Dongle-S3 0.96 N16R8** | Compiled and protocol-tested; hardware operation in this scenario is not verified. |
| **Other catalog models** | 17 recognized names across three protocol families; recognition is not proof of compatibility. Only known fields are exposed. |
| **Board-free Windows UPS service/driver** | Experimental for every model. Native service BLE, driver behavior and Windows power actions are not validated. |

[Model capabilities](docs/BLUETOOTH.md#compatibility-and-evidence) ·
[Hardware evidence](docs/validation.md) · [Experimental Windows UPS](docs/windows-ups.md)

## Documentation and development

Monitor and Widget support Russian, English, Simplified Chinese and Traditional
Chinese. Detailed guides are maintained in English.

- [Getting started](docs/getting-started.md) — downloads, first connection and board setup.
- [Windows applications](docs/windows-apps.md) — languages, graphs, Widget, storage and diagnostics.
- [Troubleshooting](docs/troubleshooting.md) — connection issues, stale readings and history gaps.
- [Documentation index](docs/README.md) — hardware, USB protocol, recovery and reference material.
- [Build from source](docs/building.md) · [Architecture](docs/architecture.md) · [Contributing](CONTRIBUTING.md).

Product code is C++23: a portable core, shared firmware and native Qt Windows
applications. Builds use CMake Presets/Ninja, CTest and CPack; Linux supports the
core and firmware build, with no Linux desktop app yet. Build and test commands
never flash boards or connect to stations.

## License

Original project code is [MIT](LICENSE); the adapted Windows driver is MS-PL.
Dependencies retain their [own licenses](THIRD_PARTY_NOTICES.md). See
[Qt relinking](docs/relinking.md) and [firmware source kits](docs/firmware-licenses.md)
when redistributing binaries.
