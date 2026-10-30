# USB HID telemetry

[Documentation](README.md) · [Updating and recovery](recovery.md)

Choose **USB · Waveshare / Pocket**, discover the board and connect. Monitor and
Widget can select the same board at the same time. Each reads directly through
Windows HID; there is no broker, service or extra driver. Direct PC Bluetooth
retains its existing exclusive connection policy.

```text
ALLPOWERS → BLE Central/GATT on board → USB HID → Monitor
                                            → Widget
                                   HID UPS → Windows battery driver
                           maintenance HID → explicit ROM update
```

The application firmware no longer enumerates CDC/COM. Old CDC firmware is
listed with **Firmware update required**; the new applications do not fall back
to COM or update a board automatically. ROM still exposes its separate serial
interface when deliberately entered for flashing. Saved board serials and station history remain valid. The incompatible old USB
archive cache is discarded when moving to the new format.

Discovery selects VID 303A/PID 4002, the generated board serial, Usage Page
`0xFF01`, Usage `1`, and 64-byte report capabilities. It queries HID metadata
without requesting telemetry. Opening a selected board revalidates VID/PID,
serial and collection before any telemetry request. Duplicate identities are
not selected arbitrarily. The worker uses shared read/write handles, bounded
overlapped I/O and cancellation; the UI never waits for USB.

## Collections and reports

The existing maintenance collection remains at `0xFF00`, report 1; its probe and
`BOOT-R2500!!` command are unchanged. The UPS descriptor and reports 2/3 are
unchanged. Telemetry is registered after those collections before USB starts.
The HID-only USB device uses class/subclass/protocol zero at device level.

| Report | Type | Purpose |
| --- | --- | --- |
| 4 | Feature | `APHID1`, protocol version 2, random boot identity, limits and clock capability |
| 5 | Feature, volatile | Atomic live snapshot, independently polled once per second |
| 6 | Output | Metadata, diagnostics, archive and boot clock synchronization |
| 7 | Input | Correlated response fragments and optional station packet events |

Every report is 64 bytes including its report ID. Payload integers use explicit
little-endian encoding; C++ structure layout is not the wire format. The shared
[wire codec](../core/src/HidTelemetryWire.h) specifies offsets and checksums.
Capabilities, live snapshots and requests have a CRC32 over their first 59
payload bytes. Request IDs contain 128 random bits from Windows CNG.

Responses echo the operation ID, boot identity and opcode. Each fragment carries
its byte offset, complete message length and CRC32, followed by up to 28 bytes.
Messages are bounded to 8192 bytes. Each client continuously drains Input reports,
ignores foreign IDs, rejects gaps/corruption, and retries the same operation after
10 seconds, at most three times. A reboot invalidates the session. Optional packet
events use a zero operation ID; they are best effort and do not confirm an archive.

Live Feature reads have no shared cursor and cannot consume another reader's
data. They include station identity, model, known-field mask, connection/freshness,
separate main/settings ages, frame counts and uptime seconds. Unknown values remain unknown.
Metadata and diagnostics retain their existing JSON content inside framed messages.
The desktop projects HID messages into the existing UI/history decoder internally;
legacy line commands are not transmitted over USB.
Initial metadata bootstraps board verification. After the first Live snapshot,
metadata continues updating board/archive information and the station-name cache,
but only Live publishes telemetry and station changes. Legacy STATUS rows inside
metadata cannot replace runtime, known fields or the separate firmware ages.

## History and diagnostics

Two independent archive snapshots are supported, each holding at most 2880
records in PSRAM. `Open` uses the client's random archive identity and returns
session/generation, record count, first/last sequence and full digest. Retrying an
active Open returns the same snapshot. `Page` reads up to eight records by ordinal;
retries return the same bytes. `Close` releases only the named snapshot. Busy
slots return `BUSY`; a new reader never cancels another reader.

Snapshots expire after 30 seconds without requests or 180 seconds total. Clean
disconnect attempts Close; a crashed client is reclaimed by expiry. Requests are
bounded and queued in USB callbacks; capture/serialization runs in the firmware
loop. Clients request one archive page at a time. One response fragment is sent
per loop after due UPS reports, so archive traffic yields to the system UPS.

The client verifies record CRCs, operation/boot identity, count, sequence order,
cursor and the firmware's frozen digest before reporting a complete import.
CRC failures, gaps, expiry or disconnect cannot become a successful empty archive.
Station changes terminate the current import. NVS station settings, flash partitions
and 32-byte slots are unchanged. Storage is **AHP3 only**; AHP1/AHP2 readers,
writers, legacy CDC archive commands and frozen old transport copies are removed.
The incompatible old board archive is intentionally discarded on transition.

Each boot reserves a monotonically increasing 32-bit epoch in NVS before any
history record is written. Failed persistence or exhaustion disables history
rather than reusing an epoch. Every record carries this epoch, a sequence number
and uptime seconds. The HID session additionally uses a random boot identity to
reject responses from an earlier connection.

Monitor and Widget offer UTC time with the uptime from a recent live snapshot.
The first accepted Clock request writes a dated sample before activating the
clock for that boot. Retries and another client cannot move this anchor or cause
extra writes. The board has no battery-backed clock: a new boot needs a new
anchor. Desktop imports infer missing dates only within the same board and boot
epoch; conflicting anchors do not produce guessed dates.

Board records have no station identity and remain separately keyed by board
serial. Up to 2880 records are retained, including undated records and dated
records older than 48 hours after downtime. Monitor's **History by epochs** view includes all
retained records, with or without dates. Each equally sized section represents
one boot epoch; samples inside it follow record order. Sections are separated,
and hover shows the epoch, sequence and uptime. Wall-clock dates are neither
invented nor modified by this view or its CSV export. Zoom and pan remain
available; **All epochs** restores the complete view. The selected history view
is remembered between launches. Dated views use real timestamps and show dates across day boundaries.
The archive-only view ends at its last imported sample, shows that timestamp,
and refreshes with **Read from board**. CSV includes sequence and epoch
(session); wall zero denotes an unknown date. Neither viewer sends station
selection, screen, reset, firmware or power-control commands. Select a station
with the board's physical controls.

## Validation and maintenance

Host tests cover framing, corruption, retries, two concurrent exports, snapshot
limits/expiry, independent clients and shared transport leases. They do not open
real USB devices. Firmware builds and package checks do not flash a board.

Hardware acceptance is separate: verify both viewers and two full exports while
Windows continues receiving UPS reports; test reconnect and sleep/resume. For
each explicitly selected board, verify physical ROM entry and a compatible
rollback before writing. After first activation, prove an actual second write
through maintenance HID → ROM before further experiments. Successful compilation
alone is not evidence for Windows HID fan-out, functioning UPS or recovery.
