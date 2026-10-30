# Windows applications

[Documentation](README.md) · [Getting started](getting-started.md) · [Troubleshooting](troubleshooting.md)

Monitor and Widget run on Windows 10/11 x64. Each is a standalone EXE with Qt,
SQLite, platform plugins, the protocol core and the C++ runtime statically linked.
Neither needs a separate Qt/Python installation or a UPS driver for viewing.
USB HID allows Monitor and Widget to read the same adapter independently, including
archive exports. No broker or service is started. Direct BLE still uses its
existing exclusive connection lease. Older CDC firmware requires an explicit
firmware update; the applications do not fall back to COM.

## Monitor

![Monitor overview](images/windows-monitor-en.png)

Monitor has three pages: overview, history and diagnostics. **Ctrl+1/2/3** switches
pages; **F5** scans for devices. Choose Bluetooth or USB at the top. Device and
profile controls explain when they are unavailable; dropdowns support the mouse,
arrow keys, Enter and Escape. Disconnect remains available during scanning.

On normal startup Monitor searches for and reconnects to the last saved station
using its Bluetooth address and protocol profile. If unavailable, discovery is
retried. **Disconnect** cancels the wait and retries. **F11** toggles fullscreen
and restores the previous normal/maximized mode; overview cards and text scale
with the window.

## History and export

![Monitor history](images/windows-history-en.png)

Graphs show green charge (%), blue charging input and white output (watts), with
separate percent/watt scales. Use the mouse wheel to zoom, drag to pan and hover
for exact values. Toggle series independently, return to live data, select
1/24/48 hours, or choose a linear/logarithmic time axis. CSV export is available.
Red/green markers indicate confirmed battery/mains changes.

Unknown or stale data, session changes and clock discontinuities break lines.
Gaps are intentional: missing data must not become an invented battery value.
Disk, history and export work runs separately from the GUI.

In USB mode, the board archive fills gaps in the main history chart. There is one
merged line per metric, but archive records retain their separate provenance:
they have no station identity. Undated records from previous boots and corrupt
records are skipped. Live application history takes priority when periods
overlap. See [USB history](usb-monitor.md#history-and-diagnostics) for retention,
timestamp rules and the archive-only view.

## Desktop Widget

![Desktop Widget](images/windows-widget-en.png)

Widget runs independently of Monitor. On first launch, select Bluetooth or USB
in Settings, discover a device and save. Its 300 × 210 desktop card shows the
device name, charge, input/output, remaining time in days/hours/minutes, and two
power traces for the last hour.

- Drag the card anywhere except its menu button to move it.
- Right-click or use the tray icon for settings, connect/disconnect, position
  locking and exit. Login startup is optional and off by default.
- Normal windows cover the card; **Win+D** leaves it on the desktop.
- If Explorer attachment is unavailable, the tray reports why and attachment
  retries automatically. No floating fallback window appears.

The logarithmic time axis gives recent seconds more space within the hour.
Consecutive valid samples join; isolated samples have no endpoint dot. On USB
connection/reconnection, Widget restores its saved board cache, reads the archive
and merges it with current samples, preserving real gaps. Switching boards or
returning to Bluetooth clears the old archive overlay.

## Connections and ownership

Monitor and Widget take turns owning a direct connection. Disconnect in one
before connecting in the other. Ownership protection requires current versions
of both applications; disconnect an older running Monitor manually. Some stations
also allow only one BLE client, including mobile apps and boards.

USB mode uses the board's existing BLE connection and does not acquire PC
Bluetooth. Disconnect closes only the PC serial port; the board continues BLE,
HID reporting and recording. Change the board's selected station with its BOOT
picker. Transport, USB serial and Bluetooth station are remembered separately.

If the optional UPS service is installed, it owns BLE and shared history.
Viewers use its readings, and closing Widget does not stop the service. Widget
owns its USB port directly. Service/driver integration remains experimental;
see [Windows UPS status](windows-ups.md) before considering it.

## Language

Choose **Windows language**, **Русский**, **English**, **简体中文**, or **繁體中文**
beside **About** in Monitor or in Widget's **Settings**. Each app saves its own
choice; close and reopen that app to apply it. Changing only Widget's language
does not require a selected device or reconnect and does not change login startup.

The system default is Russian for Russian Windows, Simplified Chinese for
China/Singapore, Traditional Chinese for Taiwan/Hong Kong/Macao, and English
otherwise. Translations are embedded. Chinese uses installed Microsoft YaHei /
Microsoft JhengHei fonts; font files are not bundled.

`--language system|ru|en|zh-CN|zh-TW` overrides only the current run and is not
saved. Combine it with `--demo` for an isolated preview. OS error text, legacy
service messages, RAW data, historical logs, device names and license texts keep
their original language. CSV columns and machine identifiers are language-independent.

## Settings and storage

| Application | Location |
| --- | --- |
| Monitor | `%LOCALAPPDATA%/AllpowersMonitor` |
| Widget | `%LOCALAPPDATA%/AllpowersWidget` |
| Optional UPS service | `%PROGRAMDATA%/AllpowersMonitor` |

Monitor stores settings, `history.sqlite3` and rotating `monitor.log` files.
Widget keeps its own settings and history. Monitor retains compatibility with
the old JSON/SQLite data. Service integration retains/merges legacy per-user
history; see [Windows UPS integration](windows-ups.md).

History retains at most 3600 live and 2880 minute points across stations, plus a
separate 2880-point USB archive within 48 hours. Archive records remain keyed by
board identity rather than an invented station identity.

## Diagnostics and licenses

Diagnostics shows a grouped parameter/value table, including freshness, source
RAW, packet counters, GATT UUIDs, MTU, last packet/error, history state and chart
rendering time. USB adds board details and archive progress. Full JSON can be
copied for troubleshooting; remove device identities before sharing it publicly.

The event log follows new entries. Scroll up to pause following; **Latest entries ↓**
resumes it. The view retains 2000 lines; the disk log rotates independently.
Experimental driver management is under **Diagnostics → Windows UPS…**.
The default embedded driver is unsigned and installation is restricted to a test
VM with a trusted signed payload. Native driver/service BLE and Windows power
actions are not validated; VM tests remain deferred.

**About** displays notices; `--export-licenses` extracts them. `--version` prints
the Git-derived embedded version. Published EXEs are UPX-packed; see
[Troubleshooting](troubleshooting.md#download-or-launch-problems) and
[relinking](relinking.md) for distribution details.

Time and epoch views use the same combined station/USB dataset, including live
readings as they arrive. Application samples take priority over overlapping
archive minutes in both views. Undated records are hidden on the time
chart and retained in epoch mode, which groups all samples by board and boot. New USB samples
persist their board identity, boot epoch and board uptime. Older local samples
without this information appear in an explicit unknown-epoch group. Power axes
remain independent, and CSV exports retain original dates rather than chart
positions. The archive-only view remains a snapshot refreshed by Read from board.
