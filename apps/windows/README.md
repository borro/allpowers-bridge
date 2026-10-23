# ALLPOWERS Monitor 0.3 — native C++ / Qt

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

Qt is linked dynamically under its open-source terms. The licenses/Qt folder
contains license texts. Unmodified Qt 6.8.3 sources are available at
https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz
Users can replace/relink the Qt libraries; this application does not restrict that.
See THIRD_PARTY_NOTICES.md for dependency notices.
