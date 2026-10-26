# Third-party software

Project code is MIT except the adapted Windows driver/INF, which retain MS-PL.
Dependencies keep their licenses; this does not relicense
Qt, Python build tools, compiler runtimes or the Arduino/Espressif SDK.

| Component | Notice |
| --- | --- |
| ALLPOWERS protocol/model definitions, James Johnston | MIT; core/src/AllpowersModels.LICENSE.txt, adapted from https://github.com/madninjaskillz/allpowers-ble |
| Qt 6.12.0 Core, Gui, Widgets, Bluetooth, Sql, Network | Statically linked under LGPLv3; original Qt and bundled-component notices are embedded in the EXE. The release includes corresponding application and Qt sources with relinking instructions. |
| Microsoft Windows-driver-samples / SimBatt | MS-PL; the adapted drivers/windows/driver.cpp and AllpowersUps.inf.in retain this license. Source reference bb68ed5e770bce04dc5395ec9258dafab034184d; drivers/windows/LICENSE.MS-PL.txt. |
| Microsoft WDK/Windows SDK | Build tools/headers/import libraries under Microsoft's SDK license; original SDK sources and tools are not bundled in the application. |
| SQLite bundled with Qt | Public domain plus notices supplied by Qt |
| Microsoft C++ runtime | Release runtime statically linked with /MT under Microsoft's redistribution terms; docs/windows-runtime-terms.md. Open-source STL portions: Apache-2.0 WITH LLVM-exception, docs/licenses/MSVC-STL.md. |
| Arduino ESP32 Core / ESP-IDF | LGPL-2.1-or-later (LGPLv3 option used) / Apache-2.0 and component-specific licenses; each firmware package includes licenses.zip, corresponding-source.zip and docs/firmware-licenses.md. |
| FreeType in Qt | FreeType Project License; portions of this software are copyright (c) 2026 The FreeType Project (https://freetype.org). All rights reserved. Original notices are embedded. |
| PCRE2, HarfBuzz, libpng, zlib and other Qt components | Original copyright and license files, including those referenced by Qt attribution metadata, are embedded and exportable. |
| CMake and Ninja | Build/test/package tools only; not embedded in the application |
| UPX 5.2.1 / UCL | Release EXEs contain the unmodified UPX decompression stub. GPL-2.0-or-later with the upstream special exception for compressed executables; docs/licenses/UPX.md is embedded and exportable. UPX is used only when packaging release copies. |
| Python, hidapi, pyserial, esptool | Optional explicit board maintenance; not required by the project build/test/package commands or the Windows monitor. Upstream Arduino tools may use their own Python internally. |

The release's separate sources ZIP contains the exact unmodified Qt Base and
Qt Connectivity 6.12.0 archives used in the build, complete application source,
license texts and docs/relinking.md. The executable's About dialog displays the
notices; --export-licenses extracts them. No restriction prevents changing Qt,
rebuilding/relinking or running a modified executable.

Manufacturer factory/recovery images and third-party keyboard firmware are not
distributed. Vendor source archives and the linked Espressif libraries required
for firmware relinking are included in generated compliance packages, not Git.
No Python application runtime is included in the Windows EXEs.

ALLPOWERS and Qt names/trademarks belong to their respective owners. This is an
independent project and does not imply manufacturer or Qt Company endorsement.
