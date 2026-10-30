"""Run analyzers and validate their reports. CMake owns all product builds."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from urllib.parse import unquote, urlparse
import xml.etree.ElementTree as ET


PORTABLE_SOURCES = ("core/native/api.cpp", *(f"tests/native/{name}.cpp" for name in
                    ("models_test", "abi_test", "ups_test", "firmware_test", "history_rollback_test")))


def run(command, cwd=None, env=None):
    return subprocess.run([str(x) for x in command], cwd=cwd, env=env, text=True,
                          encoding="utf-8", errors="replace", capture_output=True, timeout=600)


def version(tool, expected):
    result = run([tool, "--version"])
    text = result.stdout + result.stderr
    if result.returncode or not re.search(expected, text):
        raise ValueError(f"Unexpected analyzer version: {text.strip()}")
    return next(line.strip() for line in text.splitlines() if re.search(expected, line))


def relative(path, root):
    path = unquote(str(path)).replace("\\", "/")
    if path.startswith("file:"):
        path = urlparse(path).path
        if re.match(r"^/[A-Za-z]:/", path):
            path = path[1:]
    base = root.resolve().as_posix().rstrip("/") + "/"
    if path.casefold().startswith(base.casefold()):
        return path[len(base):]
    return path


def finding(file, line, rule, message):
    return dict(file=file, line=int(line or 0), rule=rule, message=message)


def unique(findings):
    values = {(x["file"], x["line"], x["rule"], x["message"]): x for x in findings}
    return [values[key] for key in sorted(values)]


def product_findings(findings):
    # Test TUs instantiate product headers; the test harness and SDKs are not
    # product analysis scope. Syntax/tool errors are handled before this filter.
    return [x for x in findings if x["file"].casefold().startswith(
        ("core/", "firmware/r2500adapter/", "apps/windows/", "drivers/windows/"))]


def write_report(path, name, tool_version, expected, completed, findings, errors, started, timing_scope="analysis", quiet=False):
    report = dict(analyzer=name, version=tool_version, expected=expected, completed=completed,
                  seconds=round(time.monotonic() - started, 3), findings=unique(findings), errors=errors,
                  complete=bool(expected) and completed == expected and not errors, timing_scope=timing_scope)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if quiet:
        return False
    print(f"{name}: {completed}/{expected} units, {len(report['findings'])} findings, "
          f"{len(errors)} errors, {report['seconds']:.1f}s")
    for item in report["findings"][:10]:
        print(f"{item['file']}:{item['line']}: {item['rule']}: {item['message']}")
    for error in errors:
        print(error)
    return report["complete"] and not report["findings"]


def parse_tidy(text, root):
    diagnostics = []
    pattern = r"^(.+?):(\d+):\d+: (warning|error|fatal error): (.+?)(?: \[([^\]]+)\])?$"
    for match in re.finditer(pattern, text, re.M):
        file, line, level, message, rule = match.groups()
        diagnostics.append(finding(relative(file, root), line,
                                   (rule or f"clang-diagnostic-{level}").removesuffix(",-warnings-as-errors"), message))
    return diagnostics


def parse_cppcheck(text, root):
    tree = ET.fromstring(text)
    if tree.tag != "results" or tree.find("cppcheck") is None or tree.find("errors") is None:
        raise ValueError("Incomplete Cppcheck XML")
    diagnostics, errors = [], []
    for item in tree.findall("./errors/error"):
        rule = item.get("id", "unknown")
        if rule in {"missingIncludeSystem", "checkersReport"}:
            continue  # Standard library behavior comes from Cppcheck's std.cfg.
        location = item.find("location")
        if rule in {"syntaxError", "internalError", "cppcheckError", "missingInclude", "missingIncludeUser"}:
            errors.append(f"Cppcheck {rule}: {item.get('msg', '')}")
        elif item.get("severity") in {"error", "warning", "portability"}:
            if location is None or not location.get("file") or not item.get("id"):
                raise ValueError("Cppcheck diagnostic has no source location or rule")
            diagnostics.append(finding(relative(location.get("file", "") if location is not None else "", root),
                                       location.get("line", 0) if location is not None else 0,
                                       rule, item.get("msg", "")))
        else:
            errors.append(f"Unexpected Cppcheck diagnostic: {rule}: {item.get('msg', '')}")
    return diagnostics, errors


def portable(args):
    started = time.monotonic()
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    database = json.loads((args.build / "compile_commands.json").read_text(encoding="utf-8"))
    by_file = {relative(Path(x["directory"]) / x["file"], args.root): x for x in database}
    missing = set(PORTABLE_SOURCES) - by_file.keys()
    if missing:
        raise ValueError(f"Missing compilation units: {sorted(missing)}")
    selected = [by_file[name] for name in PORTABLE_SOURCES]
    (output / "compile_commands.json").write_text(json.dumps(selected), encoding="utf-8")
    tidy_errors, tidy_findings, completed, tool_version = [], [], 0, "unavailable"
    try:
        tool_version = version(args.tidy, r"LLVM version 18\.")

        def analyze(entry):
            source = entry["file"]
            result = run([args.tidy, source, "-p", output, "--quiet",
                          f"--config-file={args.root / '.clang-tidy'}"], cwd=entry["directory"])
            log = result.stdout + result.stderr
            key = hashlib.sha256(source.encode()).hexdigest()[:16]
            (output / f"tidy-{key}.log").write_text(log, encoding="utf-8")
            findings = parse_tidy(log, args.root)
            if result.returncode not in (0, 1) or (result.returncode and not findings):
                raise ValueError(f"clang-tidy failed for {relative(source, args.root)}: {log[-2000:]}")
            if any(x["rule"].startswith("clang-diagnostic-error") or
                   x["rule"].startswith("clang-diagnostic-fatal") for x in findings):
                raise ValueError(f"clang-tidy could not parse {relative(source, args.root)}: {log[-2000:]}")
            return findings

        with ThreadPoolExecutor(max_workers=4) as pool:
            futures = [pool.submit(analyze, entry) for entry in selected]
            for future in futures:
                try:
                    tidy_findings.extend(future.result())
                    completed += 1
                except (OSError, ValueError, subprocess.SubprocessError) as error:
                    tidy_errors.append(str(error))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        tidy_errors.append(str(error))
    tidy_ok = write_report(output / "clang-tidy.json", "clang-tidy", tool_version,
                           len(selected), completed, product_findings(tidy_findings), tidy_errors, started)
    # Always run the independent second analyzer, including after tidy findings.
    started = time.monotonic()
    errors, findings, completed, tool_version = [], [], 0, "unavailable"
    try:
        tool_version = version(args.cppcheck, r"^Cppcheck 2\.22\.0\s*$")
        # A fresh work directory prevents reuse of old diagnostics/CTU data.
        import tempfile
        with tempfile.TemporaryDirectory(prefix="cppcheck-", dir=output) as cache:
            result = run([args.cppcheck, f"--project={output / 'compile_commands.json'}",
                          "--std=c++23", "--platform=unix64", "--enable=warning,portability,information",
                          "--check-level=exhaustive",
                          "--inline-suppr", "--error-exitcode=1", "--xml", "--xml-version=2",
                          f"--cppcheck-build-dir={cache}", "-j4"], cwd=args.root)
        (output / "cppcheck.xml").write_text(result.stderr, encoding="utf-8")
        (output / "cppcheck.log").write_text(result.stdout, encoding="utf-8")
        findings, errors = parse_cppcheck(result.stderr, args.root)
        checked = set(re.findall(r"Checking (.+?)(?: \.\.\.)?$", result.stdout, re.M))
        for entry in selected:
            source = relative(entry["file"], args.root)
            if any(relative(item, args.root) == source for item in checked):
                completed += 1
        if result.returncode not in (0, 1) or (result.returncode and not findings and not errors):
            errors.append(f"Cppcheck failed with exit code {result.returncode}")
    except (OSError, ValueError, ET.ParseError, subprocess.SubprocessError) as error:
        errors.append(str(error))
    cppcheck_ok = write_report(output / "cppcheck.json", "Cppcheck", tool_version,
                               len(selected), completed, product_findings(findings), errors, started)
    return tidy_ok and cppcheck_ok


def parse_sarif(path, root, source=None):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if data.get("version") != "2.1.0" or not data.get("runs"):
        raise ValueError(f"Empty SARIF: {path}")
    findings = []
    for record in data["runs"]:
        if not isinstance(record.get("results"), list):
            raise ValueError(f"Missing SARIF results: {path}")
        if not record.get("invocations") or not record.get("tool", {}).get("driver", {}).get("version"):
            raise ValueError(f"Missing SARIF completion/tool metadata: {path}")
        for invocation in record.get("invocations", []):
            if invocation.get("executionSuccessful") is not True:
                raise ValueError(f"Unsuccessful SARIF invocation: {path}")
        if source is not None:
            targets = [item for item in record.get("artifacts", []) if "analysisTarget" in item.get("roles", [])]
            if len(targets) != 1 or relative(targets[0]["location"]["uri"], root).casefold() != relative(source, root).casefold():
                raise ValueError(f"Wrong or missing SARIF translation unit: {path}")
            if targets[0].get("hashes", {}).get("sha-256") != hashlib.sha256(Path(source).read_bytes()).hexdigest():
                raise ValueError(f"SARIF source hash mismatch: {path}")
        for item in record["results"]:
            if item.get("suppressions"):
                continue
            if item.get("level", "warning") not in {"warning", "error"}:
                continue
            location = item.get("locations", [{}])[0].get("physicalLocation", {})
            artifact = location.get("artifactLocation", {})
            if "uri" not in artifact and "index" in artifact:
                artifact = record["artifacts"][artifact["index"]]["location"]
            message = item.get("message", {})
            if not artifact.get("uri") or not item.get("ruleId"):
                raise ValueError(f"SARIF diagnostic has no source location or rule: {path}")
            findings.append(finding(relative(artifact.get("uri", ""), root),
                                    location.get("region", {}).get("startLine", 0),
                                    item.get("ruleId", "unknown"), message.get("text", str(message))))
    return findings


def msvc_manifests(index, visited=None):
    visited = set() if visited is None else visited
    if index in visited:
        raise ValueError(f"Repeated MSVC project index: {index}")
    visited.add(index)
    data = json.loads(index.read_text(encoding="utf-8"))
    if not isinstance(data.get("manifests"), list) or not data["manifests"]:
        raise ValueError(f"No MSVC target manifests: {index}")
    manifests = [Path(path) for path in data["manifests"]]
    for child in data["children"]:
        manifests.extend(msvc_manifests(Path(child), visited))
    if len(manifests) != len(set(manifests)):
        raise ValueError("Duplicate MSVC target manifest")
    return manifests


def msvc(args):
    started = time.monotonic()
    expected, completed, findings, errors, versions = 0, 0, [], [], set()
    try:
        manifests = msvc_manifests(args.build / "analysis-index.json")
    except (OSError, ValueError, KeyError, TypeError) as error:
        manifests = []
        errors.append(str(error))
    for path in manifests:
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
            versions.add(manifest["version"])
            if not manifest["units"]:
                raise ValueError(f"No MSVC units: {path}")
            for unit in manifest["units"]:
                expected += 1
                try:
                    report = Path(unit["report"])
                    newest = max(Path(p).stat().st_mtime for p in (unit["source"], manifest["rules"]))
                    if report.stat().st_mtime < newest:
                        raise ValueError(f"Stale SARIF: {report}")
                    findings.extend(parse_sarif(report, args.root, unit["source"]))
                    completed += 1
                except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
                    errors.append(str(error))
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(str(error))
    if args.build_status != "success":
        errors.append("Product build did not complete successfully")
    return write_report(args.output / "msvc.json", "MSVC /analyze", ", ".join(sorted(versions)),
                        expected, completed, product_findings(findings), errors, started, "report validation")


def workflow(args):
    started = time.monotonic()
    files = sorted(set((args.root / ".github/workflows").glob("*.yml")) |
                   set((args.root / ".github/workflows").glob("*.yaml")))
    args.output.mkdir(parents=True, exist_ok=True)
    errors, findings, completed, tools_version = [], [], 0, "unavailable"
    try:
        tools_version = version(args.actionlint, r"^1\.7\.12\b") + "; " + version(args.shellcheck, r"version: 0\.11\.0\b")
        if not files:
            raise ValueError("No workflows selected")
        result = run([args.actionlint, "-no-color", "-shellcheck", args.shellcheck, "-pyflakes=",
                      "-format", "{{json .}}", *files], cwd=args.root,
                     env={**os.environ, "SHELLCHECK_OPTS": "--severity=warning"})
        (args.output / "workflow-lint.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode not in (0, 1) or result.stderr.strip():
            raise ValueError(f"actionlint failed: {result.stderr}")
        items = json.loads(result.stdout) if result.stdout.strip() else []
        if not isinstance(items, list) or (result.returncode and not items):
            raise ValueError("Invalid actionlint result")
        for item in items:
            findings.append(finding(relative(item["filepath"], args.root), item["line"], item["kind"], item["message"]))
        completed = len(files)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        errors.append(str(error))
    return write_report(args.output / "workflow-lint.json", "actionlint + ShellCheck", tools_version,
                        len(files), completed, findings, errors, started)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("linux", "msvc", "workflow"))
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tidy", default="clang-tidy-18")
    parser.add_argument("--cppcheck", default="cppcheck")
    parser.add_argument("--actionlint", default="actionlint")
    parser.add_argument("--shellcheck", default="shellcheck")
    parser.add_argument("--build-status", default=os.environ.get("ANALYSIS_BUILD_STATUS", "success"))
    args = parser.parse_args()
    args.root, args.output = args.root.resolve(), args.output.resolve()
    if args.build:
        args.build = args.build.resolve()
    reports = {"linux": ("clang-tidy", "cppcheck"), "msvc": ("msvc",), "workflow": ("workflow-lint",)}[args.kind]
    # Replace old success before reading inputs or invoking a tool. Interrupted
    # runs and early setup failures must never expose a previous green report.
    for name in reports:
        write_report(args.output / f"{name}.json", name, "unavailable", 0, 0, [],
                     ["Analysis has not completed"], time.monotonic(), quiet=True)
    try:
        passed = {"linux": portable, "msvc": msvc, "workflow": workflow}[args.kind](args)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"Analysis incomplete: {error}")
        passed = False
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
