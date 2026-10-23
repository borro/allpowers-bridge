"""Exercise the actual shared library, including real captured packets."""
import ctypes as C,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(Path(__file__).resolve().parent))
from native_bridge import Core,Snapshot
MAIN=bytes.fromhex('a565b1000108010264000000e696006f')
MAINS=bytes.fromhex('a565b10001060300010000030275')
BATTERY=bytes.fromhex('a565b1000106030001ffff030275')
class CoreTests(unittest.TestCase):
    def setUp(self):self.c=Core(3)
    def tearDown(self):self.c.close()
    def test_real_frame_units_source_and_age(self):
        self.assertTrue(self.c.receive(MAIN,100));self.assertFalse(self.c.snapshot(100)['fresh'])
        self.assertTrue(self.c.receive(MAINS,200));s=self.c.snapshot(201)
        self.assertEqual((s['soc'],s['input_w'],s['output_w'],s['runtime_s'],s['source'],s['fresh']),(100,0,230,2304000,2,1))
        self.assertFalse(self.c.snapshot(15201)['fresh'])
        self.assertTrue(self.c.receive(BATTERY,15202));self.assertEqual(self.c.snapshot(15202)['source'],1)
    def test_unknown_source_invalidates_mains(self):
        self.c.receive(MAIN,100);self.c.receive(MAINS,100)
        unknown=bytearray(MAINS);unknown[9]=1;unknown[-1]^=1
        self.assertTrue(self.c.receive(unknown,101));s=self.c.snapshot(101)
        self.assertEqual(s['source'],0);self.assertFalse(s['fresh'])
    def test_faults_leave_values_and_age_unchanged(self):
        self.c.receive(MAIN,100)
        for n in range(len(MAIN)):self.assertFalse(self.c.receive(MAIN[:n],200))
        for i in range(len(MAIN)):
            b=bytearray(MAIN);b[i]^=1;self.assertFalse(self.c.receive(b,200))
        s=self.c.snapshot(201);self.assertEqual(s['soc'],100);self.assertEqual(s['main_age_ms'],101);self.assertEqual(s['invalid_frames'],32)
    def test_models_do_not_invent_source_or_poll(self):
        self.c.reset(0);self.c.receive(MAIN,100);s=self.c.snapshot(101)
        self.assertEqual(s['model'],2);self.assertEqual(s['source'],0);self.assertEqual(self.c.poll(),b'');self.assertFalse(s['known']&64)
        self.c.reset(1);self.assertTrue(self.c.receive(bytes.fromhex('0f2a006400c800070441'),100));s=self.c.snapshot(101)
        self.assertEqual((s['soc'],s['runtime_s'],s['ac_output'],s['usb_output'],s['dc_output']),(42,65340,1,1,1));self.assertEqual(self.c.poll(),b'')
    def test_session_reset_and_timer_wrap(self):
        self.c.receive(MAIN,0xfffffff0);self.c.receive(MAINS,0xfffffff0);self.assertTrue(self.c.snapshot(30)['fresh'])
        self.c.reset(3);s=self.c.snapshot(31);self.assertFalse(s['main_seen']);self.assertFalse(s['fresh']);self.assertEqual(s['valid_frames'],0)
    def test_catalog_and_abi_bounds(self):
        self.assertEqual(len(self.c.catalog()),17);self.assertEqual(self.c.hint('AP R2500 V2.0'),3);self.assertEqual(self.c.hint('unknown'),0)
        self.assertEqual(self.c.poll(),bytes.fromhex('a565b1000106010000000000'))
        out=Snapshot();self.assertFalse(self.c.lib.ap_get_snapshot(self.c.handle,0,C.byref(out),C.sizeof(out)-1))
        self.assertFalse(self.c.lib.ap_get_snapshot(None,0,C.byref(out),C.sizeof(out)))
    def test_recorded_session(self):
        notifications=[bytes.fromhex(h) for h in json.loads((ROOT/'tests/fixtures/notifications.json').read_text())]
        accepted=sum(self.c.receive(b,i*100) for i,b in enumerate(notifications));self.assertGreater(accepted,20)
        self.assertEqual(self.c.snapshot(len(notifications)*100)['valid_frames'],accepted)
if __name__=='__main__':unittest.main()
