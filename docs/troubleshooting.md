# Troubleshooting

[Documentation](README.md) · [Getting started](getting-started.md)

## Bluetooth is busy or the station is missing

Some stations accept one BLE client. Disconnect the mobile application, other
Windows viewer or board that owns the connection, then scan again. To keep a
board connected, select USB in the Windows app. The app does not take over an
occupied station. An installed UPS service owns BLE and supplies viewers with
its data; see [connection ownership](windows-apps.md#connections-and-ownership).

Check the selected transport, PC Bluetooth availability and station range.
Monitor normally filters discovery by station name; it can also show unknown
devices. A listed advertiser is not proof of a working GATT connection or
supported telemetry. See [Bluetooth and models](BLUETOOTH.md). **F5** scans;
**Disconnect** cancels pending discovery/reconnection.

Board firmware keeps the last eight established BLE disconnections in RAM until
the next reboot. In USB Monitor diagnostics (or a read-only `DIAG` response),
`BLE DROP <sequence>` rows retain the stack name, raw 16-bit disconnect reason and its name,
64-bit board uptime in milliseconds, connection token and peer address, ages of
the last main/settings packets, and the last scan RSSI (not live link RSSI).
`request=telemetry_timeout` or `target_changed` identifies a firmware request;
`request=none` means no application-requested close was recorded, not proof that
the peer initiated it. `CONNECTION_TIMEOUT` is a link supervision timeout, not a
diagnosis of interference or distance. `UNAVAILABLE` differs from raw code zero.
NimBLE HCI errors retain their `0x200` offset (for example `0x0208` is HCI timeout
`0x08`); NimBLE host errors are decoded separately, not as HCI reason codes.
Records survive reconnection and station changes, but are not written to NVS or
the telemetry archive. The USB maintenance/update path and reconnect policy are
unchanged. Remove peer addresses before sharing diagnostics publicly.

## Readings are stale, unknown or incomplete

Stale means required telemetry has expired. It does not mean zero charge or an
empty battery. Inspect freshness, packet counters and the last error in
Diagnostics. Check [model compatibility](BLUETOOTH.md#compatibility-and-evidence):
some protocol families do not report mains source or USB output state.
Recognition of a model name does not establish hardware compatibility.

## USB board is missing

Select USB mode and use a USB data connection to a board running compatible
project firmware. Check the board's display and whether Windows exposes its
serial interface. The app validates USB identity and board profile; an arbitrary
serial device or a board in ROM update mode is not a telemetry endpoint.
Release the port in another viewer before retrying.

Monitor does not flash, reset or change the station on the board. Disconnecting
the PC port leaves the board's BLE and HID operation running. For an unprepared
board, follow [Updating and recovery](recovery.md), including identity and
physical recovery checks; do not use an unrelated image as a shortcut.

## History has gaps or archive import failed

Gaps represent stale/unknown data, session or clock changes, or records without a
trustworthy timestamp. Previous unclocked boots are skipped; the app does not
invent timestamps. Archive records have no station identity and remain stored
separately even when displayed in a merged chart.

Archive diagnostics distinguishes verified completion from partial/error
results. After an error, use the archive read action to retry; current firmware
safely replaces an export still active on the board and continues live status
during long reads. Switching boards clears the previous overlay.

Older firmware has weaker operation correlation across client restarts. Read
[archive compatibility](usb-monitor.md#archive-protocol-compatibility-and-completion)
before attributing an issue to missing history. Updates and rollback must follow
the [AHP3 recovery contract](recovery.md#ahp3-storage-and-supported-rollback)
to preserve stored history.

## Widget is not visible or its language did not change

Monitor and Widget now keep `diagnostics.jsonl` in their respective local data
directories (`%LOCALAPPDATA%/AllpowersMonitor` and `%LOCALAPPDATA%/AllpowersWidget`
by default). Widget's tray menu has **Open diagnostics folder**. Keep the current
file and its `.1`/`.2` rotations together; each rotates at 2 MiB (at most one
additional bounded record). No firmware update is needed for these PC logs.

The journal is independent of the history database. It records startup, requested
exit, completed teardown, Qt warnings/errors, transport/freshness transitions and
a heartbeat every 30 seconds. Each record has UTC time, process/run identity and
monotonic uptime. A run with no `runtime.end` means orderly teardown was not
recorded; it does **not** prove a crash (forced termination, power loss, log write
failure or rotation can also explain missing records). A delayed heartbeat can
indicate suspend or a stalled event loop; it does not distinguish them by itself.

USB records separate last received bytes, last accepted status, changing station
frame counters, firmware GATT state and archive activity. `usb.failure` preserves
these before closing the port. New `BLE DROP` rows from firmware are saved as
`board.ble_drop`, even if later diagnostics overwrite them. A firmware version
without those rows cannot supply the raw disconnect cause. Direct PC Bluetooth
records connection generations, Qt controller/service state and failure context;
Qt's error is not necessarily a raw radio disconnect reason.

For the next incident, note its time and whether the tray icon/process remains,
then keep these three log files before repeated restarts. `desktop.destroy` and
`desktop.create` help identify Explorer attachment recreation. The log masks
common device addresses, board serials and the home-directory prefix, but inspect
free-form error messages before sharing. It does not collect raw packet payloads
or the history database. Repeated events are rate-limited; `dropped_records`
reports omitted records when the next write succeeds. Nothing is uploaded.

Look for Widget's tray icon and its Explorer attachment status. The card is on
the desktop, behind normal windows; **Win+D** reveals the desktop. If attachment
is unavailable, the app retries and reports the reason in the tray instead of
creating a floating fallback window.

Use the tray menu for settings, position locking, connection and exit. Language
preferences are separate for Monitor and Widget and apply after that app restarts.
`--language` affects one run only. See [Windows applications](windows-apps.md).

## Download or launch problems

Compare the downloaded file's SHA256 with the release's `SHA256SUMS`. Choose the
Windows x64 EXE assets rather than GitHub's source-code archives. Qt and Python
do not need to be installed for the published applications.

Release EXEs are compressed with UPX; some antivirus products may flag packed
executables. Do not disable protection to dismiss a warning. `upx -d` can unpack
a copy for inspection; the resulting bytes will differ from the published hash.
The ordinary development build is uncompressed. See [Building](building.md)
for rebuilding from the corresponding sources.

## The computer does not act like a UPS

Standalone Monitor/Widget viewing supplies telemetry to the user. USB HID UPS
through a supported board is a separate path. The native Windows service/driver
is another, experimental path whose power actions have not been validated.
Check [hardware evidence](validation.md) and [Windows UPS status](windows-ups.md).

Fresh values or successful HID reporting alone do not prove automatic shutdown.
The station determines electrical backup/transfer behavior; the OS determines
battery policy. Keep those checks separate.

## Report a problem

Include the application version, Windows version, transport, board profile or
station model, reproduction steps, and whether the problem occurs in demo mode.
Inspect diagnostics/log excerpts before attaching them. Remove station addresses,
USB serials, personal paths and other identities; never upload recovery images
or a user history database. [Open an issue](https://github.com/borro/allpowers-bridge/issues).
