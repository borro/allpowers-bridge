# Windows development

[Documentation](README.md) · [Building](building.md) · [Architecture](architecture.md)

The desktop applications use C++23, Qt Widgets, static MSVC libraries, /MT and
the WinRT Bluetooth backend. Use the root CMake presets for the supported build;
see [Building](building.md) for toolchains, managed Qt, size optimization,
packaging and driver checks. [Windows applications](windows-apps.md) documents
the user interface.

## Build variants

Release and MinSizeRel application targets use size optimization with ordinary
linking. Managed Release Qt also optimizes size without IPO/LTO; Debug retains
its normal settings. The standalone `apps/windows` project accepts
`-DALLPOWERS_OPTIMIZE_SIZE=OFF` for application-only comparisons with the same
Qt SDK. This does not change the SDK's own build settings or remove languages
and functionality. Release packaging compresses only staged EXE copies; normal
build/install outputs stay uncompressed.

## Offline checks and diagnostics

`--demo` shows synthetic data using temporary storage and never connects BLE.
`--self-test -platform offscreen` performs an offline core/UI/database smoke
test. `--version` prints the embedded Git-derived version; `--export-licenses`
extracts notices. Normal CTest/build/package targets never switch a station
connection or install a service/driver.

`--verify-station 50 --report <path>` explicitly tests the saved station for
several seconds of fresh telemetry, then disconnects. This is a manual hardware
test and must not be invoked as an offline documentation/build check. Reports
may contain device identities and belong outside Git.

## Translation maintenance

Both applications save `ui_language` (`system`, `ru`, `en`, `zh-CN`, `zh-TW`)
in their own settings.json. UI behavior is described under
[Language](windows-apps.md#language).

```sh
cmake --build build/windows/windows-monitor --target update-translations
```

This explicit command updates the Russian and both Chinese Qt Linguist catalogs.
Normal builds compile/embed translations without editing source catalogs;
`translation-coverage` checks missing translations.

The static Qt SDK must include matching LinguistTools. Ordinary builds compile
the checked-in catalogs with lrelease; `translation-coverage` extracts into a
disposable catalog and rejects untranslated additions. Source packages include
pinned Qt Tools sources for rebuilding the translation tools.

Chinese text uses installed Microsoft YaHei / Microsoft JhengHei font families.
The offscreen renderer registers these Windows fonts too, so offline screenshots
and glyph tests exercise the same scripts. No font files are bundled. Glyph
cases report a skip on server images without those optional fonts; catalog,
locale, preference, IPC and standalone-EXE checks still run for both variants.

## Localized service diagnostics

Service diagnostics optionally include `ui_messages` with `phase`, `error`,
`driver_error`, and `ups_reason` descriptors: a stable `id` and positional `args`.
The client renders known descriptors in its own language; unknown/missing IDs fall
back to the existing text. Error replies may also carry `ui_message`. This is an
additive extension of the existing IPC version. OS errors, legacy service text,
RAW data, historical logs, device names, and license texts retain their original
language. CSV column names and machine identifiers remain language-independent.
