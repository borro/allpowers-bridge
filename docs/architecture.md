# Architecture

```text
ALLPOWERS BLE GATT
    ├── ESP32 BLE transport ── portable C++ core ── USB HID UPS + display/history
    └── Qt Bluetooth/WinRT ─── same C++ core ────── native Qt desktop/history
```

All product code and ABI tests are C++17. CMake/Ninja, CTest and CPack own the
build graph, tests and packaging. Python remains only in optional explicit board
maintenance tools; it is not part of the build or Windows application.

The portable core owns model hints, packet validation, units, known-field masks,
freshness, read-only polls and reconnect timing. It has no Arduino, OS, USB or
display dependency. C ABI v1 remains stable for embedding/testing; native tests
link the real Windows DLL or Linux SO. The Qt app statically links the identical
implementation, Qt plugins and MSVC runtime into one EXE.

Firmware shares BLE/UPS/history/UI code. BoardProfile owns wiring, geometry,
byte order, optional RGB/backlight and page counts. DisplayPanel adapts controller
commands; Dashboard adapts layout. Each board stages its own sketch, including
ESP32-generated sdkconfig, outside source directories.

The desktop uses Qt Widgets and WinRT Bluetooth through Qt's MSVC backend.
GATT discovery validates service, RX notification/indication and writable TX
where a read-only poll is required. Subscription confirmation gates connection
state. Generation tokens reject old notifications after stop/reconnect/selection.
Disconnect cancels retries and timers. Scanning never switches the selected peer.
There is no desktop UPS driver and no dependency on a USB adapter.

Qt widgets and BLE callbacks use the GUI event loop. SQLite, CSV export and
rotating logs live in a dedicated worker thread; at most one telemetry-history
job is outstanding, so slow disk does not build an unbounded queue. History
retains the old schema/path, preserving Python-monitor data without conversion.

Graphs render at telemetry cadence, not in a busy animation loop. Per-column
extrema bound painter vertices while retaining peaks. Wheel zoom, panning,
crosshair values, series visibility, 1/24/48-hour ranges and linear/log axes are
local UI operations. Session, timestamp and freshness gaps break lines; known
source changes receive red/green markers. Rendering time is visible in diagnostics.

The upstream catalog is broader than tested hardware coverage. Unsupported/stale
fields never turn into invented UPS values. R2500 V2 source mapping is FFFF =
battery, 0000 = mains; charging input watts alone do not indicate mains. This
accepted earlier mapping was not newly validated on hardware during restructuring.

Public firmware contains no selected station address. Existing NVS selection
remains; a fresh device opens the picker. Adapter MAC/serial and rollback paths
are ignored local host configuration, separate from public board capabilities.
