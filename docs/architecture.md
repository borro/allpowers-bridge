# Architecture

```text
ALLPOWERS BLE GATT
    ├── ESP32 BLE transport ── portable C++ core ── USB HID UPS + display/history
    └── Qt Bluetooth/WinRT ─── same C++ core ────── native Qt desktop/history
USB board CDC ── native serial worker ── TelemetryTransport ── same desktop/history
```

All product code and ABI tests are C++23. CMake/Ninja, CTest and CPack own the
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
No USB adapter is required. The optional Windows integration runs the same BLE
transport/decoder in a QCoreApplication service and publishes a versioned,
pointer-free state to a KMDF Battery Class miniclass. Viewers use local named-pipe
IPC and never acquire a second BLE connection. The kernel independently expires
updates at 15 seconds, without inventing critical/discharging data on loss.
This path remains experimental pending native VM and service BLE tests. See
[Windows UPS](windows-ups.md) for implementation, permissions and test boundaries.

TelemetryTransport abstracts BLE, service IPC and USB. USB I/O uses a separate
worker with bounded reads and connection generations. Discovery checks the USB
identity, then validates UPSDEV and the board profile. It never sends boot, reset,
time or station-selection commands. The archive's `usb_minutes` table is separate
from MAC-keyed station history and its retention limits. Undated records from
previous boots do not receive invented timestamps. [USB details](usb-monitor.md).

Qt widgets and BLE callbacks use the GUI event loop. SQLite, CSV export and
rotating logs live in a dedicated worker thread; at most one telemetry-history
job is outstanding, so slow disk does not build an unbounded queue. History
retains the old schema/path, preserving Python-monitor data without conversion.

Monitor graphs render at telemetry cadence and user interaction. The widget
animates its time axis and power scale briefly when a new sample changes them;
the animation stops between updates. Per-column
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
