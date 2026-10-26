# Getting started

[Documentation](README.md) · [Troubleshooting](troubleshooting.md)

## Choose how to connect

| Goal | What you need | What it does |
| --- | --- | --- |
| See station readings on a PC | Windows 10/11 x64, PC Bluetooth, Monitor or Widget | Reads station BLE telemetry directly; no board, service or UPS driver required |
| Report battery state through USB HID UPS | A supported ESP32-S3 board running the project firmware | The board connects to the station over BLE and reports supported battery state to the computer, independently of the desktop application |
| See detailed readings from that board | Windows 10/11 x64, a USB data connection, Monitor or Widget | Reads the board over USB CDC while the board keeps its BLE connection and HID UPS reporting |

Check [model compatibility](BLUETOOTH.md#compatibility-and-evidence) first.
R2500 V2 + Waveshare has BLE/GATT and Windows HID UPS hardware evidence.
Pocket is compiled and protocol-tested; this does not establish its hardware
behavior in that scenario. The separate board-free Windows UPS service/driver
remains experimental.

The project supplies station-state data. Electrical backup capability and
transfer time depend on the station. Computer shutdown depends on OS power
settings and requires separate verification.

## Download

Open [GitHub Releases](https://github.com/borro/allpowers-bridge/releases) and
choose a stable release, or deliberately select a prerelease if you need to test
it. Read its release notes before replacing an existing installation.

- **Windows:** download the Monitor and/or Widget EXE assets. Each application
  is self-contained; there is no Qt or Python runtime to install. Monitor is the
  full interface; Widget is an independent desktop card.
- **Boards:** choose the firmware ZIP for your exact board profile,
  `waveshare-147` or `pocket-096`. Follow [Updating and recovery](recovery.md)
  before any first installation or update; a download alone is not a verified
  rollback path.
- **Source:** the corresponding-source ZIP includes the application and Qt
  sources needed for rebuilding/relinking. GitHub's automatic “Source code”
  archives contain only the repository. Firmware ZIPs include their own source
  and relinking kits. See [Building](building.md#packaging-and-versions).

The release's `SHA256SUMS` lists the download hashes. For example, in PowerShell
run `Get-FileHash -Algorithm SHA256 .\<downloaded-file>` with the actual filename
and compare the result with its entry. A matching hash checks download integrity.

## Connect from Windows

1. Run **AllpowersMonitor.exe**, or open **Settings** from the Widget tray menu.
2. Select **Bluetooth** for a direct PC connection, or **USB · Waveshare / Pocket**
   for a board already connected to the station.
3. Discover and select the device. Connect in Monitor; save the selection in Widget.
4. Check that readings are fresh and update over time. Unknown or stale readings
   are not evidence of an empty battery. Diagnostics explains connection state
   and the last error; see [Troubleshooting](troubleshooting.md).

Some stations accept only one BLE client. Disconnect the other app or board
before using direct PC Bluetooth. The application does not take over an occupied
connection. To keep the board connected, use USB mode instead. Monitor and Widget
also take turns owning a direct connection; disconnect in one before connecting
in the other. If an experimental UPS service is already installed, it owns BLE
and supplies the viewers with its data.

The app remembers Bluetooth station and USB selection separately. Monitor
searches for its saved station on the next normal start and retries if it is
unavailable; **Disconnect** cancels waiting and retries. USB disconnect closes
the PC port while the board continues its own work.

## Use a prepared board

Connect the board to the computer with a USB data connection. An unconfigured
board opens the station picker; a configured board remembers its station in NVS.
BOOT changes pages, double BOOT opens the picker, and RESET resets the hardware.
See [Board profiles](boards.md) for supported variants.

Fresh station readings on the display demonstrate telemetry reception. For the
supported UPS scenario, also check battery reporting in the operating system.
Keep that observation separate from any test of power transfer or automatic
shutdown. [Validation](validation.md) records the actual hardware checks.

## Preview without hardware

```powershell
.\AllpowersMonitor.exe --demo
.\AllpowersWidget.exe --demo
```

Demo uses synthetic readings and temporary storage, does not connect to a
station, and does not change normal settings or Widget login startup. Add
`--language en`, `ru`, `zh-CN` or `zh-TW` to preview a language for this run.

Next: [Windows applications](windows-apps.md) for graphs, languages, history,
desktop placement and diagnostics.
