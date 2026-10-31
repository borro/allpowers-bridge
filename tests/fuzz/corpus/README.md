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

## Refreshing

1. Build the fuzzers for the source revision being evaluated. Keep new inputs
   synthetic; inspect provenance before considering them for Git.
2. Run `python3 tests/fuzz/seed_corpus.py build/fuzz-selection` to stage both
   generated seeds and the currently retained inputs in a fresh build directory.
3. For each target, run the matching executable with
   `-merge=1 build/fuzz-selection/<target>/corpus <candidate-corpus>`.
   Keep the candidate corpus unchanged and save merge logs outside Git.
4. Encode newly selected bytes as hex, with a SHA-256 filename, in the matching
   directory here. Preserve existing useful seeds; update provenance and counts.
5. Stage into another fresh directory and replay with `-runs=0` under ASan/UBSan.
   Compare source coverage with the full candidate corpus using identical
   coverage binaries. Check that no covered lines or branch outcomes are lost.
   A second merge of the full corpus should add no features.
6. For a discovered bug, also add a focused regression test after fixing it.

The corpus cannot reach functions the harness never calls. Extending archive
coverage requires additional harness scenarios, not only additional seeds.
