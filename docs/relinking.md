# Static Qt build and relinking

The application source is MIT; the separate adapted driver is MS-PL. Qt Core,
Gui, Widgets, Bluetooth, Network and Sql 6.12.0 are
statically linked under LGPLv3. About displays embedded Qt/component notices;
AllpowersMonitor.exe --export-licenses <directory> extracts them.

Each application (monitor or widget) needs only its EXE and Windows system components at runtime. History and
settings live in the user-data directory; no private DLLs are extracted at startup.
No restriction prevents reverse engineering, changing/relinking Qt, rebuilding or
running a modified executable.

Every release includes a separate sources ZIP with complete application source,
build instructions and the exact unmodified Qt Base/Connectivity/Tools archives used
for that build. The archives retain their original licenses/checksums. Keep this
source kit available alongside any binary distribution.

## Rebuild or replace Qt

1. Extract allpowers/ from the source ZIP. Install Git, CMake 4.4+, Ninja, licensed
   Visual Studio 2026 (Community is suitable for qualifying individual/open-source
   use) with C++ and Windows 11 SDK 10.0.26100.0; open an x64 Developer PowerShell.
   Build Tools alone has separate license prerequisites; see windows-runtime-terms.md.
2. Copy qt-source/*.tar.xz to allpowers/build/deps/downloads/.
3. In allpowers/, run cmake --preset windows, then cmake --build --preset windows.
   CMake builds static Qt from the supplied verified archives and then the EXE.
4. For a modified Qt, build/install it with static libraries, /MT, bundled SQLite
   and the WinRT Bluetooth backend; install matching LinguistTools (lrelease/lupdate). Configure a fresh build directory with
   -DALLPOWERS_QT_ROOT=/path/to/modified/qt and rebuild. Use the standalone app
   CMake project if not creating a distribution/source kit. A custom distribution
   must supply the corresponding modified Qt sources as well.
   Official release packaging checks the managed SDK's source/library identity
   and rejects an unverified or altered SDK instead of shipping unrelated sources.
   This distribution safeguard does not prevent local rebuilding with changed Qt.
5. Replace the old EXE with the rebuilt EXE and run normally. No signing key,
   activation, device unlock or permission from the author is required.

## Source-kit validation

The `release` target rebuilds and tests the portable core and both Windows
applications from the source kit using the matching static Qt SDK. It verifies
the source archives and their correspondence to the distributed SDK libraries.
Tagged and ordinary CI builds use the same packaging checks. They do not build
another Qt SDK with a modified library. The manual replacement procedure above
remains available for rebuilding with a modified Qt.

The source archive's generated VERSION file retains the release identity without
the original `.git` directory. Git is still required by the publication regression
tests. In a Git checkout, the version comes from its tag. Source archives and Qt
build/plugin documentation:

- https://download.qt.io/archive/qt/6.12/6.12.0/submodules/
- https://doc.qt.io/qt-6.12/configure-options.html
- https://doc.qt.io/qt-6.12/qt-import-plugins.html
