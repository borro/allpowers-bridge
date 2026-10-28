"""Offline license inventory: no SDK download, compiler or device access."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'cmake'))
from firmware_notices import Notices, linked_inputs


class FirmwareNoticesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.notices = Notices()
        for name in ('one', 'two'):
            self.notices.component(name, [name + '.a'])

    def test_shared_license_and_distinct_copyright_survive(self):
        terms = b'Permission is hereby granted.\nFull license terms.\n'
        for name in ('one', 'two'):
            self.notices.document(name, 'MIT', terms, name + '/LICENSE')
            source = self.root / (name + '.c')
            source.write_text('/* Copyright Alice; permission is hereby granted */\n'
                              f'/* Copyright {name}; all rights reserved */\n')
            self.notices.headers(name, self.root, [source])
        self.notices.write(self.root / 'licenses')
        files = list((self.root / 'licenses').glob('*.txt'))
        self.assertEqual(len(files), 1)
        self.assertEqual(files[0].read_bytes(), terms)
        mapping = json.loads((self.root / 'licenses/components.json').read_text())
        self.assertEqual(mapping['one']['documents'], mapping['two']['documents'])
        notices = (self.root / 'licenses/NOTICES.md').read_text()
        for who in ('Alice', 'one', 'two'):
            self.assertEqual(notices.count('/* Copyright ' + who + ';'), 1)
        self.assertIn('one/one.c', notices)
        self.assertIn('two/two.c', notices)
        first = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in (self.root / 'licenses').iterdir()}
        self.notices.write(self.root / 'licenses')
        self.assertEqual(first, {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (self.root / 'licenses').iterdir()})

    def test_only_selected_sources_and_target_headers_are_collected(self):
        for name, author in (('selected.c', 'used'), ('unused.c', 'unused'),
                             ('include/shared.h', 'header'), ('esp32s3/selected.c', 's3'),
                             ('esp32c3/selected.c', 'other-chip'), ('tests/selected.c', 'test')):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('/* Copyright ' + author + ' */\n')
        self.notices.selected_sources('one', self.root, {'selected.c.obj'})
        texts = '\n'.join(x['text'] for x in self.notices.notices.values())
        for author in ('used', 'header', 's3'):
            self.assertIn('Copyright ' + author, texts)
        for author in ('unused', 'other-chip', 'test'):
            self.assertNotIn('Copyright ' + author, texts)
        with self.assertRaisesRegex(ValueError, 'Unmatched linked source'):
            self.notices.selected_sources('one', self.root, {'missing.c.obj'})

    def test_missing_or_empty_license_stops_packaging(self):
        with self.assertRaises(FileNotFoundError):
            self.notices.file('one', 'MIT', self.root / 'missing', 'missing')
        with self.assertRaisesRegex(ValueError, 'Empty license'):
            self.notices.document('one', 'MIT', b'', 'empty')
        with self.assertRaisesRegex(ValueError, 'Missing license'):
            self.notices.write(self.root / 'licenses')

    def test_archive_inventory_ignores_discarded_objects_and_local_paths(self):
        text = 'C:/private/build/core.a(main.cpp.o)\n'
        text += ''.join(f'C:/private/lib/lib{i}.a(source{i}.c.obj)\n' for i in range(20))
        text += 'C:/private/compile/libraries/BLE/BLEClient.cpp.o\n'
        text += 'Discarded input sections\nC:/private/lib/unused.a(test.c.obj)\n'
        archives, libraries = linked_inputs(text)
        self.assertNotIn('unused.a', archives)
        self.assertEqual(libraries, {'BLE': {'BLEClient.cpp.o'}})
        self.assertEqual(archives['core.a'], {'main.cpp.o'})
        self.assertFalse(any('private' in name for name in archives))
        for bad in ('', 'Discarded input sections', 'core.a(main.o)\n'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                linked_inputs(bad)


if __name__ == '__main__':
    unittest.main()
