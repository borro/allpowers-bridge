# Updating and recovery

Building is separate from writing. No build/test/setup command opens USB or BLE.
The firmware keeps CDC and vendor HID maintenance even if station data is absent.
The USB descriptor contract and partition offsets are checked against fixtures.

## Before replacing code

1. Confirm the board profile and record the actual chip MAC and generated USB serial.
2. Verify physical ROM entry on that unit: hold BOOT, press/release RESET, release
   BOOT; for a board without RESET, insert it while holding BOOT.
3. Keep a checked application rollback image and record its SHA256 outside Git.
4. Copy `config/devices.example.json` to `.local/devices.json`, enter the actual
   identity, rollback path/hash and `physical_rom_verified: true` only after testing.

The original Waveshare has a verified full factory copy in private storage. The
Pocket factory backup was waived; its partial reads are not a full backup. Its
verified previous application is an application rollback, not factory restoration.
Historical bundles and original Git history are private because they embed images.

## Explicit commands

```sh
python -m pip install -r tools/requirements-maintenance.txt
python tools/manage_board.py status --board waveshare-147
python tools/manage_board.py boot --board waveshare-147
python tools/manage_board.py identity --board waveshare-147
python tools/manage_board.py flash --board waveshare-147 --confirm-write
```

Python is optional and used only by these explicit USB tools. CMake/CTest/CPack
do not invoke them. Replace the board name for Pocket. `boot` sends the existing HID feature command
`BOOT-R2500!!`; it does not erase flash. Physical ROM entry is independent of the
application. `identity`, `verify` and `reset` require the registered device in ROM.
Writing validates the matching build manifest, hashes, partitions and hardware,
then writes only bootloader, partition table, boot_app0 and app0. It verifies the
written segments and uses the existing volatile watchdog reset path. NVS/history
are outside these writes; no full-chip erase or eFuse command is provided.

After a new firmware's first activation, actually test another write before
further experiments. A retained ROM or old updater alone is not proof of an
accessible recovery path. Do not revive the retired AULA experiment.

The optional maintenance command uses `boards.py` for board/identity checks and
`image_contract.py` for image hashes and partition validation. Its dependencies
are pinned in `requirements-maintenance.txt`. These four files are independent
of the CMake build. For telemetry and archive import, use the monitor's USB mode;
select a station with the board's physical controls.
