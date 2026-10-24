# Third-party software

Project code is MIT except the adapted Windows driver/INF, which retain MS-PL.
Dependencies keep their licenses; this does not relicense
Qt, Python build tools, compiler runtimes or the Arduino/Espressif SDK.

| Component | Notice |
| --- | --- |
| ALLPOWERS protocol/model definitions, James Johnston | MIT; core/src/AllpowersModels.LICENSE.txt, adapted from https://github.com/madninjaskillz/allpowers-ble |
| Qt 6.8.3 Core, Gui, Widgets, Bluetooth, Sql, Network | Statically linked under LGPLv3; original Qt and bundled-component notices are embedded in the EXE. The release includes corresponding application and Qt sources with relinking instructions. |
| Microsoft Windows-driver-samples / SimBatt | MS-PL; the adapted drivers/windows/driver.cpp and AllpowersUps.inf.in retain this license. Source reference bb68ed5e770bce04dc5395ec9258dafab034184d; drivers/windows/LICENSE.MS-PL.txt. |
| Microsoft WDK/Windows SDK | Build tools/headers/import libraries under Microsoft's SDK license; original SDK sources and tools are not bundled in the application. |
| SQLite bundled with Qt | Public domain plus notices supplied by Qt |
| Microsoft C++ runtime | Statically linked with /MT under Microsoft's runtime redistribution terms |
| Arduino ESP32 Core / ESP-IDF | LGPL-2.1 / Apache-2.0 and component-specific licenses in the pinned Espressif SDK |
| CMake and Ninja | Build/test/package tools only; not embedded in the application |
| Python, hidapi, pyserial, esptool | Optional explicit board maintenance; not required by the project build/test/package commands or the Windows monitor. Upstream Arduino tools may use their own Python internally. |

The release's separate sources ZIP contains the exact unmodified Qt Base and
Qt Connectivity 6.8.3 archives used in the build, complete application source,
license texts and docs/relinking.md. The executable's About dialog displays the
notices; --export-licenses extracts them. No restriction prevents changing Qt,
rebuilding/relinking or running a modified executable.

No manufacturer factory images, third-party keyboard/Wi-Fi firmware, vendor SDK
source copies or Python application runtime are included in the repository/package.
