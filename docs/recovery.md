# Updating and recovery

Building is separate from writing. No build/test/setup command opens USB or BLE.
The firmware keeps CDC and vendor HID maintenance even if station data is absent.
The USB descriptor contract and partition offsets are checked against fixtures.

## Before replacing code

For a new owner without a registered identity, first enter physical ROM mode and
list ports with `python -m serial.tools.list_ports -v`. Listing does not open a
port or write the board. Match the newly appearing VID 303A/PID 1001 port to the
physical board; its ROM USB serial gives the chip MAC used by `rom_port`.
Register that MAC and derive the application serial as `R2500-` followed by its
12 uppercase hex digits without colons. The board profile is selected explicitly;
the USB VID alone does not distinguish Waveshare from Pocket. Then use the
registered `identity` command below to confirm the chip before considering a write.

Prepare a compatible rollback package for that exact board profile and store the
complete package outside the checkout with the application's SHA256 and its
`manifest.json`. The archive compatibility gate below applies to both the image
being written and the registered rollback. A downloaded image is a candidate
rollback until its boot/update path has been verified; a read of one region is
not a factory backup. Do not mark
physical ROM recovery as verified until BOOT/RESET entry works on that unit.

1. Confirm the board profile and record the actual chip MAC and generated USB serial.
2. Verify physical ROM entry on that unit: hold BOOT, press/release RESET, release
   BOOT; for a board without RESET, insert it while holding BOOT.
3. Keep a checked archive-compatible rollback package and record its application
   SHA256 and manifest path outside Git.
4. Copy `config/devices.example.json` to `.local/devices.json`, enter the actual
   identity, rollback path/hash and `physical_rom_verified: true` only after testing.

Historical factory and application copies remain private recovery material. They
do not automatically qualify as safe rollback for the current history format.
The Pocket factory backup was waived; partial reads are not a full backup.
Historical bundles and original Git history are private because they embed images.

## AHP1/AHP2 storage and supported rollback

New firmware reads both physical AHP1 and AHP2 records and writes AHP2. AHP2 keeps
the existing partition and 32-byte slots, adding CRC-protected known/model data.
An old AHP1-only writer can mistake an AHP2 archive for empty flash and erase a
sector when it starts again. Restoring its application bytes does not preserve
that archive safely.

The supported rollback therefore retains an AHP1/AHP2-capable writer. It does not
migrate history back to an AHP1-only writer. Every writable image and registered
rollback must have the exact `history_storage` contract: schema 1, readers `[1,2]`,
writer 2, 32-byte records, and capability `ALLPOWERS_HISTORY_R12_W2_S32`. That
capability must also occur in the compiled application; an added JSON sidecar
alone cannot qualify an unchanged old binary. The image's file hashes and the
registered rollback application SHA256 must match the complete package manifest.

Register `rollback.path`, `rollback.sha256` and `rollback.manifest` using the
compatible package in external storage; see `config/devices.example.json`.
The normal `flash` path rejects a missing contract, an AHP1-only image, a raw
rollback without its manifest, or metadata bound to another application **before
opening ROM USB**. The same conservative gate applies when the current archive
contents are unknown. This avoids guessing safety from an old version number.

For a supported rollback, keep the existing NVS/history partitions, select the
compatible package explicitly with `--image-dir` and the registered board
profile, and use the ordinary identity-checked update procedure. The compatible
reader recovers both formats before it appends. Retain prior external archive
copies and factory material; do not relabel an AHP1-only backup as compatible.
Downgrade to AHP1-only firmware is outside this supported maintenance path.

The automatic test uses the frozen original AHP1 Ring and current production
Ring on one RAM flash: it demonstrates the unsupported erase, then checks
AHP1 → AHP2 → compatible rollback/write → upgrade without losing records.
Maintenance tests independently prove rejection before device access. These are
offline checks, not physical recovery or second-write evidence for new images.

## Explicit commands

```sh
python -m pip install -r tools/requirements-maintenance.txt
python tools/manage_board.py status --board waveshare-147
python tools/manage_board.py boot --board waveshare-147
python tools/manage_board.py identity --board waveshare-147
python tools/manage_board.py flash --board waveshare-147 --confirm-write
```

If the RAM flasher's USB transfer is unreliable, add `--rom-only` to `identity`,
`flash`, `verify` or `reset`. This uses ROM commands without uploading the stub;
the same board identity, image, partition and rollback checks still apply.
ROM reading is slower. A failed or partial flash read is not a verified backup.

Python is optional and used only by these explicit USB tools. CMake/CTest/CPack
do not invoke them. Replace the board name for Pocket. `boot` sends the existing HID feature command
`BOOT-R2500!!`; it does not erase flash. Physical ROM entry is independent of the
application. `identity`, `verify` and `reset` require the registered device in ROM.
Writing validates the matching build manifest, hashes, complete partition schema,
partition MD5 and every segment's write/sector-erase range before opening ROM USB.
It also checks the compiled history capability and the registered compatible
rollback package before that access.
It rejects any overlap with protected data or the inactive application, then checks hardware,
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

Run the maintenance validator's tests explicitly with
`python -m unittest discover -s tests/maintenance -v`. They create synthetic images
in temporary storage and never import a USB backend or open a device. These tests
are separate from the Python-free CMake/CTest product build. The dedicated
`maintenance` CI job runs them for pushes, pull requests and tags and must pass
before product builds and publication; no USB packages are installed for it.
