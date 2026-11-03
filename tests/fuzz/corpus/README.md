# Retained synthetic fuzz inputs

These inputs supplement the readable cases in `../seed_corpus.py`. CMake's
`fuzz-corpus` target decodes them into each build's writable corpus before fuzzing.
Local discoveries remain in the build directory; running the generator again
does not delete them. Neither fuzzing nor replay writes into this source directory.

Each `.hex` file contains hexadecimal bytes, wrapped at 32 bytes per line. Its
name is the SHA-256 of the decoded bytes, checked during staging. Text encoding
keeps changes reviewable and prevents Git line-ending conversion of binary inputs.
Do not pass these text files directly to libFuzzer: stage them with the generator.

## Provenance

The initial selection comes from two one-hour rounds on commit
`14afe15aaf43583515bac859ec85b78d0b717298`, with Clang 18 libFuzzer, ASan/UBSan
and random seed `20261027`. All inputs derive from synthetic generated seeds and
mutations: no captured station traffic, device identities or recovery images.
No crashes were found in that campaign; these are coverage seeds, not bug
reproducers.

| Target | Additional inputs | Decoded bytes |
| --- | ---: | ---: |
| BLE | 21 | 914 |
| HID wire | 120 | 3721 |
| HID server | 103 | 11378 |

Selection used `-merge=1` with the original generated seeds as the first corpus
and the completed campaign as the second. Only additional coverage/features
were retained. This is a coverage-preserving selection, not a claim of a
mathematically minimal corpus.
Source-coverage replay found one HID wire branch outcome omitted by the original
merge. One additional 12-byte input was retained using the coverage-instrumented
build; the table includes it. Line and branch-outcome coverage must both be
checked when refreshing, because libFuzzer's feedback alone is not equivalent
to source coverage.

### Eight-hour refresh — 2026-11-01

The next selection comes from commit
`80458d29a3c8c27285b794d2a31b1b611fde4231` (`v0.7.1`): eight one-hour rounds,
five concurrent targets, Clang 18 libFuzzer with ASan/UBSan and seed `20261027`.
All 40 runs passed, with 29,427,724,473 inputs and no crashes, sanitizer errors
or timeouts. The 7,010 candidate files remain unchanged outside Git.

The following counts include the initial selection above. Generated examples
remain in `seed_corpus.py` and are additional to these retained files.

| Target | New inputs | New decoded bytes | Total retained inputs | Total decoded bytes |
| --- | ---: | ---: | ---: | ---: |
| BLE | 1 | 67 | 22 | 981 |
| HID wire | 8 | 759 | 128 | 4480 |
| HID server | 89 | 18290 | 192 | 29668 |
| Archive | 240 | 20657 | 240 | 20657 |
| Telemetry | 364 | 74052 | 364 | 74052 |
| **Total** | **702** | **113825** | **946** | **129838** |

Selection starts with generated and previously retained seeds, then merges the
completed campaign using its original frozen binaries. Coverage-instrumented
replay identified eight additional archive inputs needed to preserve source
branch outcomes; these are included above. Fresh staging from the repository
passed ASan/UBSan replay, and a second merge added zero features or coverage
edges for every target.

Identical LLVM coverage binaries built from an isolated `80458d2` snapshot
compared the old seeds, full campaign and final retained inputs. The union grew
from 443 to 449 covered lines and from 503 to 532 covered branch outcomes. The
retained set preserves every line and outcome reached by the full campaign.
The maps contain 565 lines and 556 branch outcomes in portable headers reached
by these harnesses: **79.47% line coverage is not whole-project coverage**.
Windows UI/transport code, device SDK behavior and uncalled code are outside
that denominator. Other working-tree changes are excluded from this report.

Local evidence is in `build/fuzz-refresh-20261101/`: `coverage/index.html`,
`coverage/coverage.json`, `retained.json`, `verification.json` and merge/replay
logs. The original run is in `build/fuzz-soak-20261101/campaign/`.

### v0.7.3 refresh — 2026-11-01

The second eight-hour campaign ran on
`e1b0979fe2c6ec11cced3defe4ec7c3c4900a0d5` (`v0.7.3`), starting with the
946 retained inputs above plus generated cases. All 40 runs passed under
ASan/UBSan: 30,282,636,187 inputs, no crashes, sanitizer errors or timeouts.
The original 2,062 corpus files remain unchanged outside Git.

Merging against the existing repository seeds retained just two additional
inputs from the 939 new files: one 524-byte HID server sequence and one 256-byte
archive sequence. They preserve new libFuzzer execution features (HID server
`ft` 867 to 869; archive 1251 to 1255). Most new files were smaller equivalents
of already retained inputs. Existing seeds were preserved.

| Target | New inputs | New decoded bytes | Total retained inputs | Total decoded bytes |
| --- | ---: | ---: | ---: | ---: |
| BLE | 0 | 0 | 22 | 981 |
| HID wire | 0 | 0 | 128 | 4480 |
| HID server | 1 | 524 | 193 | 30192 |
| Archive | 1 | 256 | 241 | 20913 |
| Telemetry | 0 | 0 | 364 | 74052 |
| **Total** | **2** | **780** | **948** | **130618** |

Replay using identical LLVM coverage binaries from the campaign's exact source
snapshot reached 454/576 mapped lines (78.82%) and 526/550 branch outcomes for
all three sets: previous repository seeds, full campaign and final retained
inputs. This run added execution features, but no new mapped lines or branch
outcomes. Percentages from different source revisions have different maps and
must not be compared as if their denominator were unchanged. The denominator
still covers only portable code mapped into the harnesses, not the whole project.

Fresh staging from repository text passed ASan/UBSan replay for every target;
merging the full campaign again added zero features and edges. Original data:
`build/fuzz-soak-20261101-e1b0979/`. Selection, replay/merge logs, counts and
HTML/JSON coverage: `build/fuzz-refresh-20261101-e1b0979/`.

### Ten-hour refresh — 2026-11-03

Commit `2b3061fe60841c0b1ed1381dc8c2f6f005432bf3` completed ten one-hour
rounds of all five targets with Clang 18 libFuzzer, fatal ASan/UBSan and seed
`20261027`. All 50 checks passed: 36,934,478,934 executed inputs, no sanitizer
errors, crashes or timeouts. The frozen binaries, test plan and source archive
passed their saved SHA-256 checks; all 1,364 archived source files matched Git
and the extracted campaign source. The 2,103 original corpus files are preserved.

Incremental comparison with the previous 948 retained inputs found two new
archive sequences (216 and 264 bytes): execution features grew from 1,255 to
1,257 without new coverage edges. These are synthetic coverage seeds, not
vulnerability reproducers; no product-code fix was indicated by this campaign.

The final selection also incorporates the campaign's `REDUCE` results. Generated
examples remain fixed; a merge selects shorter inputs from the union of previous
retained seeds and the full campaign. This replaced/removed 218 previous files,
added 152 files (including the two archive discoveries), and kept 730 unchanged.
Retained data decreased from 948 files / 130,618 bytes to **882 files / 117,380
bytes**, a 10.13% reduction in decoded bytes despite the two new features.
Old inputs remain in Git history and local evidence. This preserves measured
coverage/features; it is not a claim of a mathematically minimal corpus.

| Target | Executed inputs | Previous files / bytes | Final files / bytes | Removed / added files |
| --- | ---: | ---: | ---: | ---: |
| BLE | 27,380,269,678 | 22 / 981 | 16 / 314 | 10 / 4 |
| HID wire | 927,457,534 | 128 / 4480 | 120 / 3438 | 42 / 34 |
| HID server | 1,739,654,986 | 193 / 30192 | 163 / 21854 | 107 / 77 |
| Archive | 46,492,037 | 241 / 20913 | 231 / 20221 | 42 / 32 |
| Telemetry | 6,840,604,699 | 364 / 74052 | 352 / 71553 | 17 / 5 |
| **Total** | **36,934,478,934** | **948 / 130618** | **882 / 117380** | **218 / 152** |

Identical LLVM coverage binaries built from the exact campaign snapshot reached
454/576 mapped lines (78.82%) and 526/550 branch outcomes with the old seeds,
full candidate union and final retained set alike. Source-coverage comparison
restored one HID wire and seven archive inputs omitted by the first reduction.
Direct replay also restored a four-byte HID server input: merge reported no
missing features, but replay fell from 869 to 867 without it and returned to 869
when restored. Merge alone is therefore insufficient as the preservation check.
Fresh repository staging passed ASan/UBSan replay for all five targets, and
merging the union of previous seeds and the full campaign again added zero
features or edges. Selection lost no covered line or branch outcome, including
old HID server features absent from the last round's reduced active corpus.

This is a bounded negative result, not proof that the repository has no
vulnerabilities. The denominator covers only portable headers mapped into these
harnesses. Windows UI/transports, device SDK behavior, concurrency and uncalled
paths are outside this campaign's verification.

Original evidence: `build/fuzz-soak-20261103-2b3061f/`. Audit, selection,
replay/merge logs and HTML/JSON coverage:
`build/fuzz-refresh-20261103-2b3061f/` (`reduction-coverage/index.html`,
`reduced-retained.json`, `reduced-verification.json`). Raw reports remain outside Git.

## Refreshing

1. Build the fuzzers for the source revision being evaluated. Keep new inputs
   synthetic; inspect provenance before considering them for Git.
2. Run `python3 tests/fuzz/seed_corpus.py build/fuzz-selection` to stage both
   generated seeds and the currently retained inputs in a fresh build directory.
3. For each target, run the matching executable with
   `-merge=1 build/fuzz-selection/<target>/corpus <candidate-corpus>`.
   Keep the candidate corpus unchanged and save merge logs outside Git.
4. For a `REDUCE` refresh, keep generated examples as the first corpus and merge
   the union of existing retained inputs and campaign discoveries as candidates.
   Prefer shorter equivalents, but preserve all old and new measured features.
   Compare source coverage too: this can require restoring inputs omitted by
   libFuzzer's merge. Keep original inputs outside Git until verification passes.
5. Encode selected bytes as hex, with a SHA-256 filename, in the matching
   directory here. Remove superseded inputs only after verifying their coverage
   is preserved; update provenance and counts.
6. Stage into another fresh directory and replay with `-runs=0` under ASan/UBSan.
   Compare direct replay counters with the old corpus using the same binary,
   seed and `-shuffle=0`; investigate decreases even when merge adds nothing.
   Compare source coverage with the full candidate corpus using identical
   coverage binaries. Check that no covered lines or branch outcomes are lost.
   A second merge of the old/new candidate union should add no features.
7. For a discovered bug, also add a focused regression test after fixing it.

The corpus cannot reach functions the harness never calls. Extending archive
coverage requires additional harness scenarios, not only additional seeds.
