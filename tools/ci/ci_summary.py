"""Render existing CI results for GitHub's run summary; never run tests."""
from collections import Counter
from html import escape
import json
import os
from pathlib import Path
import re
import xml.etree.ElementTree as ET


BUILD_DIRS = {"portable": "native", "sanitizers": "sanitizers-linux",
              "fuzz": "fuzz-linux", "valgrind": "valgrind-linux",
              "mutation": "mutation-linux", "windows": "windows",
              "coverage-linux": "coverage-linux", "coverage-windows": "coverage-windows"}
CTEST_JOBS = set(BUILD_DIRS) - {"mutation", "valgrind"}


def cell(value):
    return escape(str(value)).replace("|", "&#124;").replace("\n", " ")


def analysis(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    complete = data["complete"] and data["expected"] > 0 and data["completed"] == data["expected"] and not data["errors"]
    state = "complete" if complete else "**incomplete — not a passing result**"
    text = (f"{cell(data['analyzer'])} ({cell(data['version'])}): **{data['completed']}/{data['expected']} units**, "
              f"**{len(data['findings'])} findings**, {data['seconds']:.1f}s {cell(data.get('timing_scope', 'analysis'))}; {state}.")
    for item in data["findings"][:10]:
        text += f"\n\n- `{cell(item['file'])}:{item['line']}` — {cell(item['rule'])}: {cell(item['message'])}"
    for error in data["errors"][:3]:
        text += f"\n\nError: {cell(error)}"
    return text


def test_totals(cases):
    counts = Counter(status for _, status, _ in cases)
    result = [f"**{len(cases)} checks:** {counts['passed']} passed, "
              f"{counts['failed']} failed, {counts['skipped']} skipped."]
    failed = [name for name, status, _ in cases if status == "failed"]
    if failed:
        result.append("Failed: " + ", ".join(cell(name) for name in failed[:20])
                      + (f" (+{len(failed) - 20} more)" if len(failed) > 20 else ""))
    return "\n\n".join(result)


def junit(path):
    cases = []
    tests = list(ET.parse(path).getroot().iter("testcase"))
    for test in tests:
        status = "passed"
        if test.find("failure") is not None or test.find("error") is not None:
            status = "failed"
        elif test.find("skipped") is not None or test.get("status") in {"notrun", "disabled"}:
            status = "skipped"
        cases.append((test.get("name", "unnamed"), status, test.findtext("system-out", "")))
    if not cases:
        raise ValueError("report contains no test cases")
    result = test_totals(cases)
    timings = sorted(((float(test.get("time", "0")), test.get("name", "unnamed"))
                      for test in tests), reverse=True)
    slow = [f"{cell(name)} **{seconds:.1f}s**" for seconds, name in timings[:3] if seconds >= 5]
    if slow:
        result += "\n\nSlowest checks: " + "; ".join(slow) + "."
    # libFuzzer prints these only for completed runs; crashes remain failed tests.
    for name, _, output in cases:
        runs = re.findall(r"Done (\d+) runs in (\d+) second", output)
        if runs:
            count, seconds = runs[-1]
            result += f"\n\n{cell(name)}: **{int(count):,} inputs**, {seconds} s."
    return result


def unittest_log(path):
    log = path.read_text(encoding="utf-8", errors="replace")
    counts = re.findall(r"^Ran (\d+) tests? in ([\d.]+)s", log, re.M)
    if not counts:
        raise ValueError("test runner did not finish")
    endings = re.findall(r"^(OK(?: \([^\n]*\))?|FAILED[^\n]*)$", log, re.M)
    if not endings:
        raise ValueError("test runner did not report a result")
    count, seconds = counts[-1]
    failures = re.findall(r"^(?:FAIL|ERROR): (.+)$", log, re.M)
    return (f"**{count} Python tests**, {seconds} s: {cell(endings[-1])}."
            + ("\n\nFailed: " + ", ".join(cell(name) for name in failures[:20]) if failures else ""))


def memcheck(directory):
    tag = (directory / "Testing/TAG").read_text(encoding="utf-8").splitlines()[0]
    reports = directory / "Testing" / tag
    cases = [(test.findtext("Name", "unnamed"),
              {"passed": "passed", "notrun": "skipped"}.get(test.get("Status"), "failed"), "")
             for test in ET.parse(reports / "DynamicAnalysis-Test.xml").findall("./Testing/Test")]
    if not cases:
        raise ValueError("Memcheck did not report any tests")
    defects = ET.parse(reports / "DynamicAnalysis.xml").findall(".//Defect")
    count = sum(int(item.text or "0") for item in defects)
    return test_totals(cases) + f"\n\nValgrind: **{count} reported memory defects**."


def mutation(directory):
    report = json.loads((directory / "mutations.json").read_text(encoding="utf-8"))
    counts = Counter(m["status"] for f in report["files"].values() for m in f["mutants"])
    total = counts.total()
    if not total:
        raise ValueError("no mutations reported")
    detected = counts["Killed"] + counts["Timeout"]
    text = [f"Mutation score: **{100 * detected / total:.2f}%** ({detected}/{total}).",
            "| Result | Mutants |\n| --- | ---: |"]
    text.extend(f"| {cell(status)} | {count} |" for status, count in sorted(counts.items()))
    review = json.loads((directory / "review.json").read_text(encoding="utf-8"))
    text += [f"\nAccepted survivors: **{len(review['accepted'])}**; "
             f"unreviewed survivors / uncovered mutations: **{len(review['unexpected'])}**.",
             "Score includes timeouts as detected; accepted survivors do not raise it. "
             "Qt and device execution are outside this measurement."]
    return text[0] + "\n\n" + "\n".join(text[1:])


def firmware_board(directory, board):
    manifest = json.loads((directory / board / "manifest.json").read_text(encoding="utf-8"))
    size = (directory / board / "R2500Adapter.ino.bin").stat().st_size
    return (f"Version **{cell(manifest['project_version'])}**, host **{cell(manifest['host_platform'])}**, "
            f"application **{size / 1024:.0f} KiB**; {len(manifest['files'])} files in manifest.")


def release_assets(directory):
    assets = sorted(path for path in directory.iterdir()
                    if path.is_file() and (path.suffix in {".zip", ".exe"} or path.name == "SHA256SUMS"))
    if not assets:
        raise ValueError("no prepared release assets")
    return "| Prepared asset | Size |\n| --- | ---: |\n" + "\n".join(
        f"| {cell(path.name)} | {path.stat().st_size / (1024 * 1024):.2f} MiB |" for path in assets)


def render(root, job, status, steps, needs):
    text = [f"## {cell(job)} — {cell(status)}", ""]
    if status != "success":
        text += ["**This job did not complete successfully; results below may be partial.**", ""]
    problems = [(name, step) for name, step in steps.items()
                if step.get("outcome") in {"failure", "cancelled"}]
    for name, step in problems:
        qualifier = " (non-blocking)" if step.get("conclusion") == "success" else ""
        text.append(f"- **{cell(step['outcome'])}{qualifier}:** {cell(name.replace('-', ' '))}")
    if problems:
        text.append("")
    def outcome(name):
        return cell(steps.get(name, {}).get("outcome", "not run"))

    if job == "audit":
        policy = steps.get("policy", {}).get("outputs", {})
        text += [f"Public-file / secret audit: **{outcome('audit-public-files-and-reachable-history')}**. "
                 f"CI mode: **{cell(policy.get('mode', 'unavailable'))}**; "
                 f"release requested: **{cell(policy.get('release', 'unavailable'))}**.", ""]
    if job == "windows-sdk":
        text += [f"Complete SDK validation: **{outcome('prepare-and-validate-complete-sdks')}**.", ""]
        key_seconds = steps.get("qt-key", {}).get("outputs", {}).get("key-seconds")
        if key_seconds:
            text += [f"Cache identity computation: **{cell(key_seconds)}s**.", ""]
    if job in {"windows-sdk", "firmware"}:
        keys = ("qt", "qt-sources", "wdk") if job == "windows-sdk" else ("arduino", "firmware-legal")
        caches = []
        for key in keys:
            step = steps.get(key, {})
            hit = step.get("outputs", {}).get("cache-hit")
            state = ("hit" if hit == "true" else "miss") if step.get("outcome") == "success" else "unavailable"
            caches.append(f"{key}: **{state}**")
        text += ["Cache restores — " + "; ".join(caches) + ".", ""]
    if job == "windows":
        text += [f"Applications built: **{outcome('build-windows-applications-and-host-tests')}**. "
                 "Release packaging / independent source rebuild: "
                 f"**{outcome('package-and-independently-rebuild-corresponding-sources')}**.", ""]
    if job == "release":
        text += [f"Release asset verification: **{outcome('prepare-and-verify-release-assets')}**; "
                 f"publication: **{outcome('publish-complete-release')}**.", ""]
    if job == "ci-result":
        text += ["| Job | Result |", "| --- | --- |"]
        text.extend(f"| {cell(name)} | {cell(data.get('result', 'missing'))} |"
                    for name, data in needs.items())
        text.append("")

    def report(title, read):
        text.extend([f"### {title}", ""])
        try:
            text.append(read())
        except (OSError, ValueError, KeyError, TypeError, IndexError, ET.ParseError):
            text.append("**Report unavailable or incomplete.** Check the failed step and diagnostics; "
                        "this is not a passing test result.")
        text.append("")

    directory = root / "build" / BUILD_DIRS.get(job, job)
    if job == "audit":
        report("Workflow static analysis", lambda: analysis(root / "build/native/workflow-lint/workflow-lint.json"))
    if job == "static-analysis":
        for analyzer in ("clang-tidy", "cppcheck"):
            report(analyzer, lambda analyzer=analyzer: analysis(root / f"build/static-analysis-linux/analysis/{analyzer}.json"))
    if job == "windows":
        report("MSVC static analysis", lambda: analysis(directory / "analysis/msvc.json"))
        report("Build with MSVC analysis", lambda:
               f"{json.loads((directory / 'analysis/build-time.json').read_text(encoding='utf-8'))['seconds']:.1f}s "
               "for compilation and analysis together; report validation is timed separately.")
    if job in CTEST_JOBS and not job.startswith("coverage-"):
        report("CTest checks", lambda: junit(directory / "ci-tests.xml"))
    if job in {"audit", "portable", "mutation"}:
        report("Python tooling tests", lambda: unittest_log(root / "build/ci-reports/unittest.log"))
    if job.startswith("coverage-"):
        report("Measured test run", lambda: junit(directory / "coverage/tests.xml"))
        report("Line coverage", lambda: (directory / "coverage/summary.md").read_text(encoding="utf-8"))
    if job == "valgrind":
        report("Memory checks", lambda: memcheck(directory))
    if job == "mutation":
        report("Mutation analysis", lambda: mutation(directory / "mutation"))
    if job == "firmware":
        for board in ("waveshare-147", "pocket-096"):
            report(board, lambda board=board: firmware_board(root / "build/firmware-linux/dist/firmware", board))
    if job == "release":
        report("Release assets", lambda: release_assets(root / "build/release-assets"))

    artifacts = [(name, data.get("outputs", {}).get("artifact-url")) for name, data in steps.items()]
    links = [f"[{cell(name.replace('-', ' '))}]({url})" for name, url in artifacts
             if url and url.startswith("https://github.com/")]
    if links:
        text += ["Reports: " + " · ".join(links), ""]
    if steps:
        text += ["<details><summary>Steps and skipped checks</summary>", "",
                 "| Step | Result |", "| --- | --- |"]
        text.extend(f"| {cell(name.replace('-', ' '))} | {cell(data.get('outcome', 'missing'))} |"
                    for name, data in steps.items())
        text += ["", "</details>", ""]
    return "\n".join(text)


def main():
    text = render(Path.cwd(), os.environ["GITHUB_JOB"], os.environ["CI_JOB_STATUS"],
                  json.loads(os.environ["CI_STEPS"]), json.loads(os.environ.get("CI_NEEDS", "{}")))
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
        stream.write(text)


if __name__ == "__main__":
    main()
