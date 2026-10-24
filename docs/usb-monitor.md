# USB telemetry in Windows Monitor

Choose **USB · Waveshare / Pocket** at the top, click **Найти платы**, select the
board and connect. Bluetooth mode remains in the same selector. The app saves
the last transport, board serial and Bluetooth station/profile separately.
Discovery does not open arbitrary ports. A selected absent board is retried by
serial even if its COM number changes. An occupied port produces an error; the
monitor does not force-close another application.

```text
ALLPOWERS → BLE Central/GATT on board → USB CDC → Monitor → charts/history/CSV
```

Both `waveshare-147` and `pocket-096` use the existing firmware protocol. A ROM
bootloader or factory demo without that protocol is not a telemetry source. No
flashing is needed with our application firmware. USB HID UPS and the existing
recovery/update interfaces remain unchanged. No virtual battery driver or extra
runtime is required for the monitor's USB mode.

The app checks VID 303A/PID 4002 and the firmware serial pattern, then requires
valid UPSDEV status, protocol/known-field information and a supported board ID.
Native serial I/O runs in a worker at 115200 baud, with timeouts, bounded reads
and connection generations. The GUI receives bounded complete JSON lines.
Known-field masks are retained; missing fields do not become zeros. Firmware
freshness, frame progress and USB silence gate freshness in the UI/history.
The old STATUS command takes runtime from the HID report. Runtime is therefore
shown only for a current confirmed UPS report; unsupported profiles or a retained
HID runtime produce an unknown value rather than an old estimate.

Only `STATUS`, `DIAG` and bounded `HISTORY-READ` requests are sent. The monitor
never sends 1200-baud touch, boot/reset/firmware/clock/screen/station-selection
commands. **Отключить** closes the USB port; the board continues BLE, HID reporting
and recording. Change the station with the board's BOOT picker. Some stations
allow one BLE client: to switch to direct PC Bluetooth, release the board's
connection yourself, for example by unplugging it.

## History and diagnostics

Current readings use the station address reported by the board and share local
station history, charts, source-change markers and CSV export. Board diagnostic
rows appear in the existing parameter/value table, alongside USB port, serial,
profile, errors and archive progress. Optional Windows driver controls are in
**Диагностика → ИБП Windows…**.

The old flash record format does not contain a station identity. Its records are
stored in a separate `usb_minutes` table keyed by board serial. On connection,
the monitor automatically reads the archive and merges it into the main History
chart (**История станции + USB**, the default). Saved archive points also appear
before a new read finishes. Application history takes priority in overlapping
minute buckets. The archive note and point tooltip identify board records whose
station is unknown; they retain their board identity in storage and CSV export.
There is one fused line per metric, not separate archive/live layers. A source
or application-session boundary is connected only where consecutive fresh board
records establish continuity. The verified current USB boot also allows the live
tail to follow its latest minute sample (up to 90 seconds). No link is persisted
as raw telemetry or accepted over IPC. Actual stale samples, marked gaps and boot
boundaries without coverage still interrupt the line.
**Только архив USB-платы** remains available for inspecting the archive alone.
Switching boards clears the previous board's overlay. Import does not evict
station history. CRC32 is checked, repeated imports deduplicate minute buckets,
and at most 2880 archive points within 48 hours are retained separately from the
existing 3600 live / 2880 station-minute points.

Clocked records keep their timestamps. Without a clock, only the current boot can
be anchored to PC time using session and uptime. Previous unclocked boots are
skipped and counted under **Пропущено без достоверной даты**. No clock-setting
command is sent automatically. **Считать с платы** repeats the bounded read; an
unfinished import times out after 60 seconds. SQLite/import/export work remains
outside the UI thread.

With optional integration installed, Bluetooth mode uses the service. Selecting
USB detaches this viewer without stopping the service or changing its Bluetooth
source. USB mode does not publish another virtual battery: the physical board
already provides HID UPS reporting where supported.

## Tests

CTest uses fake serial channels only: both profiles, fragmented/malformed/oversize
JSON, known fields, source/freshness, stale counters, CRC, missing dates, separate
archive retention, deduplication, live UI/history and transport/control placement.

Manual commands, never invoked by build/test:

```text
AllpowersMonitor.exe --scan-usb --report <private-json>
AllpowersMonitor.exe --verify-usb 25 --usb-device <exact-serial> --usb-board <profile> --report <private-json>
```

Manual verification closes its port afterwards. Reports contain real identities;
keep them private and out of Git.
