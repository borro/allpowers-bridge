"""Exercise real Linux analyzers against passing and failing inputs, offline."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def check(command, expect_failure=False, diagnostic=None, env=None):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=60, env=env)
    output = result.stdout + result.stderr
    if bool(result.returncode) != expect_failure or (diagnostic and diagnostic not in output):
        raise RuntimeError(f"Control failed: {command[0]} ({result.returncode}), expected {diagnostic}\n{output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("linux", "workflow"))
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--tidy")
    parser.add_argument("--cppcheck")
    parser.add_argument("--actionlint")
    parser.add_argument("--shellcheck")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="allpowers-analysis-control-") as directory:
        work = Path(directory)
        if args.kind == "linux":
            for broken in (False, True):
                source = work / "control.cpp"
                source.write_text("int f() { int *p = nullptr; return *p; }\n" if broken else
                                  "int f() { int value = 3; return value; }\n", encoding="utf-8")
                check([args.tidy, source, f"--config-file={args.root / '.clang-tidy'}", "--", "-std=c++23"],
                      broken, "clang-analyzer-core.NullDereference" if broken else None)
                check([args.cppcheck, "--std=c++23", "--error-exitcode=2", source],
                      broken, "nullPointer" if broken else None)
        else:
            base = "name: control\non: push\njobs:\n  check:\n    runs-on: ubuntu-24.04\n"
            source = work / "control.yml"
            command = [args.actionlint, "-no-color", "-shellcheck", args.shellcheck, "-pyflakes=", source]
            env = {**os.environ, "SHELLCHECK_OPTS": "--severity=warning"}
            for suffix, diagnostic in [
                ('    steps:\n      - run: echo "ok"\n', None),
                ('    needs: missing\n    steps:\n      - run: echo "ok"\n', "job-needs"),
                ('    steps:\n      - shell: bash\n        run: cat $(find . -name "*.txt")\n', "SC2046")]:
                source.write_text(base + suffix, encoding="utf-8")
                check(command, diagnostic is not None, diagnostic, env)
    print(f"{args.kind}: passing and failing analyzer controls passed")


if __name__ == "__main__":
    main()
