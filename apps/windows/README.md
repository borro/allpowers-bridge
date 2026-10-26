# ALLPOWERS Monitor — native C++ / Qt

Run AllpowersMonitor.exe on Windows 10/11 x64. It uses the computer's Bluetooth;
no Waveshare/Pocket, Python runtime or UPS driver is required. Normal startup
reconnects to the saved station. Some stations allow only one BLE client at a time.

Optional experimental integration runs BLE/history in a LocalSystem service and
reports to a KMDF Battery Class driver. The monitor then uses local IPC only;
closing a window leaves the service running. Shared history/configuration lives
under %PROGRAMDATA%/AllpowersMonitor; legacy per-user history is retained/merged.
The Windows UPS dialog manages integration. The default embedded package is
unsigned and installation is restricted to a test VM with a trusted signed payload.
Native driver behavior and real service BLE have not yet been validated. See
[Windows UPS status and lab procedure](../../docs/windows-ups.md).

Pages: telemetry, interactive history, diagnostics. Ctrl+1/2/3 switches pages;
select Bluetooth or USB at the top. USB uses the existing Waveshare/Pocket CDC
protocol, retains HID UPS reporting and never acquires computer Bluetooth.
Live readings share station charts/history. Old board records have no station
identity and are stored separately while filling gaps in the main USB chart; corrupt or
undated records are skipped. [USB mode](../../docs/usb-monitor.md).
Driver management is in Diagnostics, not the main navigation.
The desktop widget uses the same USB history merger: it reads the archive on
connection/reconnection, restores its saved board cache and renders one trace
per power metric. Switching boards or returning to Bluetooth clears that overlay.
F5 scans. Device/profile controls explain why they are unavailable. Dropdowns
work with mouse, arrows, Enter and Escape. Disconnect remains available while scanning.

Graph: green charge (%), blue charging input, white output (watts). Separate
percent/watt scales; wheel zoom, drag to pan, hover for exact values and return
to live. Toggle series independently, choose 1/24/48 hours or logarithmic time.
Red/green markers mean a confirmed battery/mains transition. Unknown/stale data,
session changes and clock discontinuities break the lines. CSV export is available.

Settings, history.sqlite3 and rotating monitor.log files live in
%LOCALAPPDATA%/AllpowersMonitor. The old monitor's JSON and SQLite schema remain
compatible. History keeps at most 3600 live + 2880 minute points across stations
and a separate 2880-point USB archive
within 48 hours. All disk/history/export work runs separately from the GUI.

--demo shows synthetic data using a temporary directory; it never connects BLE.
--self-test -platform offscreen performs an offline core/UI/database smoke test.
The diagnostics page includes source RAW, freshness, packet counters, GATT UUIDs,
MTU, last packet/error, history state and chart rendering time.

The monitor and desktop widget each run as an independent EXE. Qt, SQLite,
plugins and the C++ runtime are statically
linked. About displays embedded notices; --export-licenses extracts them.
The separate sources ZIP contains corresponding application/Qt source and
docs/relinking.md. Users can replace/relink Qt; the application does not restrict
that. Product version comes from Git tags; --version prints the embedded version.

--verify-station 50 --report <path> explicitly tests the saved station for several
seconds of fresh telemetry, then disconnects. This is a manual hardware test;
normal CTest/build/package targets never invoke it or switch BLE connections.

Russian, English, Simplified Chinese and Traditional Chinese are embedded in both EXEs. The monitor's selector beside About
and the widget's Settings offer Windows language / Русский / English / 简体中文 / 繁體中文. The choice
is independent per application (`ui_language`: `system`, `ru`, `en`, `zh-CN`, `zh-TW` in its existing
settings.json), takes effect after restarting, and defaults to Russian on Russian
Windows, Simplified Chinese on Chinese/Singapore Windows, Traditional Chinese on
Taiwan/Hong Kong/Macao Windows, or English otherwise.
`--language system|ru|en|zh-CN|zh-TW` overrides only this run;
combine it with --demo or --self-test for offline checks. Changing only the widget
language does not require a selected device, reconnect, or change startup settings.

Service diagnostics optionally include `ui_messages` with `phase`, `error`,
`driver_error`, and `ups_reason` descriptors: a stable `id` and positional `args`.
The client renders known descriptors in its own language; unknown/missing IDs fall
back to the existing text. Error replies may also carry `ui_message`. This is an
additive extension of the existing IPC version. OS errors, legacy service text,
RAW data, historical logs, device names, and license texts retain their original
language. CSV column names and machine identifiers remain language-independent.

The static Qt SDK must include matching LinguistTools. `update-translations` is
the explicit catalog extraction target; ordinary builds only compile the checked-in
catalogs with lrelease and embed it. `translation-coverage` extracts into a disposable
catalog and rejects untranslated additions. Source packages include pinned Qt Tools
sources for rebuilding the translation tools.

Chinese text uses the installed Microsoft YaHei / Microsoft JhengHei font families.
The offscreen renderer registers these Windows fonts too, so offline screenshots
and glyph tests exercise the same Chinese scripts. No font files are bundled.
Glyph cases report a skip on server images without those optional fonts; catalog,
locale, preference, IPC and standalone-EXE checks still run for both variants.
