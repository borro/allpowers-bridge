# Validation

[Documentation](README.md)

## Static analysis rollout — 2026-10-28

Local validation on Windows 11 / MSVC 19.51 and WSL Ubuntu, with the existing
static Qt SDK. These are local measurements; the new GitHub jobs have not run yet.

| Check | Result |
| --- | --- |
| clang-tidy 18.1.3 | 6/6 host translation units, no product findings; repeated runs about 2–3 s |
| Cppcheck 2.22.0 | 6/6 units, no product findings; repeated runs about 3–5 s |
| MSVC `/analyze` | 26/26 product units, no findings; separate SARIF per target/unit |
| actionlint 1.7.12 + ShellCheck 0.11.0 | All workflows passed; about 0.2 s excluding installation and controls |
| Analyzer controls | Passing C++, null dereference, invalid workflow dependency and unsafe Bash substitution; missing tools/reports, stale source hashes, truncated SARIF, rule changes and parallel report writes |
| Maintenance tooling | 54 tests passed, including CI policy and bounded failure summaries |
| Runtime/build contracts | Linux native 5/5; Windows root 25/25, including the nested 16-test desktop suite and identity rebuilds |
| Firmware and packaging | Both boards built/relinked offline; Windows CPack ZIP and both packed EXE smoke checks passed |
| Mutation regression | 771 mutations; 56 reviewed survivors, no unreviewed survivors/uncovered mutations or inactive acceptance entries |
| Dependency caches | Analyzer version changes left Qt/Arduino/WDK fingerprints unchanged |

One paired local Windows product rebuild with the same ready Qt SDK took 56.27 s
with analysis OFF and 125.54 s with it ON (about 69 s additional). This includes
compilation/linking and nested product builds, not a second Qt build. It is one
local sample, not a hosted-runner estimate. CI records the combined Windows build
duration; first-time Cppcheck installation is also outside the per-analyzer times.

The first pass prompted explicit Windows module/process-handle checks, guarded
service-handle use and default initialization of graph sample records. Windows
failure paths have regression tests with fake platform calls. Five Cppcheck
cross-caller bounds false positives have local, rule-specific explanations;
there is no broad static-analysis baseline. Existing mutation acceptance was
remapped only to identical code lines after comments moved their locations.
Neither the firmware builds nor tests accessed hardware.

## Client-restart operation collision follow-up: R3 — 2026-10-25

Baseline and fetched remote branch: `17794dc4eff416e162152fcb3020c93a55be2424`,
`codex/review-followup`. Earlier R1/dispatcher fixes remain in place. The missing
case was independent delivery in the two serial directions: a fresh client
reused request 1 while an old begin/complete from the same board boot and archive
generation was still queued. Old begin followed by new begin caused an error;
an old complete footer could instead falsely complete the old snapshot.

Before the production change, `restartedClientDoesNotAcceptBufferedPreviousOperation`
compiled and failed functionally in all six initial FIFO traces. They included
both the wrong-begin error and stale `complete`, not a compiler/API failure. The
other 14 archive tests still passed. The source records had valid magic/CRC and
the board session/generation deliberately stayed unchanged.

The fix adds `history_nonce_bits:128` to v3 capability negotiation and echoes a
per-operation `operation_nonce` in every archive frame. The desktop compares it
before interpreting metadata or errors. Windows CNG supplies 16 fresh bytes per
operation, including after an executable restart; errors fail before sending an
export. Numeric-only v3 compatibility uses a fresh random nonzero 32-bit request.
The exact probability assumptions, weaker fallback and collision behavior are
documented in [USB operation identity](usb-monitor.md#operation-identity-across-client-restarts).
No AHP1/AHP2 storage, partition, rollback or physical maintenance format changed.

| Regression / production path | Local observation |
| --- | --- |
| `restartedClientDoesNotAcceptBufferedPreviousOperation` | 13 deterministic queue traces: new real UsbTransport, 500 ms closed channel, fresh actual STATUS, old bytes admitted before dispatching the new command, numeric request 1 in both client instances; strong tokens A/B are deliberately injected, not lucky random IDs |
| Same test, changed snapshot and identical-archive controls | FIFO host→device and device→host queues advance independently; old begin/records/complete or abandoned prefix are delivered before the new operation. Exact new tick/SOC set and no completion before the new snapshot closes prevent a false pass on equal counts/data |
| Pre-update numeric-v3 and empty-archive rows | Actual numeric commands and extended commands use OutputController::command and the production exporter/serializer; no fabricated JSON, direct writer.start, reordered bytes or already-delivered frame replay in these traces |
| `rapidRestartsReleaseEveryReplacedSnapshot` | Multiple restarts/replacements retain at most one snapshot and release every replaced/final snapshot |
| `realOpenFailureFooterIsCorrelatedBeforeMetadata` | Real exporter open-error footers with zero session/generation: foreign strong/numeric errors do not abort the current operation; own errors remain errors |
| `strongNonceRequirementSurvivesStatusChanges` | Removed/unsupported capability in later STATUS cannot downgrade an active nonce operation |
| `matchingNonceCorruptionRemainsStrict` | Matching-operation wrong begin metadata, record CRC and footer count/digest/error retain strict failure results |
| `history-operation-process` | Separate QtCore+bcrypt-only executables invoke the same production CNG helper; 32-hex shape, distinct first values and fallback little-endian/nonzero contracts pass. This tests real process restarts without claiming mathematical uniqueness |
| Native `HistoryOperationTests` in `firmware-host` | Shared real CDC collector discards overflow/NUL through newline; malformed tokens cannot cancel an active snapshot; numeric response bytes remain unchanged; extended begin/record/error/footer echo and buffer bounds pass |

The updated archive suite passed 40/40 and monitor suite 61/61 locally. These are
host/fake-clock/channel results. The single-writer firmware cannot physically
emit an old operation after a replacement begin: bytes accepted before replacement
precede it in FIFO order and replacement closes the old snapshot. Retained
post-begin stale-frame parser tests therefore remain explicitly adversarial
replay tests, separate from the new physically ordered restart regressions.

Two process-probe outputs being different does not prove all future values
unique. With healthy uniform CNG, the per-stale-value bound is `1/2^128` in nonce
mode and `1/(2^32-1)` in numeric fallback. Exact full-identity collisions remain
indistinguishable; no absolute guarantee or authentication is claimed.

The working tree based on `17794dc`, built as `0.5.2-9-g17794dc-dirty`, passed:

| Environment / offline gate | Actual result |
| --- | --- |
| Windows 11, MSVC 19.51.36260, static Qt 6.12, /MT, WinRT | Products build; root CTest 20/20 in 337.61 s; nested app CTest 8/8 in 38.26 s, including 40 archive cases and 61 monitor cases |
| Windows identity graph | Root and standalone CLI, PE VERSIONINFO and isolated EXEs pass clean → source-only dirty → clean revert without manual configure; wrong expected identity rejected |
| Ubuntu WSL, GCC 13.3 | Full native CTest 14/14 in 6.78 s |
| Ubuntu WSL, fatal ASan/UBSan | Full CTest 15/15 in 7.30 s, including sanitizer-policy and firmware-host; ASAN/UBSAN/LSAN option variables unset |
| Standard-library maintenance | 11/11; no device discovery or USB backend dependencies |
| Both firmware targets, Windows and Linux | Actual compilation and offline relink passed; Linux strict image validation, artifact hashes and all 36 current firmware/core source hashes matched |
| Windows packaging | Driver/INF/catalog build checks; CPack runtime/source ZIP and SHA256; extracted source kit without .git rebuilt and passed native 16/16 and supported desktop 6/6, including archive-transport and history-operation-process |
| Publication checks | Staged index, worktree and reachable-history audits, gitleaks, actionlint and local documentation link targets passed |

The ordinary Windows CTest and extracted-source-kit check both run the new
regression. CI uses these same gates; portable firmware-host covers the command
collector and extended serializer in normal and fatal-sanitizer jobs.

Implementation and regression commit:
[`a6beb6bc580d40021e77d6227401a108c126c96a`](https://github.com/borro/allpowers-bridge/commit/a6beb6bc580d40021e77d6227401a108c126c96a).
[Hosted run 40227626329](https://github.com/borro/allpowers-bridge/actions/runs/40227626329)
completed successfully on 2026-10-25: audit, maintenance, normal/fatal-sanitizer
portable tests and both Linux/Windows products all passed. Release publication
was skipped on this working branch.

The Windows hosted log confirms root CTest 20/20 in 750.13 s, nested app CTest
8/8 in 36.61 s, archive-transport 40/40 in 0.84 s and the independent-process
CNG test in 0.07 s. The identity graph took 651.06 s of the root total. Packaging
also rebuilt the extracted source kit without .git: native 16/16 in 56.98 s and
supported desktop 6/6 in 32.05 s, including archive-transport and the process
test again. These are hosted/offline results for that exact implementation
commit, separate from the local observations above.

No image activation, live BLE/USB exchange, service/driver installation or kernel
runtime experiment was performed for this follow-up. Prior hardware evidence and
hosted runs below retain their historical scope.

## Serial output lifecycle follow-up: R1/R3 — 2026-10-25

Baseline: `9e043226d5c2f7694e20665e49c2211390506b6e` on
`codex/review-followup`. Its successful CI did not exercise a full legacy dump
against the released transport watchdog, or send an early retry through the
firmware command dispatcher. The earlier direct `ExportSession::start` retry
test bypassed the active-export guard in that dispatcher.

An offline MSVC probe of the exact baseline exporter, using 2880 mixed AHP1/AHP2
records, a fully accepting sink and the production 5 ms pump cadence, sent only
600 records by 6005 ms and finished at 28810 ms. The production dispatcher
extraction retaining the original behavior emitted no STATUS during the dump
and no begin for an early replacement request. The frozen `b989bd1` transport
then reproduced a first watchdog close at 6500 ms and zero imported records.
The current desktop's early-error/retry test sent request 2 through the real
dispatcher while request 1 was still active; no second snapshot opened and the
retry remained in `reading`. Both new integration regressions failed with that
baseline behavior. These are host/fake-channel observations, not a hardware
test. The previous CI and hardware journals below retain their original scope;
they do not establish these missing lifecycle properties.

The production fix uses `OutputController::command` in the actual `.ino`, a
single line-aware byte-stream owner and shared `StatusWire.h` serializers.
Implementation and regressions: [5ba366ae4771995d470381248fb9a763002509d8](https://github.com/borro/allpowers-bridge/commit/5ba366ae4771995d470381248fb9a763002509d8).
STATUS continues during legacy and v3 exports; a busy history-status lock omits
inventory/time-anchor claims instead of fabricating unavailable storage. New
read commands replace the active export and release its snapshot. Framing
barriers do not acknowledge records. Valid foreign v3 transactions are ignored;
the current transaction still requires its complete count/cursor/digest footer.

| Host/fake-channel regression | Observed result |
| --- | --- |
| `releasedTransportReceivesFullMixedArchiveWithWatchdog` | Exact frozen `b989bd1` transport/parser/watchdog imports all 2880 mixed records: 31.295 s, zero port closes, maximum accepted STATUS gap 1.025 s |
| Same test, short writes and a 2 s stall | 2880/2880 imported in 33.735 s, zero closes, maximum STATUS gap 2.680 s; complete JSON lines |
| `currentV3ReceivesFullMixedArchiveThroughProductionScheduler` | Complete original 2880-record frozen set despite live append/retention and history-status Busy; 31.300 s normally, 49.355 s with short writes/stall |
| `earlyDesktopErrorCanReplaceActiveFirmwareRequest` | Real current desktop rejects request 1 metadata, immediately sends request 2 through the production dispatcher while request 1 has a partial row; complete in 1.660 s, two snapshots opened sequentially |
| `repeatedReplacementIgnoresCapturedOldFramesBeforeAuthoritativeBegin`, `staleFramesAfterAuthoritativeBeginDoNotDamageCurrentImport`, `previousBootFramesWithRecycledRequestAreIgnoredAfterBegin` | Rapid replacements and attributable old request/session begin/record/done frames do not complete or damage the current operation; at most one snapshot active, all released |
| `stalledOutputReleasesSnapshotAndRetryWorks`, native `OutputTests::selfTest` in `firmware-host` | Blocked ordinary output/barrier pauses within its deadline, releases the archive and preserves framing; explicit retry recovers; STATUS/barrier never advances ACK |
| `reopenedTransportGetsStatusAndFreshOperation` | Same-object reopen and a new transport reusing request 1 obtain STATUS and import the newly changed archive, not the old snapshot |
| `frozenParserAndLifecycleMatchHistoricalSource` | Reverse only documented namespace/include/clock-access adaptations and reproduce all four original source SHA256 values |

The new Windows Qt integration suite passed 14 cases with no skips using fake
channels and clocks. These elapsed values are simulated device-loop time, not
wall-clock USB measurements. No new image was activated and no live BLE/USB,
service/driver installation or kernel-runtime test was performed.

The complete local working tree based on `9e04322`, identified as
`0.5.2-6-g9e04322-dirty`, also passed the following gates:

| Environment / standard gate | Result |
| --- | --- |
| Windows 11, MSVC 19.51.36260, static Qt 6.12, /MT, WinRT | Integrated products build and root CTest 20/20; standalone app CTest 7/7, including archive-transport, native UI checks and isolated EXEs |
| Ubuntu WSL, GCC 13.3 | Both actual firmware builds and relinks; full native CTest 14/14 |
| Ubuntu WSL, fatal ASan/UBSan | Full CTest 15/15, including the production output controller in firmware-host; no ASAN/UBSAN/LSAN suppression variables |
| Stdlib maintenance, Windows and Linux | 11/11; no device discovery or backend dependencies |
| Windows driver and packaging | Driver/INF/catalog checks and CPack runtime/source ZIP with SHA256; extracted source kit without .git rebuilt and passed native 16/16 and supported desktop 5/5, including archive-transport |
| Strict offline firmware package validation | Both Linux packages accepted; artifact hashes and all 35 source hashes matched the tested working tree; history reader [1,2], writer 2 and 32-byte records unchanged |

CI coverage uses the ordinary Windows app CTest and extracted-source-kit check
for `archive-transport`; portable `OutputTests::selfTest` runs in both normal
and fatal sanitizer `firmware-host` jobs. The earlier hosted runs below are
historical evidence for their named commits, not executions of these new tests.

### Hosted Qt hang and diagnostic bounds

[Run 40216333239](https://github.com/borro/allpowers-bridge/actions/runs/40216333239)
for `ba7cffd7321863b77dffb01ca2f4dc097c293b56` reached an
`archive-transport` timeout after 1500 seconds inside `windows-unit`; the other
six nested Qt/EXE tests passed. The run was then cancelled. It is not successful
CI evidence. Its text-file-only Qt logger did not reveal the stalled method.

The new test target had missed the existing explicit static-plugin policy and
automatically imported the Windows NLM, Schannel and ODBC plugins. It now uses
the same `NO_DEFAULT` allowlist as the other Qt targets. The generated link edge
was checked to contain only the four selected platform/image/SQLite plugin
initializers, and the archive suite passed locally again with all 14 cases.
The old log does not prove which plugin or method caused the timeout.

CI now streams verbose outer/nested CTest output. The archive suite additionally
flushes stderr markers before QCoreApplication and at every case boundary;
JUnit reports are retained on failure or cancellation. Its timeout is 60 seconds,
the other six app suites have 120-second limits, and the parent has a 900-second
limit. Fail-fast returns failure immediately instead of spending time on later
integration rebuilds. No dataset or assertion was removed or relaxed. The seven
app suites passed locally again in 34.86 seconds with these bounds.

## Follow-up review: R1–R8 — 2026-10-25

Baseline: `b989bd1bc970ce9e5af39d5838413a204b20f64d`, including fixes in
`82bde93be836607f5d7b9c2fdb68689d71d1e9ed` after the original review of
`79b017332a11f78be632e33885d91c2ea0d66f0a`. The follow-up uses working branch
`codex/review-followup`. All new checks are offline or hosted CI: no physical
flashing, live BLE/USB, service/driver installation or security-setting changes.
Historical hardware observations below do not validate these new images.

### R1–R8 implementation and regression matrix

| ID | Production change | Regression / observable result | Limit |
| --- | --- | --- | --- |
| R1 | `HistoryWire.h` legacy projection plus `OutputController.h`/`StatusWire.h` periodic real STATUS and separate versioned export | `usbLegacyProjectionAcceptedByFrozenOldParser`: AHP1-only, mixed and AHP2-only, unchanged flash; `releasedTransportReceivesFullMixedArchiveWithWatchdog`: actual released transport imports 2880 mixed records over >6 s with short writes/stall and no disconnect | Old wire format cannot carry known/model; unknown-output projections clear Fresh and old graphs may omit them |
| R2 | `HistoryCompatibility.h`, build/package metadata, `history_contract`/`rollback_check` before ROM access | `history-rollback`: original Ring demonstrates old erase; compatible rollback preserves all records through upgrade/write/upgrade. Stdlib maintenance tests bind app/hash/manifest/marker and reject unsafe combinations before device access | Supported rollback keeps an AHP1/AHP2 reader/writer; AHP1-only downgrade is refused |
| R3 | `HistoryExport.h` frozen snapshot/typed reads/ACK and `OutputController::command` latest-request replacement; one output owner, bounded ordinary writes/barriers; strict current-transaction footer and foreign-frame filtering | `earlyDesktopErrorCanReplaceActiveFirmwareRequest`, rapid replacement/reopen/stale-frame tests and native `OutputTests::selfTest`, plus retained export/parser fault tests: real dispatcher retry, partial STATUS/archive rows, Busy, timeout, count/cursor/digest, append/retention | Complete means all valid wire records received; unusable dates are counted separately. v2 completion stays partial; prolonged blocked writes require a new explicit STATUS/read after bounded pause |
| R4 | `UsbTransport::nextLegacyPage`, bounded 5888-position search, frozen inventory/final recheck | `usbLegacyPaginationRecoversRetainedRecords`, bounds/inventory tests: `{1,3…2881}`, `{1,3}`, wrap, three pages including RAM span beyond physical slots, deduplication, changed inventory | Legacy mutation/unconfirmed count/deadline yields partial; max 2880 positions/request, 2880 retained records, 120 s total |
| R5 | Sorted relative generated firmware inventory is a real command dependency; staging mirror retained | `firmware-graph`: ordinary build of both targets with all byproducts present; unchanged/add/rename/deletion-only while identity stays dirty; source kit without .git and no-op | Only supported inventory/staging directories participate |
| R6 | Isolated EXE tests read authoritative generated identity at execution | `windows-identity-graph`: actual root/standalone Monitor+Widget CLI and PE versions, clean → dirty → revert without configure; wrong expected identity fails; tag/INF and package guards retained | Windows MSVC/Qt integration test |
| R7 | `ALLPOWERS_SANITIZERS` adds fatal UBSan and ASan | `sanitizer-policy`: signed-overflow control diagnoses UB and exits nonzero with production flags; normal native suites pass; recoverable contrast exits zero | GCC/Clang instrumentation; no suppressions or forced LSan disable |
| R8 | Separate stdlib-only maintenance CI job gates product builds/publication on branch push, PR and tag | `python3 -I -S -B -m unittest discover -s tests/maintenance -v`: hash-consistent malformed images, all segment bounds, full partition schema/MD5, compatibility and pre-device refusal | Independent of Python-free CMake product build; no USB backend installed |

### Original F01–F16 traceability

| ID | Production change retained / completed | Confirming check |
| --- | --- | --- |
| F01 | Shared decoder/reducer sets and clears MainsSource truthfully | `ups-state`: fixture bytes → C ABI → mapper/cache; mains/battery/unknown/stale/reset/missing groups |
| F02 | Full partition schema/checksum and sector-safe write plan | `partitions` plus stdlib image/rollback tests; invalid packages rejected before ROM access |
| F03 | Real-record export and multi-page legacy import | R3/R4 tests recover interrupted-write gaps and compare the final record set |
| F04 | Explicit reconnect target admitted to a full BLE catalog | `firmware-host`, `StationPicker::selfTest` saturation cases |
| F05 | Full usable telemetry controls recovery rather than any valid packet | Real `BleTransport`/channel/fake-clock test: healthy → either group missing → freshness expiry → fail/retry/reconnect; no first snapshot and GATT deadline |
| F06 | One running history sample plus one coalesced latest sample | Real WidgetController/QThread/SQLite tests with held worker and 1000 submissions, station/session change, invalid DB and controlled shutdown |
| F07 | Firmware snapshots/atomic flags and synchronized history publication | Source synchronization audit; both board compilers and portable firmware tests; hardware task interleavings not exercised |
| F08 | Exact staging mirror plus membership dependency | `firmware-staging` and R5 real `firmware-graph` |
| F09 | Build-time identity includes tracked/untracked dirtiness and checks parent commit | `build-identity` and R6 graph: current header/CLI/PE/CTest expectation through source-only changes |
| F10 | Selected root tag/commit forwarded to desktop and driver | `build-identity`, release tag tests and driver INF checks with competing tags |
| F11 | Physical AHP2 carries CRC-protected known/model; AHP1 read conservatively | Actual decode → HistorySample → Ring → serializer → USB parser for Old10/Legacy16/R2500, unknown/OFF/ON; R1 legacy limits and R2 rollback policy |
| F12 | Direct verification acquires lease and rejects installed service before transport creation | Real CLI parser BUSY cases; fake service and held test lease produce zero constructed transports/discovery/connect |
| F13 | Invalid demo/self-test temporary directory fails before opening user data | Actual subprocess demo/self-test isolation failure test |
| F14 | Session activity counters survive stop/transport changes and reach the timed report | Real `--test-seconds` parser/finish with fake transport; stopped connection still reports Bluetooth activity |
| F15 | Shared current-power formatter checks fresh/known | `firmware-host`, `telemetryLabelsSelfTest` plus both display builds |
| F16 | Atomic QSaveFile export checks final flush and commit | Same existing good destination survives injected flush failure and actual Windows commit sharing-denial; probe confirms open/write succeeded |

The additional F05/F06/F12/F14/F16 tests strengthen evidence for retained fixes;
they are not claimed as newly reproduced baseline defects. The new production
seams inject clocks/channels/workers/transactions into the real paths. They do
not enable native I/O in automated tests. Console service-mode refusal (1063)
and embedded-driver extraction inspect offline behavior/files; they do not
install or start a Windows service or kernel driver.

### Follow-up evidence

Local source validation covered the working tree based on `b989bd1` through
the retry-framing follow-up based on `5884b23` (`0.5.2-4-g5884b23-dirty`).
The complete implementation, including retry framing, is confirmed by the
successful hosted run for `b944a304f1007f58a6588825b986230ac809b890` below.
This evidence update changes documentation only; the old baseline's hosted
result is listed separately.

| Environment / check | Result | Scope |
| --- | --- | --- |
| Windows 11, MSVC 19.51.36260, Qt 6.12 static, /MT, WinRT | Root CTest 20/20 passed again at clean `b944a304f1007f58a6588825b986230ac809b890` | Both firmware builds/relinks; real build graphs, core/ABI/UPS, portable firmware, rollback, Qt, isolated EXEs, driver INF/catalog/build checks |
| Windows extracted source kit, without .git/Python product build | Native 16/16 and supported desktop 4/4 passed; CPack runtime/source ZIP + SHA256 passed | Rebuilt again after retry framing; actual source archive includes invalid/empty inventory and exporter/parser restart tests |
| Ubuntu WSL, GCC 13.3 | Full native CTest 14/14 passed after retry framing | Both actual board compiles, USB/partition/maintenance checks and object-kit relink; strict offline image validation and all source/artifact hashes matched |
| Ubuntu WSL, fatal ASan/UBSan | Full CTest 15/15 passed after retry framing | No ASAN/UBSAN/LSAN suppression variables; recoverable control exits zero, production-policy UB control diagnoses and exits nonzero |
| Python standard library, Windows and Linux | 11/11 passed | No USB/serial/esptool packages imported; synthetic malformed images and supported/unsupported rollback before device boundary |
| Workflow / publication source audit | Actionlint, source inventory, local doc links and diff checks passed | No private hardware identities, images, reports or absolute user paths in public files |
| Hosted follow-up CI, `b944a304f1007f58a6588825b986230ac809b890` | [Run 40206524419 succeeded](https://github.com/borro/allpowers-bridge/actions/runs/40206524419), attempt 1, completed 2026-10-25 | All six checking jobs passed: audit, stdlib maintenance, portable normal/fatal sanitizer, Linux products and Windows products including CPack/source-kit rebuild. Release job skipped for this branch push; no release published |
| Historical baseline hosted CI, `b989bd1bc970ce9e5af39d5838413a204b20f64d` | [main run 40199484506 succeeded](https://github.com/borro/allpowers-bridge/actions/runs/40199484506) | Baseline only; not evidence for R1–R8 |

Kernel runtime, live station transitions and the next physical write of these
new images remain separate, unperformed stages. The archive gate requires a
compatible complete rollback package; it deliberately does not provide an
AHP1-only downgrade/migration path.

## Review-fix evidence — 2026-10-25

The review fixes were validated before committing, in a working tree based on
`79b0173`, identified as `0.5.1-3-g79b0173-dirty`. The offline results below
record that source validation; later tagged builds and deployment are separate checks.
Hardware and hosted CI rows describe earlier evidence, not a rerun of the changes.
Local build/test evidence remains in ignored storage and is not distributed in Git.

| Component | Revision / date | Environment and result | Evidence / remaining limit |
| --- | --- | --- | --- |
| Core, C ABI, UPS mapper, firmware RAM logic, build regressions | Review working tree / 2026-10-25 | Linux GCC 13.3: CTest 12/12; Windows MSVC 19.51: CTest 14/14 passed | Local `/tmp/allpowers-review-integrated-native` and `build/review-integrated-native`; real fixture bytes through C ABI to UPS cache, gaps/known metadata and staging/version/partition cases; no hardware |
| Memory/undefined-behavior checks | Same working tree / 2026-10-25 | Linux ASan + UBSan: models, ABI, UPS state and firmware host 4/4 passed | Local `/tmp/allpowers-review-sanitizers`; default sanitizer options; no hardware |
| Monitor and Widget | Same working tree / 2026-10-25 | Windows MSVC 19.51, static Qt 6.12, /MT, WinRT: Qt CTest 6/6 passed | Local `build/review-windows`; desktop 46 cases; native UI/DPI and isolated EXEs; no BLE/USB |
| Firmware and relink kits | Same working tree / 2026-10-25 | Both board targets compile, partition/USB contracts and object-kit relinking pass on Windows and Linux | Local `build/review-integrated-native` and `/tmp/allpowers-review-integrated-native`; both resulting Windows image packages pass offline maintenance validation; no activation or second-write test of these images |
| Maintenance image guards | Same working tree / 2026-10-25 | Offline Python tests 6/6 passed; existing firmware package accepted | `tests/maintenance/test_image_contract.py`; synthetic hash-consistent invalid images rejected before device access |
| Driver and release package | Same working tree / 2026-10-25 | Windows driver/INF/catalog and CPack runtime/source ZIP passed; extracted source kit rebuilt/tested without .git | Local `build/review-integrated-windows`; extracted core CTest 14/14 and desktop suites 4/4; kernel runtime remains unverified |
| Hosted product checks | `da1415b`, before review fixes / 2026-10-25 | Previously recorded hosted MSVC 19.51, Ubuntu, CTest and CPack success | Toolchain-upgrade journal below; these working-tree changes have not run on hosted CI |
| Waveshare + R2500 V2 | Firmware before review fixes; exact hardware-test revision not recorded / 2026-10-25 | Previously recorded BLE Central/GATT, fresh telemetry and physical HID UPS success | Hardware journal below; no claim of transitions, Pocket hardware or native Windows driver readiness |

Regression tests cover data validity, archive recovery, bounded work, failure
paths and build contracts. Firmware synchronization and direct-verification
ownership were also reviewed in code. The packet decoder/reducer, full-snapshot recovery deadline and
bounded history queue are shared logic. Archive AHP2 adds CRC-covered known/model
metadata without moving partitions or changing slot sizes; old AHP1 remains
readable with conservative output validity. New cursor export is additive;
legacy HISTORY-READ retains its original meaning. Firmware STATUS exposes the
product identity independently of the unchanged USB protocol version.

Driver VM lifecycle/IOCTL, LocalSystem BLE, sleep/resume and real Windows power
actions remain pending. Firmware compilation and offline contracts do not prove
an actual next write. No device was opened or flashed while applying these fixes.

The sections below are a dated journal; superseded observations are historical.

Configure/build with CMake Presets and run ctest --preset windows or native.
C++ tests exercise the actual shared C ABI, model decoding, recorded packets,
freshness, malformed data, timer wrap and bounds. QtTest covers session generations,
controls, bounded compatible history, immediate-close settings persistence,
source transitions, graph interactions and rendering.
Dropdown regression tests cover row geometry, keyboard selection/cancel and list
stability during discovery updates. Live-log tests cover tail following, paused
scrolling/selection and preservation of visible entries as the 2000-line buffer
rotates. These UI tests run offline, including at 150% display scale.
The native Windows popup regression waits beyond the animation interval and
checks the actual two-option history selector at 150%, including reopening and
the last selected item. Component geometry was also checked at 125% and 200%.
This catches Qt's delayed scroll effect restoring a popup height that omits our
layout margins; the offscreen backend alone cannot reproduce that effect.

Firmware targets check USB descriptors, partition offsets, ABI/updater/startup
contracts and source/artifact hashes. Publication audits check private identities,
absolute user paths, unexpected files, staged/working contents and reachable Git
history. Gitleaks checks credentials in history and changes. Regression tests use
temporary repositories to verify rejection of ignored-but-force-staged outputs,
unreviewed files, staged keys and secrets deleted in a later commit. CI builds
Windows and Ubuntu, fetches tags and packages Windows. CI never opens USB or BLE.

CTest and the release target run the Windows external project's tests. Single-EXE tests
inspect PE imports, reject Qt/shared CRT DLLs and run only the EXE in an isolated
folder with a system-only PATH. Embedded LGPL notices and SQLite/UI startup are
also checked. CPack release builds additionally extract their source archive and
rebuild/test the portable core without .git or Python, checking its saved version.

## Tag-driven release publication — 2026-10-25

Windows and Ubuntu checks cover exact lightweight/annotated tags, multiple tags
on one commit, the triggering Git object, invalid versions, dirty/staged/untracked
sources and attempts to publish a source archive as a Git checkout. Windows tests
compile the actual VERSIONINFO template, then reject wrong/missing EXE versions,
stale source/firmware versions and tampered package files. SemVer build metadata
is preserved inside EXEs and encoded safely in uploaded filenames.

The publisher is exercised with a mock CLI only: successful draft publication,
draft resume, interrupted/partial uploads, wrong server hashes, a moved remote
tag, a forbidden lookup, foreign/old drafts and matching/mismatching published
releases. Failure cases never publish or replace a published release.
Local Windows CTest, Linux portable checks, Actionlint and the source-kit rebuild
pass. A live GitHub Release from a new tag has not been created by these tests.

## Toolchain upgrade — 2026-10-25

Qt is pinned to 6.12.0, MSVC CI to toolset 14.51, CMake to 4.4.4 and Ninja to
1.13.2. Product targets use C++23; MSVC uses its dedicated `/std:c++23preview`
mode rather than draft-standard `/std:c++latest`. C ABI v1 is unchanged.

The Windows publication run failed with Qt 6.8.3 and the hosted runner's newer
compiler: Qt's C++17 WinRT probe selected the removed experimental coroutine
header and silently disabled Qt Core's WinRT feature. Bluetooth then failed on
the missing private factory-cache header. The new build explicitly requires
Qt Core C++/WinRT and the Bluetooth WinRT backend and configures Qt with C++23.

- Ubuntu WSL / GCC 13.3: core, both firmware profiles and all six CTest checks pass.
- Windows / locally installed MSVC 19.44: both firmware profiles and all eight
  root CTest checks pass, including the six nested Qt/UI/isolated-EXE suites.
- A clean Qt 6.12.0 static bootstrap passed the required C++/WinRT and Bluetooth
  WinRT probes. CPack and the source-kit rebuild/test without Git metadata pass.
  The hosted MSVC 19.51 build, CTest, CPack/source-kit rebuild and publication
  audits also passed on GitHub Actions (commit `da1415b`).
- USB descriptor, partition and maintenance contracts pass without changing fixtures.

These results do not replace the driver VM/runtime checks below.

## Waveshare hardware check — 2026-10-25

The user authorized flashing Waveshare 1.47 only. Its registered identity,
existing external rollback image and prior physical ROM-entry verification were
checked before writing. An attempted full flash read through the RAM stub failed;
it is not a new full backup. ROM reads of the initial region and NVS succeeded.
The existing verified external factory/rollback copies remain the recovery copies.

Two actual writes of the C++23 firmware were completed using `--rom-only`, with
all four written segments verified. The second write was entered through the
new firmware's HID maintenance command. NVS and history partitions were not
erased; the saved target loaded and the historical record count continued rising.
Real BLE Central/GATT notifications, fresh decoded telemetry, dynamic/static USB
HID UPS reports and Windows power status agreed on full charge and mains power.
Display/graph/history self-tests passed, with no HID-send or history errors.
The user confirmed live screen updates, green RGB and BOOT page switching.

The Qt 6.12 widget was restarted in its saved USB mode and passed a real native
desktop check: board archive points were imported into the combined graph,
history opened without error and the computer's BLE transport stayed unused.
The downloaded CI artifact and its runtime ZIP were checksum-verified. Both
MSVC 19.51-built EXEs passed a further real USB telemetry/archive check; the
updated widget was left running.
Pocket was compiled but not flashed. Mains/battery transitions, physical ROM
entry and factory rollback were not exercised again in this session.

## Local checks — 2026-10-24

| Check | Result |
| --- | --- |
| New CMake native core / C ABI / fixtures on Windows 11, MSVC 19.44 | Passed |
| New CMake native core / C ABI / fixtures on Ubuntu WSL, GCC 13.3 | Passed |
| Waveshare/Pocket firmware compilation and USB/partition checks through CMake on Windows | Passed |
| Static Qt UI/history/graph tests, isolated EXE, CPack/source-kit rebuild | Passed |
| Clean Qt 6.8.3 static MSVC/WinRT bootstrap, then monitor/widget tests | Passed |
| Header-only incremental rebuild and unchanged no-op build, MSVC/Ninja and GCC/Ninja | Passed |
| Separate source/binary directories, Windows `BUILD_TESTING=OFF`, source-kit packaging outside the checkout | Passed |
| Deleted firmware byproduct restored by the next Linux build | Passed |
| New CMake Linux firmware orchestration, both boards | Passed; official SHA256-verified SDK archives were downloaded through Windows because WSL could not reach the ESP32 index |
| Reachable Git history and current public sources, Gitleaks 8.30.1 | No leaks detected |
| GitHub workflow, Actionlint 1.7.12 | Passed; remote execution was pending at this stage (subsequent hosted evidence above) |

## USB monitor — 2026-10-24

USB tests cover both profiles with fake channels/sanitized records: fragmentation,
malformed/oversize lines, known-field/source mapping, stale counters, CRC rejection,
unknown dates, archive deduplication and separate station/archive retention.
UI tests cover live recording, archive selection, USB/Bluetooth choice and driver
management being located only in Diagnostics.

An earlier manual Waveshare USB-only test verified fresh telemetry and archive
import, with invalid/undated records skipped and no USB/CRC errors. Pocket's
hardware path remains unverified beyond protocol tests. Publication preparation
uses only offline tests and compilation, without contacting either board.

## Experimental Windows UPS work — offline boundary

The new UPS mapper and the exact kernel state validator/cache are tested on
synthetic snapshots for 100/50/5/0%, missing required fields, unknown runtime,
15-second expiry, replay rejection and connection-generation/tag changes.
QtTest exercises the service host with an offline BLE transport, real named pipes
and SQLite: multiple viewers, data recording after closing a window, invalid IPC,
and legacy/service history merging. No CTest test connects a station.

The x64 KMDF driver compiles and its INF/catalog pass WDK tools; PE imports and
embedded package hashes are checked. This is build validation, **not kernel runtime
validation**. VM provisioning was deferred by the user. No ALLPOWERS service/driver
has been installed on the host; Secure Boot, BCD and host certificates are unchanged.
SCM lifecycle, installation/update/rollback/removal, Battery Class IOCTL behavior,
Driver Verifier, Windows shutdown/hibernate policy and real BLE from LocalSystem
before login are **unverified** until guest tests. Hyper-V raw Bluetooth passthrough
is not assumed. See [the lab procedure](windows-ups.md).

Both firmware targets now pass the CMake build/contract path on Linux. This does
not verify WSL's failed network/index-update path: the pinned SDK was installed
from verified cached downloads. At this earlier stage Windows and Ubuntu CI
jobs had not run remotely; subsequent hosted results are recorded above.

An earlier authorized manual WinRT test verified a real Central/GATT connection,
subscription and fresh R2500 V2 telemetry. That result is separate from BLE
observation, hardware USB UPS reporting and the unverified kernel-driver path.

An offscreen software-rendering benchmark used 6,480 history samples in a
1,120 × 820 window: full plot redraws were about 20–30 ms; cached redraws about
0.03–0.1 ms on the development machine. These are local measurements, not an FPS
guarantee. SQLite, CSV, logs and settings writes run outside the UI thread.

Original research/Git history, factory material and verified application rollback
images remain privately preserved. Existing user history/settings were retained.
The Pocket has no complete factory backup; partial reads are not called backups.
