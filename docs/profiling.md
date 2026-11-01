# Profiling portable adapter logic on a PC

[Documentation](README.md) · [Building](building.md)

`allpowers_logic_bench` runs the production C++ telemetry decoder, AHP3 history
ring and HID archive protocol on synthetic data. It has no Bluetooth, USB,
serial, board-maintenance or filesystem-storage backend. History writes affect
only an in-memory flash model; the executable never accesses a physical device.
No station selection or firmware update is involved.

This is a workload for a native profiler, not an ESP32 emulator or a full firmware
build. It does not run FreeRTOS tasks, the BLE stack, TinyUSB, the display or NVS.
Real flash latency, scheduling, peripheral waits and ESP32 CPU/cache behavior are
outside its scope. Host timings cannot be converted to board timings.

## Build and run

Use the same CMake/Ninja and compiler prerequisites as [ordinary builds](building.md#individual-builds).
The profiling preset uses `RelWithDebInfo`: optimized code with debug symbols,
without coverage, sanitizer or mutation instrumentation. The benchmark target is
opt-in and does not enter release packages or the default product graph.

Linux / WSL, from the checkout root:

```sh
cmake --preset logic-profile
cmake --build --preset logic-profile
ctest --preset logic-profile
./build/logic-profile/core/allpowers_logic_bench
```

Windows, from an x64 Developer PowerShell, uses a separate directory so that the
same checkout can also be used from WSL:

```powershell
cmake --preset logic-profile -B build/logic-profile-windows
cmake --build build/logic-profile-windows --target allpowers_logic_bench --parallel 2
ctest --test-dir build/logic-profile-windows -L '^benchmark$' --output-on-failure
.\build\logic-profile-windows\core\allpowers_logic_bench.exe
```

The executable supports `--help`, `--scenario all|telemetry|history|hid`,
`--iterations 1..100000`, `--records 1..2880`, and `--csv`. Defaults are all
scenarios, 100 iterations and 2,880 records. Each scenario can run independently.
For example, record an uninstrumented baseline in the ignored build directory:

```sh
mkdir -p build/logic-profile/reports
./build/logic-profile/core/allpowers_logic_bench --csv > build/logic-profile/reports/baseline.csv
./build/logic-profile/core/allpowers_logic_bench --scenario hid --iterations 1000
```

## Workloads and measurements

| Output row | One operation | Production code exercised |
| --- | --- | --- |
| `telemetry` | One pair of R2500 V2 Main/Settings frames | `AllpowersCore::Session`, freshness checks, `HistoryFormat::sample`; every 17th sample also submits a corrupt frame |
| `history-append` | One record appended to RAM flash | `HistoryFormat::Ring::append`, sector erasure, CRC and read-back validation |
| `history-recover` | One recovery scan of all 4,096 slots | `HistoryFormat::Ring::recover`, latest-record read and validation |
| `hid-archive` | One complete archive of `--records` records | `FrozenArchive`, `HidTelemetry::Server`, request encoding/decoding, pages, response fragmentation, `Assembly`, record validation and archive digest |

Frame generation, allocations, initial history filling and provider construction
are outside the timers. History is filled with 4,096 records before timing so
that even short runs exercise wrap and sector erasure. The timed loops include
correctness checks. Failures return a nonzero exit code even in optimized builds;
checks do not depend on `assert` or `NDEBUG`.

`--csv` writes `scenario,unit,iterations,records,operations,elapsed_ms,ns_per_operation,checksum`
to standard output. Checksums keep results observable and should match between
builds with the same options. Times vary with host load and compiler; use repeated
uninstrumented runs on the same machine when comparing changes. Very short runs
are useful for validation, not reliable timing comparisons. A full HID archive
is an in-process protocol round trip including both sender and receiver, not a
measurement of USB bandwidth or just the device-side server.

## Function and source-line profiles

On Linux / WSL, Valgrind Callgrind can collect instruction counts and call graphs
without changing the executable. Install Valgrind separately if it is absent.
Collect only the HID workload, excluding setup:

```sh
mkdir -p build/logic-profile/reports
valgrind --tool=callgrind --collect-atstart=no \
  --toggle-collect='LogicBench::runHidArchive*' \
  --callgrind-out-file=build/logic-profile/reports/hid.callgrind \
  ./build/logic-profile/core/allpowers_logic_bench --scenario hid --iterations 10
callgrind_annotate --inclusive=no --threshold=99 \
  build/logic-profile/reports/hid.callgrind > build/logic-profile/reports/hid-profile.txt
```

Use KCachegrind to explore the same file interactively. Other named entry points
are `LogicBench::runTelemetry*`, `LogicBench::runHistoryAppend*` and
`LogicBench::runHistoryRecover*`; select the matching scenario and function for
each profile. Product functions remain optimized and can be inlined, so examine
source-line attribution as well as the function list. Workload entry points are
kept out of line to allow collection boundaries.

Callgrind's default event is executed host instructions (`Ir`), not elapsed time
or ESP32 cycles. Do not compare the benchmark's wall-clock results while running
under Valgrind with its uninstrumented baseline. See the
[Callgrind manual](https://valgrind.org/docs/manual/cl-manual.html) for collection
controls and interpretation.

On Windows, the same executable and its adjacent PDB can be launched from a
native CPU profiler; use `--scenario` and increase `--iterations` to make the
selected workload long enough for sampling. Keep the PDB from the exact build
being profiled. No firmware SDK is needed for this target.

The optional `benchmark` CTest label checks a 17-record archive (including a
partial last HID page), history wrap/recovery and invalid command-line values.
These are correctness checks; there are no machine-dependent speed thresholds.

## Table CRC32 comparison — 2026-11-01

History and HID share `core/src/Crc32.h`: CRC-32/ISO-HDLC with the same
reflected polynomial, initial value and final XOR as the previous bitwise loops.
A 256-entry table is generated at compile time and occupies 1 KiB of read-only
storage in the portable backend. ESP32 builds use the ROM backend described
below. There is no runtime table initialization or allocation. The existing
`HistoryFormat::crc32` and `HidTelemetry::crc32` interfaces remain unchanged.

The following local measurements used an Intel Core Ultra 7 265K, MSVC
19.51.36260 on Windows and GCC 13.3 in WSL, with the same `RelWithDebInfo`
workload. The old executable was preserved before rebuilding. Both variants
were warmed up, then run in seven pairs of 1,000 iterations with 2,880 records;
the execution order alternated between pairs. Values below are medians from
uninstrumented runs, not timings under Callgrind.

| Operation | Windows: bitwise → table | Speedup | Linux/WSL: bitwise → table | Speedup |
| --- | --- | --- | --- | --- |
| Append one RAM history record | 384 → 67 ns | 5.73× | 352.2 → 72.4 ns | 4.86× |
| Recover 4,096 slots | 0.740 → 0.076 ms | 9.74× | 0.687 → 0.090 ms | 7.61× |
| Transfer a 2,880-record HID archive | 2.169 → 0.415 ms | 5.23× | 1.949 → 0.420 ms | 4.64× |

Telemetry does not use CRC32: its instruction count was identical in both
Callgrind profiles. Its median Windows timing changed from 20.5 to 21.3 ns
(Linux: 24.5 to 24.4 ns), a control for timing variation and code layout rather
than an improvement from the CRC change. The number of executed host instructions
fell by 75.7% for append, 86.6% for recovery and 82.3% for the HID workload.

Every workload checksum matched between both variants on both hosts. Tests also
compare the new routine against an independent copy of the original bitwise
algorithm for all one-byte values, all lengths through 512 bytes, protocol
boundaries through 8,192 bytes and unaligned inputs. Existing golden wire/archive
fixtures were retained. These results establish host-side computation speed;
physical flash, USB latency and ESP32 performance still require separate
measurements on hardware.

## On-board ROM CRC and runtime measurements — 2026-11-01

When `ESP_PLATFORM` is defined, `AllpowersCrc32::compute` calls
`esp_rom_crc32_le(0, data, size)`. The zero seed accounts for the ROM API's
internal complements and preserves CRC-32/ISO-HDLC results. This is a CPU routine
in ROM, not a general-purpose hardware CRC accelerator. PC builds retain the
portable table routine. An ordinary ESP32 image has no separate CRC lookup table;
the diagnostic image includes one explicitly for comparison.

`ALLPOWERS_FIRMWARE_PROFILING=ON` enables elapsed-time counters in the existing
HID Diagnostics response (`profile`, schema 1). Descriptors, report IDs, archive
format and maintenance interface remain unchanged. Build it in a separate Linux
directory, with separate output so it cannot replace the normal image package:

```sh
cmake --preset firmware-linux -B /tmp/allpowers-profile \
  -DALLPOWERS_FIRMWARE_PROFILING=ON \
  -DALLPOWERS_RELEASE_DIR=/tmp/allpowers-profile/dist
cmake --build /tmp/allpowers-profile --target waveshare-147 --parallel 2
```

This only builds an image. Device writing is a separate, explicit maintenance
operation subject to [identity, candidate and recovery checks](recovery.md).
The option defaults to `OFF`; the ROM backend is enabled in normal firmware too.

The optional instrumentation measures screen drawing, BLE decoding, actual
history appends, HID response preparation, message CRC, fragmentation and sending.
Loop-work timing excludes the final `delay(5)`. Counters report calls, accumulated
elapsed microseconds/cycles and a maximum since boot; nested timings must not be
summed as independent CPU load. Interrupts, blocking and task scheduling can
contribute to elapsed measurements. Phase comparisons use counter differences;
cumulative maxima are not phase maxima.

A bounded synthetic CRC comparison runs automatically once per diagnostic boot:
100 calls for each of ROM/table/bitwise and lengths 28/59/260/8192, using an
unaligned buffer. Each loop performs one call and compares the result with the
independent bitwise reference. Collect workload counters after `crc_done=true`
and require `crc_passed=true`. The diagnostic build uses 8,808 additional bytes
of static RAM in the measured build. It makes no synthetic history writes.

On the registered Waveshare 1.47 at 240 MHz, all 12 cases passed. Means over
100 calls (microseconds):

| Bytes | ROM | Portable table | Bitwise |
| --- | ---: | ---: | ---: |
| 28 | 2.247 | 6.189 | 6.705 |
| 59 | 4.372 | 9.405 | 13.762 |
| 260 | 17.706 | 24.202 | 59.857 |
| 8,192 | 547.042 | 553.354 | 2,135.554 |

Short-buffer table measurements had cache/preemption outliers: best ROM/table
times were 2.096/1.958 us at 28 bytes and 17.563/17.425 us at 260 bytes. The means
do not establish an isolated intrinsic ROM speedup. Large-buffer ROM/table
throughput was nearly equal. These are instrumented calls on the running board,
not host timings.

Three real archive reads of 2,103–2,104 records took 18.221, 18.783 and 19.516 s.
Windows viewers remained active. Counter-difference windows include the boundary
Diagnostics request and other clients/notifications: response preparation cost
17.7–20.1 ms total, outgoing message CRC 3.9–4.1 ms, fragmentation 7.1–7.6 ms,
HID send calls 1.83–2.12 s and drawing 1.05–1.16 s. Mean drawing time was about
55.2 ms; a separately observed normal history append took 2.113 ms. No artificial
history entry or flash benchmark was used.

This baseline identified servicing/transport pacing as the archive latency limit:
the old `pumpHid` sent one fragment per loop followed by a 5 ms delay, drawing
blocked the loop periodically, and other requests shared the channel.
Do not convert these counters into a claim of total CPU utilization or predict
a speedup before measuring a scheduler/transport change on the device.

## Bounded HID batches — 2026-11-01

`HidPumpBudget` now limits each service call to four report attempts and a 3 ms
elapsed-work budget, checked before each iteration. Response preparation and an
individual blocking driver's timeout may overrun the remaining budget; this is
not a hard real-time deadline. A failed send ends the batch without advancing
the fragment offset. A pending/recently completed transfer yields for 1 ms so
the host can enqueue its next page; an idle or disconnected device keeps the
previous 5 ms delay. Each iteration checks the maintenance/restart flags.

No new task, SPI clock, wire format, descriptor, archive storage or station
selection behavior was introduced. Periodic UPS/live updates still run before
the archive pump. Offline tests exercise the count/time limits, failed-send stop,
32-bit microsecond wrap and active/idle delays. Profiling diagnostics additionally
expose cumulative `hid_send_completed` and `hid_send_failed` counters.

On the same registered Waveshare, with Windows viewers active, the immediate
before-update archive of 2,125 records took 19.380 s. After updating, the first
2,134-record read took 4.026 s; three 2,136-record reads took 3.880, 4.313 and
4.322 s. Median latency improved 4.49×; record-byte throughput rose from 3,509
to 15,848 B/s (4.52× after normalizing for record count). These are local board
measurements, not a promise for all hosts or hardware.

Two independent readers then each imported 2,138 checked records concurrently
in 6.079/6.158 s. During that load, all 50 maintenance Feature probes passed:
median 0.288 ms, maximum 0.422 ms. They did not request reboot or another write.
All 2,125 initial records remained byte-for-byte identical in every checked
snapshot. Station selection and fresh BLE telemetry were retained; response,
UPS and vendor send failures and history errors were zero. Drawing remains a
periodic blocking operation and is a separate future optimization target.

## Changed display rows — 2026-11-01

`DisplayFrame` retains an optional physical wire-image shadow in PSRAM and sends
only changed rows, in synchronous stripes of at most eight rows. The panel
address window includes the existing board offsets for each stripe. SPI remains
20 MHz; panel initialization, logical layouts, rotation and RGB565 byte order
are unchanged. The shadow costs 110,080 bytes on Waveshare and 25,600 on Pocket.
If allocation fails, every frame is sent in full. The first frame is always
complete, and comparison in physical coordinates also handles page changes.

Independent host tests reconstruct the entire panel from the transmitted stripes
and compare it with a forward transform of the logical image for both boards.
They cover unchanged frames, sparse changes, clearing, rotation/page transitions,
split/partial stripes and operation without a shadow. Measured host coverage
remains 100% (core 636/636, firmware 437/437); this does not cover device drivers.

On the registered Waveshare, two 20-second background windows on dashboard page
zero each contained 20 draws. Mean draw elapsed time fell from 55.164 to
11.816 ms (4.67×). The new window transferred 351 of 6,400 possible rows,
skipping 94.5%; mean `display_present` was 7.509 ms, including 2.798 ms per
frame in nested `display_send` calls. These elapsed counters overlap and must
not be added or interpreted as whole-device CPU utilization. Full redraws still
block, and the observed cumulative draw maximum was 82.039 ms including page changes.

Three checked archive reads before the display change took 4.000/3.948/4.496 s
for 2,148 records; after, 4.378/4.358/3.858 s for 2,156 records. The user changed
display pages during the latter two runs. These samples do not establish an
archive speed improvement. Two simultaneous readers each read 2,157 records in
7.343/7.441 s; 50/50 concurrent maintenance Feature probes passed, with median
0.255 ms and maximum 0.390 ms. All initial 2,148 records remained byte-identical;
station selection and fresh GATT/UPS reporting were retained, with zero send or
history errors. The user confirmed normal image and colors on the physical
screen, including page changes. Pocket was built and host-tested, not flashed.

## Stack watermarks and repeat profile — 2026-11-01

The profiling build now samples the current task's
`uxTaskGetStackHighWaterMark(nullptr)` at most once per second for each of five
roles. ESP-IDF 5.5.5, bundled with Arduino ESP32 3.3.11, reports **bytes**, not
upstream FreeRTOS words. The value is the lowest untouched stack remainder since
task creation, including earlier calls between samples. Scanning happens outside
critical sections; only the throttle and copied results use the profile lock.
No task suspension, all-task enumeration, borrowed handles or new tasks are used.
Ordinary builds compile out the sampler. Diagnostics include the actual task
name, `min_free_bytes` and sample count in `profile.stack_minimums`.

A registered Waveshare completed 180 seconds of concurrent archive reads from
two clients, diagnostic polling and maintenance Feature probes, while receiving
normal BLE telemetry and writing history at its normal cadence. The user cycled
all three display pages with nine button presses and left page zero selected. The observer
recorded all three pages in 82 samples. Both clients completed 25 valid archives
each (108,774 records checked in total); all 2,887 maintenance probes passed,
with median 0.274 ms and maximum 1.497 ms. Including final reads, the run lasted
183.610 seconds. No restart or send/history failure was observed.

| Task / role | Configured stack, bytes | Minimum observed free, bytes |
| --- | ---: | ---: |
| `loopTask` / main loop | 8,192 | 1,723 |
| `station_ble` / BLE worker | 10,240 | 7,312 |
| `history_log` / history | 6,144 | 4,312 |
| `nimble_host` / BLE callback | 5,120 | 1,920 |
| `usbd` / USB callback | 4,096 | 3,032 |

These are diagnostic-image watermarks, including the sampler's own stack use,
not a proof of a worst-case bound. Untouched reservations can retain the fill
pattern. Unexercised reconnect/failure paths and other system/interrupt stacks
remain outside this measurement. During the observed stress window, 824 actual
scans accumulated 142.888 ms elapsed time, averaging 173.408 us per scan; these
multitask elapsed measurements do not establish CPU utilization.

After stress, a 20-second page-zero background window averaged 12.151 ms per
draw, versus 11.816 ms in the preceding display experiment and the original
55.164 ms full-frame baseline. Short stress windows with equal page numbers at
both boundaries averaged 43.915 ms on the graph (8 draws) and 14.786 ms on
diagnostics (4 draws). Graph raster work before `display_present` accounted for
27.618 ms per draw in those windows. The cumulative draw maximum, including page
changes/startup, reached 83.165 ms. Full redraws remain synchronous.

Before adding the stack sampler, three reads of 2,170 records took
3.991/3.694/4.070 s (median 3.991 s). After stress, 2,178 records took
4.542/4.225/3.812 s (median 4.225 s). The medians differ by 5.9%, the ranges
overlap, and three runs do not prove a persistent throughput regression. There
is no return to the original 18–20-second archive latency. Windows viewers
remained active; counter windows include boundary diagnostics and other traffic.

The new archive windows accumulated 2.555–2.800 s in HID send calls, versus
14.5–15.2 ms preparing responses, 3.6–3.7 ms on message CRC, 6.9–7.1 ms on
fragmentation and 46–61 ms drawing. ROM CRC for 260 bytes averaged 17.667 us,
close to the original 17.706 us. Three natural history writes in the stress
window accumulated 16.379 ms, with a lifetime single-write maximum of 12.124 ms;
this occasional spike was not a dominant archive cost. The strongest remaining
archive latency target is USB send/wait/pacing. Graph raster work and synchronous
full-page transfers are separate display latency targets.

All 2,170 initial records remained byte-for-byte identical across 54 checked
snapshots. Station selection, fresh GATT telemetry, UPS and maintenance were
retained. Both firmware targets built with profiling on/off, Linux runtime and
contracts passed, and Windows Qt/core tests and CPack/UPX passed. Pocket was not
flashed. Raw reports and exact images remain outside Git; Waveshare remains on
the diagnostic image. This bounded load is not a long soak or a suspend/unplug,
battery-transition or BLE reconnect test.

## Exact graph projection and raster work — 2026-11-01

`GraphHistory::Projection` inverts the existing rounded log-age mapping once at
display initialization, using the platform's original floating-point function.
Its 248 first-age thresholds then map integer ages by binary search. No
approximation or host-generated floating-point table changes the axis. The
cache/object padding adds about 1 KiB of ordinary static RAM.

`FrameRaster` preserves diagonal Bresenham pixels and writes clipped horizontal
and vertical spans directly. `GraphRenderer` shares both layouts' curve code
and skips only later idempotent triples of identical projected SOC/output/input
points. The first repetition still repaints because earlier crossing lines or
overlapping 3×3 dots may cover its centers. Gap markers and source overlays keep
their previous order. Colors, time axis, scaling, SPI and storage are unchanged.

Independent host tests check all integer ages 0–86,401 and out-of-range ages,
50,176 clipped line endpoint combinations, and entire canvases for both layouts:
empty/single/dense series, gaps, stale points, source changes, overlaps, markers
and full-capacity input. Host line coverage is 100% (core 636/636, firmware
542/542); hardware-only drivers are not measured by these tests.

The diagnostic image performs five paired raster comparisons after the CRC test
finishes and the graph is displayed. Each pair draws the old log/per-pixel path
and new cached/span path from the same copied points on a temporary PSRAM canvas.
Order alternates, clearing/CRC are outside the timed regions, and the canvas is
released after five pairs. This measures curves/markers/overlays, excluding
history copying, text/axes and SPI. Normal builds omit this benchmark/allocation.

On the registered Waveshare, five pairs of 2,332–2,335 identical input points
averaged **13.534 ms old / 8.502 ms new** (**1.59×**). All canvas CRCs agreed.
After the benchmark stopped, ordinary graph refresh averaged 45.783 ms over 31
draws; before updating, 30 draws averaged 59.036 ms. The live-ring size changed
after reboot (1,055 versus 172 second samples, with 2,191 versus 2,200 persistent
records), so the observed 1.29× full-refresh ratio is not an isolated algorithm
speedup. Windows viewers, BLE and history remained active. The user confirmed
normal lines, colors and labels on the physical screen.

The new refresh window averaged 9.036 ms copying/assigning ages, 8.483 ms drawing
curves and 22.209 ms in `display_present`, including panel writes. Transfer and
the history snapshot are the next latency candidates. Single archives during
the graph checks took 4.401 s before / 4.684 s after; these samples establish no
archive speedup. Two simultaneous readers then imported 2,201 valid records each
in 7.580/7.481 s. All 50 concurrent maintenance probes passed (maximum 0.425 ms).
All 2,191 initial records remained byte-identical in four new snapshots. Saved
station, fresh GATT/UPS and maintenance survived; send/history failures were zero.
Main-loop stack watermark remained 1,723 bytes.

Both boards built/relinked with profiling on/off; Linux runtime/contracts,
Windows Qt/core and CPack/UPX passed. Pocket was host-tested but not flashed.
Private data/images remain external and Waveshare stays on the diagnostic image.
No long soak, suspend/unplug or battery/reconnect failure test was performed.

## Dirty rectangles and SPI windows — 2026-11-01

`DisplayTiles` replaces full-width changed rows with bounding rectangles within
16×8 physical tiles. Adjacent rectangles merge only when the extra RGB565 pixels
cost no more than the saved 11-byte CASET/RASET/RAMWR window (at most five extra
pixels). A per-stripe full-row alternative handles dense separated-row changes
when its wire cost is lower. First frames and absent shadows still send complete
frames. Rotation reads short consecutive logical runs while transposing an
eight-row stripe, instead of repeatedly revisiting widely separated PSRAM rows.

Each packed rectangle uses one selected SPI transaction for the address window
and pixels, at the unchanged 20 MHz. Both controllers describe consecutive bytes
with CS held low and command/data selected by D/C: [ST7789V3 section 8.3.2](https://files.waveshare.com/wiki/common/ST7789V3_V0.1.pdf),
[ST7735S section 9.4.1](https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf).
Panel initialization, offsets, RGB565 order, rotation, backlight and pins remain
unchanged. There is no DMA or new task. Packed pixel/address arrays are 32-bit
aligned; the SPI driver's rounded last word stays inside the stripe allocation.

The old row encoder remains as the profiling reference. Five paired tests copy
the same old physical shadow and use the same immutable new framebuffer for
both paths, alternating order. Both perform real SPI writes. Shadow setup and
CRC are outside the timers. The temporary shadow is freed after five pairs.
The normal presentation still runs afterward; collect ordinary refresh windows
only after the paired tests stop. CRC comparison checks the encoded shadows,
not LCD RAM readback. Host tests independently reconstruct every emitted patch
and verify translated CASET/RASET/RAMWR coordinates, clipping, both orientations
and byte orders, partial edge tiles, sparse/merged/dense changes and absent
shadow behavior. Ordinary firmware has no paired test or temporary allocation.

On Waveshare, five pairs averaged **18.652 ms old / 7.659 ms new** for encoding,
comparison, window setup and actual SPI transfer (**2.44×**). All resulting shadow
CRCs matched. Pixel payload fell from 68,284 to 2,784 pixels across the five pairs
(95.92% less); counting the windows as well, SPI bytes fell 95.10%. The user
confirmed normal colors, lines and image updates on the physical display.

Ordinary graph windows contained 31 frames each: `display_present` averaged
19.413 ms before / 7.867 ms after, and nested send calls accumulated 13.830 /
1.306 ms per frame. Full draws averaged 48.265 / 31.152 ms. The live ring differed
after reboot (810 versus 90 second samples; persistent records 2,211 versus
2,223), so these full-window ratios are contextual, not an isolated speedup.
The paired comparison is the same-frame result. The new window averaged
8.856 ms copying/assigning ages and 8.344 ms rasterizing curves; those are now
comparable to the complete display preparation/transfer path. Display statistics
add `display_pixels` and `display_rectangles`; `display_rows` counts distinct
physical rows touched per frame, even for partial-width patches, and is no
longer a pixel-bandwidth proxy. Profiling diagnostics use the existing 8 KiB
message capacity to leave room for counters and retained disconnect rows;
ordinary builds retain the previous 6 KiB local diagnostic buffer.

All 2,211 initial records survived byte-identically in four checked new snapshots.
Two simultaneous readers validated 2,224 records each in 7.443/7.522 s. All 50
concurrent maintenance probes passed (maximum 0.798 ms); fresh Central/GATT and
UPS remained active with zero send/history errors. Main-loop minimum free stack
was 1,556 bytes, compared with 1,723 in the preceding diagnostic image. No
overflow or reset was observed; these are measured watermarks, not worst-case
proofs. SPI-only data has no acknowledgement or LCD RAM readback here.

Both firmware targets built/relinked on Linux with profiling on/off; runtime and
contract checks, Windows Qt/core and CPack/UPX passed. Host line coverage remained
100% (core 636/636, firmware 648/648). Pocket was host-tested and built, not flashed.
Waveshare remains on the optimized diagnostic image. Exact packages, raw device
reports and identities are external. Five graph-frame pairs do not establish
universal speedups for all images; no long soak, suspend/unplug or battery/BLE
failure-path test was performed in this iteration.

## History snapshot and diagnostic page control — 2026-11-01

`GraphSnapshot::build` traverses the live and persistent rings backward, filters
overlapping minute records and computes ages, joins and unknown-time boundaries
in the same pass. A pending point stays in internal RAM until its predecessor is
known; each completed point is written to PSRAM once. The returned view occupies
the tail of the existing allocation, avoiding a counting pass or final move.
The owning allocation remains unchanged. Ordinary builds add no scratch buffer
or cache; persistent records, history format and retention remain unchanged.

The fused builder runs under the existing history data mutex, including age work
that previously ran after releasing it. The 20 ms acquisition limit is unchanged.
The new `graph_copy_locked` timer measures this region; the old image had no
equivalent lock timer, so a shorter lock hold has not been demonstrated. The
timestamp is captured under the lock before the build, rather than after the
previous copy/release. This shifts the sampling instant by a few milliseconds,
without changing the age algorithm or inventing time across unknown boundaries.

After the other paired benchmarks finish, profiling builds freeze occupied ring
runs once and compare the retained reference copy/age path with the fused builder
five times, alternating order and keeping one timestamp. A temporary 466,560-byte
PSRAM block contains the frozen records and output; it is freed after five pairs.
Allocation, freezing and digesting are outside builder timers. Digests cover all
record bytes, ages and boolean flags, excluding point padding; count and global
unknown-time status also agree. Independent host tests compare every field with
an oracle based on the previous algorithm, including wrapped/full rings,
noncontiguous overlaps, clock reversal, session changes, gaps, age saturation,
invalid inputs and buffer sentinels.

Five paired measurements on Waveshare, using **2,314 points** from 2,249 archive
and 68 live records, averaged **8.7946 ms reference / 6.7790 ms fused** (**1.30×**,
22.9% less elapsed time). All comparisons passed. Ordinary graph windows measured
12.372 / 7.113 ms for snapshot preparation and 37.944 / 29.050 ms for full draws.
These windows had different live-ring sizes after reboot (964 / 104 records),
so their ratios do not isolate the algorithm. The new window's mutex region
averaged 7.099 ms, with a lifetime maximum of 7.136 ms. Curve rendering averaged
8.425 ms and presentation 7.522 ms; the three costs remain comparable. Timers
measure elapsed time, including scheduling, rather than CPU utilization.

Profiling images also accept feature report 1 with the 12 ASCII bytes
`PAGE-R2500!!` followed by a zero-based page byte. `profile.page_control` advertises
this capability. The callback validates the report and queues an atomic request;
the main loop applies it only with the station picker closed and no pending
boot/restart. It does not synthesize BOOT presses or change the station. The
ordinary images contain no page handler or marker. Existing maintenance update
reports, USB descriptors and partitions are unchanged. USB tests verified pages
0 → 1 → 2 → 0 and later 2 → 0, with unchanged button count, station and boot
session. The final screen is the main page (0).

All 2,238 original archive records remained byte-identical in four new snapshots.
Two simultaneous readers validated 2,252 records each in 7.400 / 7.302 s; all 50
concurrent maintenance probes passed (maximum 0.423 ms). Single archives took
4.590 s before / 4.030 s after; these isolated samples establish no throughput
speedup. Fresh Central/GATT telemetry, UPS and maintenance remained available,
with zero send/history errors. Minimum free stack was 1,556 bytes in `loopTask`,
7,416 in the BLE worker, 4,320 in history, 1,928 in NimBLE and 3,060 in USB.

Both targets built/relinked on Linux with profiling on/off; runtime/contracts,
Windows Qt (23/23), core/contracts (8/8) and CPack/UPX passed. Host line coverage
was 100% (core 636/636, firmware 738/738). Both ordinary binaries were checked
for absence of the page marker. Pocket was built and host-tested, not flashed.
The registered Waveshare stays on the diagnostic image; exact packages and raw
reports are external. This iteration did not repeat physical recovery, perform
LCD RAM readback or test long-running/suspend/battery/BLE failure scenarios.

## Reusing exact graph coordinates — 2026-11-01

The renderer now reuses a trace's integer height while its raw value repeats
within one draw; the maximum and layout are fixed for that call. A per-draw
`Projection::Cursor` caches the half-open integer-age interval of the last
column, falling back to the same binary search outside it. Neither cache assumes
continuous samples or ordered ages. Primitive order, gap markers, overlapping
traces and the final source-transition overlay pass remain unchanged. The cache
is local to the draw, with no persistent buffer or new allocation.

The diagnostic graph benchmark now compares the preceding cached-projection /
span-raster renderer against coordinate reuse. Both use identical copied points,
the same projection thresholds, coalescing and raster functions. This replaces
the older log/per-pixel benchmark reference described earlier. Five alternating
pairs on Waveshare, containing 2,278–2,282 points, averaged **8.3132 ms reference /
7.5258 ms new** (**1.105×**, 9.47% less elapsed time). All canvas CRCs agreed.
Clearing and CRC are outside the timers. The host oracle still checks against
the original independent renderer; cursor tests exhaust all ages 0–86,401 in
ascending, descending and shuffled orders, including repeated queries, fallback
before initialization and out-of-range ages.

Ordinary graph windows averaged 10.120 / 7.482 ms in curve rendering and
32.539 / 28.367 ms for complete draws. The live ring changed from 583 to 63
records after reboot, so these ratios do not isolate the optimization. The new
window averaged 7.039 ms for snapshot preparation and 7.875 ms for presentation.
Those costs and raster work are now similar; USB send/wait remains the much
larger archive latency cost than CRC or response preparation.

All 2,257 initial archive records remained byte-identical in the four checked
new snapshots. Two concurrent clients validated 2,263 records each in
7.082 / 7.180 s; all 50 maintenance probes passed (maximum 0.434 ms). Fresh
Central/GATT, UPS and history remained active, with zero send/history errors.
Pages were selected through diagnostic USB control and the main screen restored.
Raw measurements and exact packages remain external. Pocket is built/tested
separately; these timing figures are from Waveshare only.
