"""Run already-built native tests under Mull and summarize its merged report."""

import argparse
from collections import Counter
from html import escape
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def run(command, log, cwd, timeout=1800):
    print(f"Mutation check: {log.stem}", flush=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}\n"
                           + log.read_text(encoding="utf-8", errors="replace")[-6000:])


def summarize(report, root):
    counts = Counter()
    areas = Counter()
    sources = Counter()
    survivors = []
    for filename, details in report["files"].items():
        path = Path(filename)
        if path.is_absolute():
            path = path.relative_to(root)
        name = path.as_posix()
        if not (name.startswith("core/") or name.startswith("firmware/R2500Adapter/")
                or name == "apps/windows/ups_state.h"):
            raise ValueError(f"Unexpected mutation source: {name}")
        for mutant in details["mutants"]:
            status = mutant["status"]
            if status not in {"Killed", "Survived", "Timeout", "NoCoverage"}:
                raise ValueError(f"Incomplete or erroneous mutation: {name}: {status}")
            counts[status] += 1
            areas[name.split("/")[0]] += 1
            sources[name] += 1
            if status == "Survived":
                line = mutant["location"]["start"]["line"]
                survivors.append(f"- `{name}:{line}` — {mutant['mutatorName']}")
    if not counts.total() or not areas["core"] or not areas["firmware"]:
        raise ValueError("Missing mutations in core or host firmware; result is not a valid baseline")
    if not sources["core/native/api.cpp"] or not sources["apps/windows/ups_state.h"]:
        raise ValueError("Missing shared-library ABI or portable UPS mutations")
    detected = counts["Killed"] + counts["Timeout"]
    score = 100 * detected / counts.total()
    text = (f"## Host mutation testing\n\n"
            f"Mutation score: **{score:.2f}%** ({detected}/{counts.total()}). "
            "Informational; no score threshold.\n\n"
            "| Result | Mutants |\n| --- | ---: |\n"
            + "".join(f"| {status} | {counts[status]} |\n" for status in
                      ("Killed", "Survived", "Timeout", "NoCoverage"))
            + "\nScore = (killed + timeout) / all mutants, including uncovered code. "
            "Mull merges duplicate mutations across test programs.\n\n"
            "Scope: core, host-tested firmware and the portable UPS adapter. "
            "Built-in product self-tests are included; Qt and device execution are not measured.\n\n"
            "### Surviving mutations (first 30)\n\n"
            + ("\n".join(sorted(survivors)[:30]) if survivors else "None.")
            + "\n\nSee the HTML/JSON artifact for every mutation and its source.\n")
    return text


def standalone_html(report):
    """Mull's default HTML fetches a CDN script and JSON; this works offline."""
    parts = ['<!doctype html><html lang="en"><meta charset="utf-8">',
             '<title>ALLPOWERS mutation report</title>',
             '<style>body{font:16px system-ui;margin:2rem;max-width:1200px}'
             'table{border-collapse:collapse;width:100%}td,th{border:1px solid #bbb;'
             'padding:.4rem;text-align:left}pre{white-space:pre-wrap}'
             '.Survived,.NoCoverage{background:#fff0d0}</style>',
             '<h1>Host mutation testing</h1><p>Full merged Mull results. '
             'See summary.md for score and scope; this report needs no network connection.</p>']
    for name, file in sorted(report["files"].items()):
        lines = file["source"].splitlines()
        parts.append(f'<h2>{escape(name)}</h2><table><tr><th>Line</th><th>Status</th>'
                     '<th>Operator</th><th>Original line → replacement expression</th></tr>')
        for mutant in sorted(file["mutants"], key=lambda m: m["location"]["start"]["line"]):
            line = mutant["location"]["start"]["line"]
            status = escape(mutant["status"])
            parts.append(f'<tr class="{status}"><td>{line}</td><td>{status}</td>'
                         f'<td>{escape(mutant["mutatorName"])}</td><td><pre>'
                         f'{escape(lines[line - 1])}\n→ {escape(mutant["replacement"])}'
                         '</pre></td></tr>')
        parts.append('</table>')
    parts.append('</html>')
    return "\n".join(parts)


def source_digest(source):
    return hashlib.sha256(source.replace("\r\n", "\n").encode("utf-8")).hexdigest()


def review_survivors(report, baseline):
    """Accept only explicitly reviewed mutations of exactly the reviewed source."""
    if baseline["version"] != 1:
        raise ValueError("Unsupported mutation baseline version")
    accepted = {}
    for name, file in baseline["files"].items():
        digest = file["sha256"]
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError(f"Invalid source digest: {name}")
        for mutant in file["mutants"]:
            if (mutant["category"] not in {"equivalent", "diagnostic", "sanitizer-confirmed"}
                    or not mutant["reason"].strip()):
                raise ValueError(f"Missing review rationale: {name}")
            key = (name, digest, mutant["id"], mutant["replacement"])
            if key in accepted:
                raise ValueError(f"Duplicate accepted mutation: {mutant['id']}")
            accepted[key] = mutant
    reviewed, unexpected, used = [], [], set()
    for name, file in report["files"].items():
        digest = source_digest(file["source"])
        for mutant in file["mutants"]:
            if mutant["status"] not in {"Survived", "NoCoverage"}:
                continue
            key = (name, digest, mutant["id"], mutant["replacement"])
            item = {"file": name, "id": mutant["id"], "status": mutant["status"]}
            # Previously covered survivors must never excuse lost coverage.
            if mutant["status"] == "Survived" and key in accepted:
                used.add(key)
                reviewed.append({**item, "category": accepted[key]["category"],
                                 "reason": accepted[key]["reason"]})
            else:
                unexpected.append(item)
    inactive = [{"file": key[0], "id": key[2]} for key in sorted(accepted.keys() - used)]
    result = {"accepted": reviewed, "unexpected": unexpected, "inactive": inactive}
    text = ("\n### Survivor review\n\n"
            f"Accepted survivors: **{len(reviewed)}**. "
            f"Unreviewed survivors or uncovered mutations: **{len(unexpected)}**. "
            f"Inactive baseline entries: **{len(inactive)}**.\n\n"
            "Acceptance does not change mutation status or score. Source changes require a new review; "
            "inactive entries may be removed after review.\n")
    if unexpected:
        text += "\n" + "\n".join(f"- `{item['id']}` — {item['status']}" for item in unexpected) + "\n"
    return result, text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("runner", "reporter", "manifest", "root", "output", "baseline"):
        parser.add_argument(f"--{arg}", required=True, type=Path)
    parser.add_argument("--library-dir", action="append", default=[])
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    # Only our dedicated generated report directory may be cleaned.
    if output.name != "mutation" or not output.is_relative_to(root / "build"):
        raise ValueError("Reports must be in build/<preset>/mutation")
    output.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.sqlite", "*.sqlite-*", "*.json", "*.html", "*.txt", "*.log", "summary.md"):
        for path in output.glob(pattern):
            path.unlink()
    pairs = json.loads(args.manifest.read_text(encoding="utf-8"))
    if len(pairs) != 7:
        raise ValueError("Expected five native test executables and two shared-library runs")
    tests = sorted({test for _, test in pairs})
    for test in tests:
        run([test], output / f"baseline-{Path(test).name}.log", root, timeout=30)
    for index, (binary, test) in enumerate(pairs):
        libraries = [arg for directory in args.library_dir for arg in ("--ld-search-path", directory)]
        run([str(args.runner), "--allow-surviving", "--include-not-covered",
             "--timeout", "3000", "--workers", "2",
             "--reporters", "SQLite", "--report-name", "mutations",
             "--report-dir", str(output), "--test-program", test, *libraries, binary],
            output / f"run-{index}-{Path(test).name}.log", root)
    run([str(args.reporter), "--allow-surviving", "--reporters", "Elements",
         "--reporters", "IDE", "--report-name", "mutations", "--report-dir", str(output),
         str(output / "mutations.sqlite")], output / "reporter.log", root)
    report = json.loads((output / "mutations.json").read_text(encoding="utf-8"))
    summary = summarize(report, root)
    report["files"] = {Path(name).relative_to(root).as_posix(): file
                       for name, file in report["files"].items()}
    for file in report["files"].values():
        for mutant in file["mutants"]:
            mutant["id"] = mutant["id"].replace(root.as_posix() + "/", "")
    review, review_text = review_survivors(report, json.loads(args.baseline.read_text(encoding="utf-8")))
    summary += review_text
    (output / "review.json").write_text(json.dumps(review, ensure_ascii=False, indent=2), encoding="utf-8")
    (output / "mutations.json").write_text(json.dumps(report, ensure_ascii=False), encoding="utf-8")
    (output / "mutations.html").write_text(standalone_html(report), encoding="utf-8")
    (output / "summary.md").write_text(summary, encoding="utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
            stream.write(summary)
    print(summary, flush=True)
    if review["unexpected"]:
        raise ValueError("Unreviewed survivors or uncovered mutations; see summary.md and review.json")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"Mutation testing failed: {error}", file=sys.stderr)
        sys.exit(1)
