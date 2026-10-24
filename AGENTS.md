# Working on ALLPOWERS

- Product code is C++23: portable `core/`, common dual-board `firmware/`, native
  Qt Windows monitor. Python is build/test/maintenance tooling only.
- Build with CMake Presets/Ninja, CTest and CPack. No Python build wrapper.
  Windows Qt requires static MSVC libraries, /MT and the WinRT backend, not MinGW.
  Dependency versions/checksums live in cmake/Dependencies.cmake; product
  versions come only from Git tags (or generated VERSION in a source archive).
- Keep device addresses, serials, reports and recovery images out of Git. Real
  identities are in ignored local configuration; recovery storage is external.
- Build/test/cleanup must never flash a board or switch station connections.
  Device maintenance always requires an explicit profile and matching identity.
- Firmware changes must retain an accessible next write and independent physical
  recovery. An image backup, ROM presence or old updater alone is not proof.
  Put update support in the first image and verify an actual second write before
  further experimentation. Micropitch pads are not an accessible user recovery path.
- Preserve USB/partition/maintenance contracts; do not regenerate fixtures simply
  to bypass a failing check. Preserve NVS/history and never write eFuses here.
- Distinguish BLE observation, a real Central/GATT connection and functioning USB
  UPS reporting. Describe the exact verification and remaining limits.
- The AULA receiver experiment is retired. Do not resume it without a new explicit
  user request. Its lost update path must not be repeated.
- Test core on Windows/Linux, both firmware targets, native Qt tests and packaging.
  Keep README.md and README.ru.md aligned. Audit the index before committing.
