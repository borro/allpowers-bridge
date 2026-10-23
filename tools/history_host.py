"""Bounded history export, clock sync and display control for the pinned adapter."""
import argparse,json,struct,sys,time,zlib
from pathlib import Path
from boards import BOARDS,device
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
import hid,serial
from serial.tools import list_ports
p=argparse.ArgumentParser();p.add_argument('action',choices=['status','tail','time','screen','watch','diagnostics']);p.add_argument('--count',type=int,default=10);p.add_argument('--screen',type=int,choices=range(6),default=0);p.add_argument('--seconds',type=int,default=5);p.add_argument('--board',choices=BOARDS,required=True);args=p.parse_args()
assert 1<=args.count<=2880 and 1<=args.seconds<=120
SERIAL=device(args.board)['serial']
devices=[d for d in hid.enumerate(0x303a,0x4002) if d.get('serial_number')==SERIAL and d.get('usage_page')==0xff00]
ports=[d for d in list_ports.comports() if d.vid==0x303a and d.pid==0x4002 and d.serial_number==SERIAL]
assert len(devices)==len(ports)==1,'Expected pinned history adapter'
d=hid.device();d.open_path(devices[0]['path'])
try:probe=bytes(d.get_feature_report(1,64))
finally:d.close()
assert len(probe)==64 and probe[:7]==b'\x01UPSDEV' and chr(probe[7]) in ('H','I','J','K','L','M','N','O','P','Q','R')
result={'version':chr(probe[7]),'action':args.action,'events':[],'records':[]}
with serial.Serial(ports[0].device,115200,timeout=.1,write_timeout=2) as con:
    def send(command):con.write(command.encode()+b'\n');con.flush()
    def collect(seconds,until=None):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            line=con.readline().decode(errors='replace').strip()
            if not line:continue
            try:row=json.loads(line)
            except json.JSONDecodeError:continue
            result['events'].append(row)
            if row.get('history_record'):
                body=struct.pack('<IIIIIHHHBB',0x31504841,row['sequence'],row['session'],row['uptime_s'],row['epoch'],row['charge_w'],row['output_w'],row['runtime_minutes'],row['soc'],row['flags'])
                assert zlib.crc32(body)==row['crc'],'History CRC mismatch'
                result['records'].append(row)
            if until and until(row):return
    send('STATUS');collect(3,lambda r:r.get('history_ready') is True)
    info=next((r for r in reversed(result['events']) if 'history_ready' in r),None)
    assert info and info['history_ready'] and info['history_selftest'],info
    result['initial_history']=info
    if args.action=='tail':
        count=min(args.count,info['history_records']);first=(info['history_latest']-count+1)&0xffffffff
        if first==0:first=1
        if count:send(f'HISTORY-READ {first} {count}');collect(30,lambda r:r.get('history_dump_done'))
        assert not count or result['records'],'No stored history returned'
    elif args.action=='time':
        send(f'TIME {int(time.time())}');collect(3,lambda r:r.get('time_set'))
        assert any(r.get('time_set') for r in result['events'])
    elif args.action=='screen':
        assert args.screen<2 or chr(probe[7]) in ('P','Q','R'),'Diagnostics page requires firmware P'
        assert args.screen<BOARDS[args.board]['screens'];send(f'SCREEN {args.screen}');collect(3,lambda r:r.get('screen')==args.screen)
        assert any(r.get('screen')==args.screen for r in result['events'])
    elif args.action=='diagnostics':
        assert chr(probe[7]) in ('P','Q','R'),'Diagnostic export requires firmware P'
        send('DIAG');collect(3,lambda r:r.get('diagnostic'))
        report=next((r for r in reversed(result['events']) if r.get('diagnostic')),None)
        assert report and len(report['rows'])==26,'No complete diagnostic view'
        result['diagnostic']=report
    elif args.action=='watch':collect(args.seconds)
    else:collect(1)
result['final_history']=next((r for r in reversed(result['events']) if 'history_ready' in r),info)
reports=ROOT/'.local/reports';reports.mkdir(parents=True,exist_ok=True)
out=reports/f'{args.board}-history-{args.action}-{result["version"]}-{time.strftime("%Y%m%d-%H%M%S")}.json'
out.write_text(json.dumps(result,indent=2))
print(json.dumps({'file':str(out),'version':result['version'],'action':args.action,'initial':info,'final':result['final_history'],'records':result['records']},indent=2))
