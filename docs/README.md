# Documentation

[Project overview](../README.md) · [Downloads](https://github.com/borro/allpowers-bridge/releases) · [Wiki navigator](https://github.com/borro/allpowers-bridge/wiki)

ALLPOWERS brings power-station telemetry to a Windows application or a USB UPS
board. Start with the route that matches what you want to do. Detailed
documentation is maintained in English; the project overview is also available
in [Русский](../README.ru.md), [简体中文](../README.zh-CN.md) and
[繁體中文](../README.zh-TW.md).

## Start using ALLPOWERS

| I want to… | Read |
| --- | --- |
| Choose a download and connect for the first time | [Getting started](getting-started.md) |
| Use Monitor, Widget, graphs or CSV export | [Windows applications](windows-apps.md) |
| Read a connected board through USB | [USB telemetry and history](usb-monitor.md) |
| Check whether my station is supported | [Bluetooth and model compatibility](BLUETOOTH.md) |
| Fix a connection, missing reading or widget problem | [Troubleshooting](troubleshooting.md) |

## Use a USB UPS board

| Topic | Read |
| --- | --- |
| Waveshare and Pocket hardware, displays and controls | [Board profiles](boards.md) |
| Prepare a first installation, update or recovery | [Updating and recovery](recovery.md) |
| Understand what has actually been tested | [Validation evidence](validation.md) |
| Evaluate the separate, experimental Windows service/driver | [Windows UPS integration](windows-ups.md) |

Firmware installation is an explicit maintenance operation. Read the recovery
requirements before writing a board. A working telemetry display or USB HID
report does not by itself validate the computer's shutdown policy.

## Build and contribute

| Topic | Read |
| --- | --- |
| Toolchains, CMake presets, tests, packaging and releases | [Building](building.md) |
| Core, firmware and Windows component boundaries | [Architecture](architecture.md) |
| Desktop development, translations and offline checks | [Windows development](windows-development.md) |
| Review and contribution rules | [Contributing](../CONTRIBUTING.md) |
| Changes in a particular release | [Release notes](releases/) |
| Maintain this documentation and publish the Wiki navigator | [Documentation maintenance](documentation.md) |

## Licensing and redistribution

- [Project license](../LICENSE) and [third-party notices](../THIRD_PARTY_NOTICES.md)
- [Static Qt rebuild and relinking](relinking.md)
- [Firmware licenses and corresponding source](firmware-licenses.md)
- [Microsoft runtime terms](windows-runtime-terms.md)
