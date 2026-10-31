"""Seed the generated libFuzzer corpora; never delete discoveries from earlier runs."""
from pathlib import Path
import hashlib
import struct
import sys
import zlib

CORPUS_ROOT = Path(__file__).resolve().parent / "corpus"
RETAINED_SUBJECTS = ("ble", "hid_wire", "hid_server")


def seed(root):
    # Synthetic telemetry and protocol commands: no device identities/captures.
    requests = []
    for opcode in [1, 2, 3, 4, 5, 7]:
        report = bytearray(63)
        report[0:2] = bytes([2, opcode])
        report[2:18] = bytes(range(1, 17))
        struct.pack_into("<Q", report, 18, 0xfedcba9876543210)
        if 3 <= opcode <= 5:
            report[26:42] = bytes(range(17, 33))
        if opcode == 7:
            struct.pack_into("<II", report, 46, 1800000000, 100)
        struct.pack_into("<I", report, 59, zlib.crc32(report[:59]))
        requests.append(bytes(report))
    cases = {
        "ble": [bytes.fromhex("a565b1000108010264000000e696006f"),
                bytes.fromhex("a565b10001060300010000030275"),
                bytes([15, 100, 0, 1, 0, 2, 0, 3, 0, 4]),
                bytes(10), bytes(513), b"\xa5\x65"],
        "hid_wire": requests + [bytes(63), bytes(range(64)), bytes(range(256)),
                     bytes([255]) * 2048, b"\x01\x02\x03"],
        "hid_server": [bytes(range(256)), bytes([0, 1, 2, 3, 4, 5]) * 20,
                       bytes([255]) * 64, bytes(64),
                       # Two open snapshots, third reader BUSY, repeat/page/close/reopen.
                       bytes([0,2,0,0, 1,2,0,0, 2,2,0,0, 0,2,0,0,
                              0,3,8,0, 1,3,16,0, 0,4,0,0, 2,2,0,0]),
                       bytes([0,2,0,0, 0,3,0,29, 0,3,0,30, 1,2,0,0,
                              1,3,0,179, 1,3,0,1])],
        "archive": [],
        "telemetry": [],
    }
    # Archive header: prior epoch, storage faults, starting sequence, pointer flags.
    # Eight-byte actions: append/torn write/torn erase/reboot/corrupt/read/read fault/bad record.
    for start in range(8):
        for torn in (0, 1, 15, 31, 32):
            header = bytes([16, 8, 16, 0, 0, start, 0, 0])
            actions = bytes([1, 0, 0, 0, 1, 50, 1, torn,
                             3, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 1, 75, 1, 0,
                             5, 1, 0, 0, 0, 0, 0, 0,
                             2, 0, 0, 0, 1, 75, 1, torn,
                             3, 0, 0, 0, 0, 0, 0, 0])
            cases["archive"].append(header + actions)
    cases["archive"] += [bytes(64), bytes([255]) * 64, bytes(range(256))]
    for prior in (0, 0xffffffff):
        for faults in range(16):
            cases["archive"].append(struct.pack("<IBBBB", prior, faults, 1, 0, 0))
    cases["archive"].append(bytes([16, 8, 16, 0, 0, 1, 0, 0,
                                    4, 0, 0, 1, 1, 28, 0, 0,
                                    6, 0, 0, 0, 0, 0, 0, 0,
                                    7, 0, 0, 0, 0, 0, 0, 0]))
    for fault in (4, 8):
        cases["archive"].append(bytes([16, 8, 16, 0, 0, 1, 0, 0,
                                        0, fault, 0, 0, 1, 50, 1, 0,
                                        3, 0, 0, 0, 0, 0, 0, 0]))
    # Telemetry header: initial uint32 clock and station model; then 16-byte events.
    # Main, settings from mains/battery/unknown, expiry, reset, UTC and UPS state.
    for model in range(4):
        for now in (0, 0xfffffff0):
            header = struct.pack("<IB", now, model) + bytes(11)
            events = []
            for op, jump, a, b in [(0, 0, 3, 100), (1, 0, 16, 0), (1, 1, 16, 1),
                                   (1, 0, 16, 2), (7, 4, 0, 0), (5, 0, 0, 0),
                                   (6, 0, 0, 1), (6, 6, 0, 1), (4, 0, 3, 0)]:
                events.append(bytes([op, jump, a, b, 1, 1, 1, 4, 80, 10, 0, 30, 0, 0, 0, 16]))
            cases["telemetry"].append(header + b"".join(events))
    cases["telemetry"] += [bytes(64), bytes([255]) * 256, bytes(range(256))]
    cases["telemetry"].append(bytes([0, 0, 0, 0, 1]) + bytes(11) +
                              bytes([2, 0, 0, 0, 0, 1, 0, 2, 0, 3, 0, 0, 0, 0, 0, 10]))
    cases["telemetry"].append(bytes(16) + bytes([5, 0, 1, 1]) +
                              struct.pack("<II", 0xffffffff, 0) + bytes([1, 1, 0, 0]))
    for malformed in range(14):
        cases["telemetry"].append(bytes(8) + bytes([255]) * 4 + bytes(4) +
                                  bytes([6, 0, 0, 1, 1, 1, 1, 4, 80, 10, 0, 30, 0, 0, malformed, 0]))
    for mode in (0, 8, 16, 24, 32, 64, 96):
        for size in (0, 16, 32, 48, 64, 80, 96):
            cases["hid_server"].append(bytes([size, 2 + mode, 0, 0,
                                              size, 3, 0, 0, size, 3, 128, 0,
                                              size, mode, 0, 0, size, 6, 0, 0,
                                              size | 128, 6, 0, 0]))
    for subject, inputs in cases.items():
        (root / subject / "corpus").mkdir(parents=True, exist_ok=True)
        (root / subject / "crashes").mkdir(parents=True, exist_ok=True)
        for index, data in enumerate(inputs):
            (root / subject / "corpus" / f"seed-{index}").write_bytes(data)
        if subject not in RETAINED_SUBJECTS:
            continue
        for source in sorted((CORPUS_ROOT / subject).iterdir()):
            data = bytes.fromhex(source.read_text(encoding="ascii"))
            if source.suffix != ".hex" or source.stem != hashlib.sha256(data).hexdigest():
                raise ValueError(f"Corpus checksum/name mismatch: {source.name}")
            (root / subject / "corpus" / f"corpus-{source.stem}").write_bytes(data)


if __name__ == "__main__":
    seed(Path(sys.argv[1]))
