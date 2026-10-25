"""Offline maintenance guards. Run explicitly; never opens USB/BLE."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import image_contract as contract
import manage_board


def table_bytes(entries=None):
    entries = entries or list(contract.PARTITIONS.items())
    data = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, subtype, address, size,
                                name.encode(), flags)
                    for name, (kind, subtype, address, size, flags) in entries)
    return (data + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(data).digest()).ljust(0xc00, b'\xff')


class ImageContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        for _, name, _ in contract.SEGMENTS:
            (self.folder / name).write_bytes(b'\xe9' * 32)
        self.table = self.folder / 'R2500Adapter.ino.partitions.bin'
        self.table.write_bytes(table_bytes())

    def manifest(self):
        fixture = json.loads((contract.ROOT / 'tests/fixtures/usb-contract.json').read_text())
        manifest = dict(board='waveshare-147', descriptor_sha256=fixture['descriptor_sha256'],
                        maintenance='BOOT-R2500!!', efuse_writes=False,
                        files={p.name: contract.sha(p) for p in self.folder.glob('*.bin')})
        (self.folder / 'manifest.json').write_text(json.dumps(manifest))

    def rejected(self):
        self.manifest()  # Consistent hashes must not bypass layout checks.
        with self.assertRaises((ValueError, RuntimeError)):
            manage_board.validate_image(self.folder, 'waveshare-147')

    def test_valid_plan_and_sector_boundaries(self):
        self.manifest()
        manage_board.validate_image(self.folder, 'waveshare-147')
        plan = contract.write_plan(self.folder)
        self.assertEqual([(p.start, p.erase_end) for p in plan],
                         [(0, 0x1000), (0x8000, 0x9000), (0xe000, 0xf000), (0x10000, 0x11000)])
        app = self.folder / 'R2500Adapter.ino.bin'
        app.write_bytes(b'x' * 0x300000)
        self.assertEqual(contract.write_plan(self.folder)[-1].erase_end, 0x310000)
        app.write_bytes(b'x' * 0x300001)
        self.rejected()

    def test_every_segment_is_bounded_and_nonempty(self):
        for _, name, capacity in contract.SEGMENTS:
            path = self.folder / name
            original = path.read_bytes()
            for size in (0, capacity + 1):
                with self.subTest(name=name, size=size):
                    path.write_bytes(b'x' * size)
                    self.rejected()
            path.write_bytes(original)

    def test_partition_schema_with_valid_checksum(self):
        for name, values in contract.PARTITIONS.items():
            for index in (0, 1, 2, 3, 4):
                with self.subTest(name=name, field=index):
                    changed = list(values)
                    changed[index] += 1
                    entries = list(contract.PARTITIONS.items())
                    entries[entries.index((name, values))] = (name, tuple(changed))
                    self.table.write_bytes(table_bytes(entries))
                    self.rejected()

    def test_duplicate_extra_missing_and_checksum(self):
        entries = list(contract.PARTITIONS.items())
        for bad in (entries + entries[:1], entries + [('extra', entries[0][1])], entries[1:]):
            self.table.write_bytes(table_bytes(bad))
            self.rejected()
        data = bytearray(table_bytes())
        for offset in (224 + 16, 260):  # checksum then trailing non-erased bytes
            corrupt = bytearray(data)
            corrupt[offset] ^= 1
            self.table.write_bytes(corrupt)
            self.rejected()

    def test_no_device_access_when_plan_invalid(self):
        self.table.write_bytes(table_bytes().ljust(0x5000, b'\xff'))
        self.manifest()
        args = ['manage_board.py', 'flash', '--board', 'waveshare-147', '--confirm-write',
                '--image-dir', str(self.folder)]
        with patch.object(sys, 'argv', args), patch.object(manage_board, 'device', return_value={}), \
                patch.object(manage_board, 'rom_port') as port:
            with self.assertRaises(ValueError):
                manage_board.main()
            port.assert_not_called()

    def test_extra_binary_cannot_become_an_implicit_segment(self):
        (self.folder / 'unexpected.bin').write_bytes(b'x')
        self.rejected()


if __name__ == '__main__':
    unittest.main()
