"""Public portability, ABI and recovery invariants; no USB access."""
import hashlib, json, os, sys, tempfile, unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from boards import device

class RepositoryTests(unittest.TestCase):
    def test_usb_and_recovery_contract(self):
        contract=json.loads((ROOT/'tests/fixtures/usb-contract.json').read_text())
        sketch=ROOT/'firmware/R2500Adapter'
        self.assertEqual(hashlib.sha256((sketch/'UpsDescriptor.h').read_bytes()).hexdigest(),contract['descriptor_sha256'])
        source=(sketch/'R2500Adapter.ino').read_text()
        self.assertIn('Console.enableReboot(false)',source)
        self.assertIn('BOOT-R2500!!',source)
        self.assertIn('if(!targetLoaded){pickerOpen=true',source)
        self.assertNotIn('14:63:93',source)
        self.assertIn('#define AP_ABI_VERSION 1',(ROOT/'core/native/api.h').read_text())
    def test_example_identity_cannot_select_hardware(self):
        with self.assertRaises(ValueError):device('waveshare-147',ROOT/'config/devices.example.json')
    def test_identity_requires_matching_serial(self):
        with tempfile.TemporaryDirectory() as tmp:
            config=Path(tmp)/'devices.json'
            config.write_text(json.dumps({'pocket-096':{'mac':'01:23:45:67:89:ab','serial':'wrong'}}))
            with self.assertRaises(ValueError):device('pocket-096',config)
            config.write_text(json.dumps({'pocket-096':{'mac':'01:23:45:67:89:ab','serial':'R2500-0123456789AB'}}))
            self.assertEqual(device('pocket-096',config)['mac'],'01:23:45:67:89:ab')

if __name__=='__main__':unittest.main()
