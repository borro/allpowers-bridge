"""Offline integrity checks shared by the optional board-maintenance tools."""
import hashlib, json, struct
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def partition_check(path):
    entries = {}
    table = path.read_bytes()
    for offset in range(0, len(table), 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from('<HBBII16sI', table, offset)
        if magic != 0x50aa: break
        entries[label.rstrip(b'\0').decode()] = [address, size]
    contract = json.loads((ROOT/'tests/fixtures/usb-contract.json').read_text())
    for name, expected in contract['partitions'].items():
        if entries.get(name) != expected: raise RuntimeError('Partition changed: '+name)
    return entries
