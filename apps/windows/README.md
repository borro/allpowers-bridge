# ALLPOWERS Windows applications

Native C++23 / Qt Monitor and desktop Widget for Windows 10/11 x64. Both are
standalone EXEs with static MSVC/Qt and WinRT Bluetooth, sharing the portable
protocol core. They read a station directly over BLE or a prepared board over
USB HID. Ordinary viewing needs no UPS driver or service.

- [Getting started](../../docs/getting-started.md)
- [Windows applications](../../docs/windows-apps.md): UI, languages, history and diagnostics
- [Windows development](../../docs/windows-development.md): offline checks and translations
- [Building and packaging](../../docs/building.md)
- [USB telemetry protocol](../../docs/usb-monitor.md)
- [Experimental Windows UPS service/driver](../../docs/windows-ups.md)

[All documentation](../../docs/README.md) · [Project overview](../../README.md)

USB history uses AHP3 boot epochs and protocol version 2. Both viewers synchronize
the clock once per boot. Monitor uses the same live and archive readings in its
time and epoch views. New live samples retain the board epoch; older local samples
without boot information form an unknown-epoch group. Undated records appear only
in the epoch view; CSV preserves original dates. Old USB archive caches
are discarded on this incompatible transition; station history is separate.
