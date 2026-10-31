"""Portable corpus staging checks; no fuzzing or device access."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "seed_corpus", Path(__file__).resolve().parents[1] / "fuzz/seed_corpus.py")
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


class SeedCorpusTests(unittest.TestCase):
    def test_checkout_seeds_preserve_bytes_and_local_discoveries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            corpus.seed(root)
            for subject in ("archive", "telemetry"):
                self.assertTrue(list((root / subject / "corpus").glob("seed-*")))
                self.assertTrue((root / subject / "crashes").is_dir())
            for subject, limit in (("ble", 1024), ("hid_wire", 2048), ("hid_server", 2048)):
                stored = list((corpus.CORPUS_ROOT / subject).glob("*.hex"))
                self.assertTrue(stored, subject)
                destination = root / subject / "corpus"
                self.assertTrue((destination / "seed-0").is_file())
                for source in stored:
                    data = bytes.fromhex(source.read_text(encoding="ascii"))
                    self.assertEqual(source.stem, hashlib.sha256(data).hexdigest())
                    self.assertGreater(len(data), 0)
                    self.assertLessEqual(len(data), limit)
                    self.assertEqual((destination / f"corpus-{source.stem}").read_bytes(), data)
                (destination / "local-discovery").write_bytes(b"\x00\r\n\xff")
            before = {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()}
            corpus.seed(root)
            after = {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()}
            self.assertEqual(before, after)

    def test_corrupted_seed_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stored = root / "stored" / "ble"
            stored.mkdir(parents=True)
            source = stored / (hashlib.sha256(b"a").hexdigest() + ".hex")
            with patch.object(corpus, "CORPUS_ROOT", stored.parent):
                source.write_text("62\n", encoding="ascii")
                with self.assertRaisesRegex(ValueError, "checksum"):
                    corpus.seed(root / "output")
                source.write_text("not hex", encoding="ascii")
                with self.assertRaises(ValueError):
                    corpus.seed(root / "output")


if __name__ == "__main__":
    unittest.main()
