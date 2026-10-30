"""Seed the generated libFuzzer corpora; never delete discoveries from earlier runs."""
from pathlib import Path
import struct
import sys
import zlib


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
    }
    for subject, inputs in cases.items():
        (root / subject / "corpus").mkdir(parents=True, exist_ok=True)
        (root / subject / "crashes").mkdir(parents=True, exist_ok=True)
        for index, data in enumerate(inputs):
            (root / subject / "corpus" / f"seed-{index}").write_bytes(data)


if __name__ == "__main__":
    seed(Path(sys.argv[1]))
