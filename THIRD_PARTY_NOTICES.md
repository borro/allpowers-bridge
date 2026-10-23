# Third-party software

Project code is MIT. Dependencies keep their licenses; this does not relicense
Qt, Python build tools, compiler runtimes or the Arduino/Espressif SDK.

| Component | Notice |
| --- | --- |
| ALLPOWERS protocol/model definitions, James Johnston | MIT; core/src/AllpowersModels.LICENSE.txt, adapted from https://github.com/madninjaskillz/allpowers-ble |
| Qt 6.8.3 Core, Gui, Widgets, Bluetooth, Sql | Dynamically linked; LGPLv3/GPLv3/commercial options and third-party notices supplied with Qt. License texts ship in licenses/Qt. This project uses the LGPL option for these modules. |
| SQLite bundled with Qt | Public domain plus notices supplied by Qt |
| Microsoft C++ runtime | Redistributed by the official Qt deployment tool under Microsoft's runtime redistribution terms |
| Arduino ESP32 Core / ESP-IDF | LGPL-2.1 / Apache-2.0 and component-specific licenses in the pinned Espressif SDK |
| Python, uv, CMake, Ninja, Zig, aqtinstall, maintenance libraries | Build/test/maintenance tools only; not embedded in the native Windows monitor. See their distributions for notices. |

Qt's unmodified 6.8.3 source is available from
https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz
The application places no restrictions on replacing/relinking its Qt DLLs.

No manufacturer factory images, third-party keyboard/Wi-Fi firmware, vendor SDK
source copies or Python application runtime are included in the repository/package.
