"""Discover product tests without importing a desktop or connecting to BLE."""
from pathlib import Path
import sys, unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests/python'))
suite = unittest.defaultTestLoader.discover(str(ROOT/'tests/python'))
if __name__ == '__main__':
    raise SystemExit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
