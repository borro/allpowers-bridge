"""Check real Mull outcomes and cross-run deduplication using synthetic subjects."""
import json
from pathlib import Path
import subprocess
import sys


def main():
    runner, reporter, executable, directory = sys.argv[1:]
    output = Path(directory)
    output.mkdir(parents=True, exist_ok=True)
    database = output / "control.sqlite"
    database.unlink(missing_ok=True)
    subprocess.run([executable, "strong"], check=True)
    if subprocess.run([executable, "fail"]).returncode == 0:
        raise RuntimeError("Broken baseline failure control")
    for mode in ("weak", "strong"):
        with (output / f"{mode}.log").open("w") as log:
            subprocess.run([runner, "--allow-surviving", "--timeout", "1000",
                            "--minimum-timeout", "1000",
                            "--ld-search-path", "/usr/lib/x86_64-linux-gnu",
                            "--reporters", "SQLite", "--report-name", "control",
                            "--report-dir", directory, executable, mode],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            subprocess.run([reporter, "--allow-surviving", "--reporters", "Elements",
                            "--report-name", mode, "--report-dir", directory, str(database)],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        data = json.loads((output / f"{mode}.json").read_text())
        mutants = [mutant for file in data["files"].values() for mutant in file["mutants"]]
        expected = {"cxx_ge_to_gt": "Survived" if mode == "weak" else "Killed",
                    "cxx_add_to_sub": "Survived", "cxx_sub_to_add": "Timeout"}
        actual = {mutant["mutatorName"]: mutant["status"] for mutant in mutants}
        if len(mutants) != 3 or actual != expected:
            raise RuntimeError(f"Unexpected {mode} control results: {actual}")
    print("Mull controls passed: killed, survived, timeout, baseline failure and cross-test merge")


if __name__ == "__main__":
    main()
