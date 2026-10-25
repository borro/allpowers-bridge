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

Only `STATUS`, `DIAG` and bounded archive read requests are sent. Export v3 uses
`HISTORY-EXPORT <request-id>` and freezes at most 2880 retained records. Begin,
each row and completion identify the request, boot session and captured generation.
Count, order, final cursor and a rolling record digest must agree before the
desktop reports `complete`. The monitor
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

Flash records do not contain a station identity. Their records are
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
command is sent automatically. **Считать с платы** repeats the bounded read.
New v3 firmware bounds capture/transmission retries to 60 seconds with a final
2-second drain limit; the desktop allows 70 seconds. Legacy multi-page reads
have a 120-second overall deadline. SQLite/import/export work remains outside
the UI thread.

## Archive protocol compatibility and completion

`HISTORY-READ <first> <sequence-count>` retains the old limit of 2880 sequence
numbers per request. New firmware projects physical AHP2 records to an AHP1 wire
record and recalculates its CRC for this command, leaving flash bytes unchanged.
An old parser can therefore read mixed archives. AHP1 cannot carry AHP2's field
validity/model. If a new record has unknown outputs, its legacy projection clears
`Fresh`: an old graph may omit that point instead of inventing a known OFF state.
Use the versioned export with a current desktop to preserve known/model data.
Wire compatibility does not make an AHP1-only firmware writer a safe rollback;
the separate [maintenance gate](recovery.md#ahp1ahp2-storage-and-supported-rollback)
checks that case.

For v3, firmware captures a bounded immutable copy under the storage mutex and
then transmits outside that lock. Busy, end and storage error are distinct; short
writes retain their offset and acknowledge a cursor only after the whole line.
Optional notification logs are suppressed and other console responses deferred
during a dump to prevent interleaving. Appends and retention after capture do not
change that snapshot. A new request cancels the previous one. If cancel/timeout
abandoned a partially accepted line, firmware retains that framing state and
sends a bounded newline barrier before opening the next snapshot or advancing
ACK. The desktop treats its own matching begin as the transaction boundary;
prior framing residue cannot spoil a valid retry. Corruption after that begin
still prevents complete. Old request/session rows cannot complete the replacement
import.

Diagnostics distinguishes `usb_history_result` values `reading`, `complete`,
`partial` and `error`; `usb_history_complete` is true only for verified completion.
Accepted valid points may be saved even when a later row is damaged or missing.
The received-record count includes records skipped because their dates are unknown;
wire completeness and the number of usable plotted points are separate measures.
A disconnected board, new boot, unavailable storage, lost/corrupt row, premature
done or deadline cannot be reported as complete. Repeat **Считать с платы** after
a partial/error result.

Old v2 cursor exports have no verifiable complete footer; the new desktop labels
them partial. With original firmware, it freezes the advertised session/count/latest,
reads backwards in up to three legacy pages, deduplicates sequences, and rechecks
the advertised inventory before confirming completion. The search bound is 5888
nonzero sequence positions: legacy boot recovery inspected at most 2880 positions,
the first append could skip at most a 128-slot erase sector, and an initial RAM
record could survive fewer than 2880 more appends. RAM can thus retain a record
whose physical slot has already been erased; a 4096-slot search is insufficient.
An append/retention or boot change during these live legacy reads, an unconfirmed
count, or exhaustion of that bound produces an explicit partial result.

With optional integration installed, Bluetooth mode uses the service. Selecting
USB detaches this viewer without stopping the service or changing its Bluetooth
source. USB mode does not publish another virtual battery: the physical board
already provides HID UPS reporting where supported.

## Desktop widget

The widget automatically reads the archive when USB connects or reconnects. Its
own history worker also restores previously saved records for the selected board
before that read finishes. It uses the monitor's merger, with application samples
taking priority and one line per power metric. Real stale/boot gaps remain.
Board records retain their original unidentified provenance and remain separate
from station history in storage. Switching boards clears the previous archive;
Bluetooth/service mode does not include a USB board overlay. The one-hour graph
refreshes every second, with disk work on the history worker. Repeated reads do
not duplicate records. New AHP2 records retain CRC-protected model and field
validity; old AHP1 records keep output states unknown because their validity was
never stored. The monitor and widget still use a direct connection in
turn, so disconnect the monitor before connecting the widget to the same port.
The widget uses logarithmic time over that hour to keep live-second changes
visible on its compact plot, instead of compressing each second into 0.075 pixels.
It draws lines between consecutive valid samples; an isolated sample has no endpoint dot.

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
