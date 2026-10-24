# ALLPOWERS Monitor — native C++ / Qt

Run AllpowersMonitor.exe on Windows 10/11 x64. It uses the computer's Bluetooth;
no Waveshare/Pocket, Python runtime or UPS driver is required. Connection is
explicit. Some stations allow only one BLE client at a time.

Pages: telemetry, interactive history, diagnostics. Ctrl+1/2/3 switches pages;
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
within 48 hours. All disk/history/export work runs separately from the GUI.

--demo shows synthetic data using a temporary directory; it never connects BLE.
--self-test -platform offscreen performs an offline core/UI/database smoke test.
The diagnostics page includes source RAW, freshness, packet counters, GATT UUIDs,
MTU, last packet/error, history state and chart rendering time.

The runtime is one EXE. Qt, SQLite, plugins and the C++ runtime are statically
linked. About displays embedded notices; --export-licenses extracts them.
The separate sources ZIP contains corresponding application/Qt source and
docs/relinking.md. Users can replace/relink Qt; the application does not restrict
that. Product version comes from Git tags; --version prints the embedded version.

--verify-station 50 --report <path> explicitly tests the saved station for several
seconds of fresh telemetry, then disconnects. This is a manual hardware test;
normal CTest/build/package targets never invoke it or switch BLE connections.
