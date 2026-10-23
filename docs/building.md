# Build prerequisites

## Common

Git, uv 0.9.5 (the CI version), Python 3.11. Run `uv sync --locked` from the root.
The lock fixes CMake 3.31.6, Ninja 1.11.1.4, Zig 0.13.0 and aqtinstall 3.3.0.
`uv run --locked python tools/build.py setup` installs a checksum-verified
Arduino CLI 1.5.1 and Espressif ESP32 Core 3.3.11. On Windows it also installs
the official Qt 6.8.3 MSVC desktop SDK inside `build/qt/`.

Commands: `test`, `core`, `waveshare-147`, `pocket-096`, `windows`, `all`, `doctor`,
`setup`, `clean`. Native C ABI outputs are isolated in `build/native/Windows`
and `build/native/Linux`. Firmware sketch staging is independent per board.
Intermediate output is ignored; no source/version header is rewritten by a build.

Existing Arduino installations: copy `config/arduino.example.json` to
`.local/arduino.json`. Set the paths; optionally set `platform` to `win32` or
`linux` if sharing a checkout. `ARDUINO_CLI` / `ARDUINO_DATA_DIR` override these.
Host-specific `.local/arduino.win32.json` / `.local/arduino.linux.json` take precedence.

## Windows desktop

Install Microsoft's signed [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022)
with MSVC x64/x86 C++ tools and Windows 11 SDK. The local verification used
MSVC 19.44 and SDK 10.0.26100. The builder discovers the installation with
vswhere and imports vcvars64; no Developer Command Prompt or IDE is required.

Qt MSVC is required: Qt's MinGW package has a dummy Bluetooth backend. The
builder rejects a missing WinRT feature before compiling. Set `QT_ROOT` to reuse
an existing official 6.8.3 MSVC SDK. The executable is native C++17, links Qt
dynamically, and contains the same protocol source as the portable core.

`windows` configures CMake/Ninja, builds the application and QtTest checks,
deploys the needed DLLs/plugins with windeployqt and creates a ZIP + SHA256.
The optional offscreen platform is deployed so `--self-test -platform offscreen`
can verify the packaged UI/database without opening Bluetooth.

`--demo` uses synthetic history and temporary user data. `--scan-seconds 5
--report build/scan.json` scans without connecting to peripherals. Reports and
screenshots may contain identifying information outside demo mode: keep them local.

## Linux

Only the portable core/tests and both ESP32 firmware targets are supported.
Run the same sync/test/setup/all commands in Linux. The core produces an SO;
firmware uses Linux-hosted Espressif tools. Windows EXEs are not cross-built.
The new desktop transport/UI is structured for later portability, but Linux and
macOS desktop support are not part of this release.
When sharing a Windows checkout with WSL, use a Linux uv binary and set
`UV_PROJECT_ENVIRONMENT=build/venv-linux` to keep the Windows `.venv` intact.
SDK/configuration directories are separated by host OS.
For firmware builds in WSL, keep the checkout/SDK on the Linux filesystem;
repeated header access through `/mnt/c` can be substantially slower.

`clean` removes `build/` and `dist/`, including downloaded project-local SDKs.
It never removes `.local/`, private rollback storage or application history.
Run setup again after cleaning SDKs.
