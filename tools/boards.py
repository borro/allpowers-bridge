"""Public board traits and explicitly registered local device identities."""
import json, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BOARDS={'waveshare-147':{'define':1,'width':172,'height':320,'screens':3},
        'pocket-096':{'define':2,'width':80,'height':160,'screens':6}}

def device(board, config=None):
    path=Path(config) if config else ROOT/'.local/devices.json'
    if board not in BOARDS:raise ValueError('Unknown board profile')
    if not path.exists():raise RuntimeError('Copy config/devices.example.json to .local/devices.json and register your adapter')
    value=json.loads(path.read_text(encoding='utf-8')).get(board,{})
    mac=value.get('mac','').lower()
    if not re.fullmatch(r'(?:[0-9a-f]{2}:){5}[0-9a-f]{2}',mac) or mac=='00:00:00:00:00:00':
        raise ValueError('Register the actual adapter MAC; examples are not valid identities')
    expected='R2500-'+mac.replace(':','').upper()
    if value.get('serial')!=expected:raise ValueError('USB serial does not match the registered MAC')
    return {**value,'mac':mac}

def rom_port(identity):
    from serial.tools import list_ports
    found=[p for p in list_ports.comports() if p.vid==0x303a and p.pid==0x1001
           and (p.serial_number or '').lower()==identity['mac']]
    if len(found)!=1:raise RuntimeError(f'Expected one registered ESP32-S3 ROM port, found {len(found)}. Use physical BOOT/RESET.')
    return found[0].device
