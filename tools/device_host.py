"""Pinned BLE picker diagnostics. Choosing requires the highlighted MAC explicitly."""
import argparse,json,sys,time
from pathlib import Path
from boards import BOARDS,device
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
import hid,serial
from serial.tools import list_ports
p=argparse.ArgumentParser();p.add_argument('action',choices=['list','menu','next','choose','close','watch'])
p.add_argument('--address');p.add_argument('--seconds',type=int,default=5);p.add_argument('--board',choices=BOARDS,required=True);args=p.parse_args()
assert 1<=args.seconds<=60
SERIAL=device(args.board)['serial']
def port():
    found=[p for p in list_ports.comports() if p.vid==0x303a and p.pid==0x4002 and p.serial_number==SERIAL]
    assert len(found)==1,'Expected pinned adapter CDC';return found[0].device
devices=[d for d in hid.enumerate(0x303a,0x4002) if d.get('serial_number')==SERIAL and d.get('usage_page')==0xff00]
assert len(devices)==1
d=hid.device();d.open_path(devices[0]['path'])
try:probe=bytes(d.get_feature_report(1,64))
finally:d.close()
assert len(probe)==64 and probe[:7]==b'\x01UPSDEV' and chr(probe[7]) in ('L','M','N','O','P','Q','R')
result={'version':chr(probe[7]),'action':args.action,'events':[]}
with serial.Serial(port(),115200,timeout=.1,write_timeout=2) as con:
    def send(command):con.write(command.encode()+b'\n');con.flush()
    def collect(seconds,until=None):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            try:line=con.readline().decode(errors='replace').strip()
            except serial.SerialException:
                result['events'].append({'usb_restarted':True});return
            if not line:continue
            try:row=json.loads(line)
            except json.JSONDecodeError:continue
            result['events'].append(row)
            if until and until(row):return
    send('STATUS');collect(2,lambda r:'picker_open' in r)
    initial=next((r for r in reversed(result['events']) if 'picker_open' in r and 'picker_selftest' in r),None)
    assert initial and initial['picker_selftest'],initial
    if args.action=='choose':
        assert args.address and initial['picker_open'] and not initial['picker_back']
        assert initial['picker_highlight']==args.address,'Highlighted device differs; no selection sent'
        send('SELECT');collect(args.seconds)
    elif args.action=='list':send('DEVICES');collect(3,lambda r:r.get('devices_done'))
    elif args.action=='menu':send('PICKER');collect(args.seconds)
    elif args.action=='next':
        assert initial['picker_open'];send('NEXT');collect(1)
    elif args.action=='close':send('PICKER-CLOSE');collect(1)
    else:collect(args.seconds)
reports=ROOT/'.local/reports';reports.mkdir(parents=True,exist_ok=True)
out=reports/f'{args.board}-devices-{args.action}-{result["version"]}-{time.strftime("%Y%m%d-%H%M%S")}.json'
out.write_text(json.dumps(result,indent=2))
states=[r for r in result['events'] if 'picker_selftest' in r]
print(json.dumps({'file':str(out),'initial':initial,'final':states[-1] if states else None,'devices':[r for r in result['events'] if r.get('ble_device')]},indent=2))
