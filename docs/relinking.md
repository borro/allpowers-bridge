# Static Qt build and relinking

The application source is MIT; the separate adapted driver is MS-PL. Qt Core,
Gui, Widgets, Bluetooth, Network and Sql 6.8.3 are
statically linked under LGPLv3. About displays embedded Qt/component notices;
AllpowersMonitor.exe --export-licenses <directory> extracts them.

Only the EXE and Windows system components are needed at runtime. History and
settings live in the user-data directory; no private DLLs are extracted at startup.
No restriction prevents reverse engineering, changing/relinking Qt, rebuilding or
running a modified executable.

Every release includes a separate sources ZIP with complete application source,
build instructions and the exact unmodified Qt Base/Connectivity archives used
for that build. The archives retain their original licenses/checksums. Keep this
source kit available alongside any binary distribution.

## Rebuild or replace Qt

1. Extract allpowers/ from the source ZIP. Install CMake, Ninja, MSVC 2022 C++ Build
   Tools and Windows 11 SDK 10.0.26100.0; open an x64 Developer PowerShell.
2. Copy qt-source/*.tar.xz to allpowers/build/deps/downloads/.
3. In allpowers/, run cmake --preset windows, then cmake --build --preset windows.
   CMake builds static Qt from the supplied verified archives and then the EXE.
4. For a modified Qt, build/install it with static libraries, /MT, bundled SQLite
   and the WinRT Bluetooth backend. Configure a fresh build directory with
   -DALLPOWERS_QT_ROOT=/path/to/modified/qt and rebuild. Use the standalone app
   CMake project if not creating a distribution/source kit. A custom distribution
   must supply the corresponding modified Qt sources as well.
5. Replace the old EXE with the rebuilt EXE and run normally. No signing key,
   activation, device unlock or permission from the author is required.

The source archive's generated VERSION file retains the release identity without
Git. In a Git checkout, the version comes from its tag. Source archives and Qt
build/plugin documentation:

- https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
- https://doc.qt.io/qt-6.8/configure-options.html
- https://doc.qt.io/qt-6.8/qt-import-plugins.html
