"""Seed the generated libFuzzer corpora; never delete discoveries from earlier runs."""
from pathlib import Path
import sys


def seed(root):
    # Synthetic telemetry and protocol commands: no device identities/captures.
    cases = {
        "ble": [bytes.fromhex("a565b1000108010264000000e696006f"),
                bytes.fromhex("a565b10001060300010000030275"),
                bytes([15, 100, 0, 1, 0, 2, 0, 3, 0, 4]),
                bytes(10), bytes(513), b"\xa5\x65"],
        "cdc": [b"STATUS\n", b"HISTORY\n", b"HISTORY-READ 1 2880\n",
                b"HISTORY-EXPORT 4294967295 " + b"f" * 32 + b"\n",
                b"HISTORY-EXPORT 1\nSTATUS\r\n", b"0" * 80 + b"\nSTATUS\n",
                b"HISTORY\x00STATUS\n", b"HISTORY-READ 4294967296 1\n"],
    }
    for subject, inputs in cases.items():
        (root / subject / "corpus").mkdir(parents=True, exist_ok=True)
        (root / subject / "crashes").mkdir(parents=True, exist_ok=True)
        for index, data in enumerate(inputs):
            (root / subject / "corpus" / f"seed-{index}").write_bytes(data)


if __name__ == "__main__":
    seed(Path(sys.argv[1]))
