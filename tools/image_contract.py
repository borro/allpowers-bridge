"""Offline integrity checks shared by the optional board-maintenance tools."""
import hashlib, json, struct
from dataclasses import dataclass
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

# Complete, deliberately fixed on-device layout. Changing a type or flag is a
# migration, even if the labels and byte ranges remain the same.
PARTITIONS = {
    'nvs': (1, 2, 0x9000, 0x5000, 0),
    'otadata': (1, 0, 0xe000, 0x2000, 0),
    'app0': (0, 0x10, 0x10000, 0x300000, 0),
    'app1': (0, 0x11, 0x310000, 0x300000, 0),
    'ffat': (1, 0x81, 0x610000, 0x9c0000, 0),
    'history': (1, 0x40, 0xfd0000, 0x20000, 0),
    'coredump': (1, 3, 0xff0000, 0x10000, 0),
}
SEGMENTS = ((0, 'R2500Adapter.ino.bootloader.bin', 0x8000),
            (0x8000, 'R2500Adapter.ino.partitions.bin', 0x1000),
            (0xe000, 'boot_app0.bin', 0x2000),
            (0x10000, 'R2500Adapter.ino.bin', 0x300000))
SECTOR = 0x1000
HISTORY_CAPABILITY = 'ALLPOWERS_HISTORY_R12_W2_S32'
HISTORY_CONTRACT = {'schema': 1, 'read_versions': [1, 2], 'write_version': 2,
                    'record_bytes': 32, 'capability': HISTORY_CAPABILITY}


@dataclass(frozen=True)
class WriteOperation:
    name: str
    start: int
    end: int
    erase_start: int
    erase_end: int

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def partition_check(path):
    entries = {}
    table = path.read_bytes()
    if not table or len(table) > SECTOR or len(table) % 32:
        raise ValueError('Partition table must fit its sector and contain whole entries')
    checksum = False
    for offset in range(0, len(table), 32):
        record = table[offset:offset + 32]
        if record[:2] == b'\xeb\xeb':
            if record[2:16] != b'\xff' * 14 or record[16:] != hashlib.md5(table[:offset]).digest():
                raise ValueError('Partition table checksum mismatch')
            if table[offset + 32:] != b'\xff' * (len(table) - offset - 32):
                raise ValueError('Unexpected data after partition checksum')
            checksum = True
            break
        magic, kind, subtype, address, size, label, flags = struct.unpack_from('<HBBII16sI', table, offset)
        if magic != 0x50aa:
            raise ValueError('Invalid partition entry or missing checksum')
        name_bytes, separator, padding = label.partition(b'\0')
        if not separator or padding.strip(b'\0'):
            raise ValueError('Invalid partition label')
        name = name_bytes.decode('ascii')
        if name in entries or PARTITIONS.get(name) != (kind, subtype, address, size, flags):
            raise ValueError('Partition schema mismatch: ' + name)
        entries[name] = [address, size]
    if not checksum or set(entries) != set(PARTITIONS):
        raise ValueError('Incomplete partition schema or checksum')
    contract = json.loads((ROOT/'tests/fixtures/usb-contract.json').read_text())
    for name, expected in contract['partitions'].items():
        if entries.get(name) != expected: raise RuntimeError('Partition changed: '+name)
    return entries


def write_plan(folder):
    """Validate all write and erase ranges without importing/opening a device."""
    partition_check(folder / 'R2500Adapter.ino.partitions.bin')
    operations = []
    # Only app0 and otadata are intentionally replaced. All other partition
    # contents, including the inactive application, are protected.
    protected = [(name, entry[2], entry[2] + entry[3])
                 for name, entry in PARTITIONS.items() if name not in ('app0', 'otadata')]
    for start, name, capacity in SEGMENTS:
        size = (folder / name).stat().st_size
        if not 0 < size <= capacity:
            raise ValueError('Firmware segment exceeds its allowed range: ' + name)
        end = start + size
        erase_start = start // SECTOR * SECTOR
        erase_end = (end + SECTOR - 1) // SECTOR * SECTOR
        for partition, lower, upper in protected:
            if erase_start < upper and lower < erase_end:
                raise ValueError('Erase range overlaps protected partition: ' + partition)
        if any(erase_start < previous.erase_end and previous.erase_start < erase_end
               for previous in operations):
            raise ValueError('Firmware erase ranges overlap')
        operations.append(WriteOperation(name, start, end, erase_start, erase_end))
    return tuple(operations)


def history_contract(manifest, application):
    """Fail closed for images unable to preserve the retained archive on rollback.

    This is a capability/integrity contract, not a signature/authenticity claim.
    The marker is emitted by the production STATUS path, so metadata cannot be
    retrofitted onto an unchanged released AHP1-only application binary.
    """
    metadata = manifest.get('history_storage')
    if metadata != HISTORY_CONTRACT or not isinstance(metadata, dict) or \
            any(type(metadata.get(key)) is not int for key in ('schema', 'write_version', 'record_bytes')) or \
            any(type(value) is not int for value in metadata.get('read_versions', [])):
        raise ValueError('AHP1/AHP2-compatible image metadata is required; unsafe downgrade refused')
    if (HISTORY_CAPABILITY.encode('ascii') + b'\0') not in application.read_bytes():
        raise ValueError('Application does not contain the declared archive capability')
    return manifest['history_storage']


def rollback_check(rollback, board, validate):
    """Require a pinned application AND its complete compatible package manifest."""
    if not rollback or not rollback.get('manifest'):
        raise ValueError('Register a verified AHP1/AHP2-compatible rollback package manifest')
    backup = Path(rollback['path'])
    if sha(backup) != rollback.get('sha256'):
        raise ValueError('Rollback image checksum mismatch')
    path = Path(rollback['manifest'])
    manifest = validate(path.parent, board)
    if path.resolve() != (path.parent / 'manifest.json').resolve():
        raise ValueError('Rollback metadata must be the package manifest.json')
    if manifest['files'].get('R2500Adapter.ino.bin') != rollback['sha256']:
        raise ValueError('Rollback package metadata is not bound to the registered application')
    return manifest
