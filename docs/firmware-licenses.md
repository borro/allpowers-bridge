# Firmware licenses and corresponding source

[Documentation](README.md)

This firmware statically uses Arduino ESP32 3.3.11 (LGPL-2.1-or-later),
ESP-IDF and third-party components. The application remains MIT; each dependency
retains its own license. Modification for your own use and reverse engineering
for debugging library modifications are permitted. There is no author signing
key or activation requirement for a rebuilt application image.
For Arduino files offered under LGPL-2.1-or-later, this distribution exercises
the LGPLv3 option, compatible with the Apache-2.0 components. The original
Arduino license and headers remain intact; LGPLv3 and GPLv3 texts are also
included in `licenses/`. Files with other licenses keep their original terms.

Public board packages contain installation images, a manifest, license notices,
the `licenses/` directory and `FIRMWARE-SOURCES.md`. `licenses/README.md` maps
the components selected by the linker to full license texts (deduplicated by
SHA256) and retained copyright/permission notices in `licenses/NOTICES.md`.
`components.json` supplies the same component mapping for tooling. No nested
license ZIP or unrelated SDK examples/compiler-language licenses are included.
`FIRMWARE-SOURCES.md` identifies the exact project
commit/tag, source archive, pinned third-party source downloads and their SHA256
checksums, with build and modification instructions. Keep these directions and
notices with the binary distribution, including when mirroring a release.
The large generated source/object kit is not duplicated in public firmware ZIPs.
Existing releases retain their original packages.

GitHub's automatic source archive contains this repository, not the Arduino or
ESP-IDF dependency sources, and lacks Git version/source-inventory metadata.
For a supported rebuild, clone the release tag named in `FIRMWARE-SOURCES.md`;
the build downloads the checksum-pinned SDK and corresponding source archives.
The links identify the versions used for that binary, rather than an upstream
project's current main branch. Redistribution must retain access to those
matching sources and the ability to rebuild with modified LGPL libraries.

The internal build's `corresponding-source.zip` contains the application and build scripts,
the Arduino source release (including its omitted board-manager license), the
generated Arduino release header, and the matching attribution/source archives
for ESP-IDF, NimBLE, TinyUSB, TLSF, Mbed TLS and ESP Diagnostics. `DEPENDENCIES.md` records
their immutable revisions/versions, download locations and SHA256 checksums.
ESP-IDF's archive does not contain its Git submodules; the separately included
archives cover the open-source submodules actually linked here. It is not
advertised as a complete generic ESP-IDF SDK distribution.

The `relink/` directory contains the application's and Arduino libraries'
objects, `core.a`, the SDK static archives selected by the linker, and linker
scripts/flags for this exact board build. Only reviewed linked archives are
included; the full SDK's unrelated radio, speech, camera and Zigbee libraries
are not redistributed. GCC/newlib runtime notices, the GCC Runtime Library
Exception and Espressif binary-library licenses are preserved in `licenses/`.
The ESP-IDF source archive additionally retains per-file copyright notices.
Compiler runtime archives (including libgcc and libstdc++) are not redistributed
as separate `.a` files. `TOOLCHAIN-LIBRARIES.txt` identifies the matching libraries
to use from the recipient's separately installed Espressif compiler. The GCC
Runtime Library Exception covers the compiled combination in the firmware; it
does not waive GPL obligations for distributing standalone compiler libraries.

## Rebuild or relink

1. Clone the exact release tag using the command in `FIRMWARE-SOURCES.md`.
   Install the pinned CMake/Ninja and host C++ compiler from
   [the build guide](building.md). In Linux, run `cmake --preset firmware-linux`
   and `cmake --build --preset firmware-linux`. These commands obtain the SDK,
   compile both boards and generate their packages without accessing devices.
2. The source/object kit and its offline relink test remain internal build
   byproducts. They are not needed to install a public firmware package.
   Third-party source downloads and hashes are listed in `FIRMWARE-SOURCES.md`
   and `cmake/Dependencies.cmake` at the same commit.
3. To change Arduino, obtain the pinned `arduino-esp32-3.3.11.tar.gz` from the
   source links and copy `core_version.h` from the installed SDK into its
   `cores/esp32/` directory. Use that
   source with a separate Arduino SDK installation. The publication check
   deliberately rejects an altered SDK until its corresponding sources and
   notices have also been updated. For local experiments, invoke Arduino CLI
   compile directly with the FQBN and C++ flags in `allpowers/cmake/Dependencies.cmake`
   and `allpowers/cmake/scripts/project/BuildFirmware.cmake`.
4. Developers using the internally generated object kit can install
   `esp-x32/2601` and run:

   ```sh
   cmake -DTOOLCHAIN=/path/to/esp-x32/2601 -DKIT=/path/to/relink -P relink/RelinkFirmware.cmake
   ```

   The command produces `relinked.elf` without device access. Replace `lib/core.a`
   or the appropriate Arduino library objects with your rebuilt versions to
   use a modified library. The package builder exercises this command before
   accepting a firmware package. Changes to headers/ABI may require recompiling
   the complete application from the included sources.
5. Convert a rebuilt ELF with the matching Espressif `elf2image` tool and the
   board's original flash settings. Installation is a separate, explicit device
   maintenance operation: follow `allpowers/docs/building.md` and the board
   maintenance documentation, match the board identity, preserve NVS/history,
   and retain an accessible second-write and physical recovery path. Do not
   change partitions or write eFuses as part of relinking.

## Component terms

| Component | Terms / location in the source kit |
| --- | --- |
| ALLPOWERS application and model definitions | MIT, `allpowers/LICENSE` and model notices |
| Arduino core and libraries | LGPL-2.1-or-later and per-file terms, Arduino source archive |
| ESP-IDF | Apache-2.0 and component-specific terms, IDF source archive and `licenses/` |
| FreeRTOS, TinyUSB, TinyCrypt, TLSF, newlib | MIT/BSD variants and per-file notices, `licenses/` and source archives |
| NimBLE, Mbed TLS, ESP Diagnostics | Apache-2.0 and retained third-party notices |
| Espressif BT controller, PHY/BT baseband, coexistence archives | Apache-2.0, `bt-LICENSE`, `phy-LICENSE`, `coex-LICENSE`; binary-only portions retain those terms |
| GCC libgcc / libstdc++ | GPL with GCC Runtime Library Exception; generated firmware is not relicensed to GPL solely by these runtimes |

The presence of a license for a development tool or unused example in an
upstream source archive does not mean that tool or example is linked into the
firmware. `relink/LIBRARIES.txt` records the actual archive inventory.
