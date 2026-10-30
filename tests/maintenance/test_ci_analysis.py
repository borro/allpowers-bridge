import argparse
import importlib.util
import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("static_analysis", ROOT / "tools/quality/static_analysis.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class AnalysisReports(unittest.TestCase):
    def test_header_duplicates_merge_but_distinct_rules_remain(self):
        item = analysis.finding("core/header.h", 7, "nullPointer", "null")
        self.assertEqual(len(analysis.unique([item, item, {**item, "rule": "other"}])), 2)

    def test_cppcheck_incomplete_and_unparsed_code(self):
        for text in ('<results/>', '<results><cppcheck/>', '<results><cppcheck/></results>'):
            with self.assertRaises((ValueError, analysis.ET.ParseError)):
                analysis.parse_cppcheck(text, ROOT)
        findings, errors = analysis.parse_cppcheck(
            '<results><cppcheck/><errors><error id="syntaxError" severity="error" msg="parse failed"/>'
            '</errors></results>', ROOT)
        self.assertFalse(findings)
        self.assertTrue(errors)

    def test_sarif_missing_truncated_and_unsuccessful(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.sarif"
            for data in ({}, {"version": "2.1.0", "runs": [{}]},
                         {"version": "2.1.0", "runs": [{"results": [], "invocations": [
                             {"executionSuccessful": False}]}]}):
                path.write_text(json.dumps(data), encoding="utf-8")
                with self.assertRaises(ValueError):
                    analysis.parse_sarif(path, ROOT)
            path.write_text('{"runs":', encoding="utf-8")
            with self.assertRaises(ValueError):
                analysis.parse_sarif(path, ROOT)

    def test_no_units_and_incomplete_counts_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            for expected, completed in [(0, 0), (6, 5), (2, 3)]:
                self.assertFalse(analysis.write_report(Path(directory) / "report.json", "test", "1",
                    expected, completed, [], [], time.monotonic()))

    def test_missing_tool_fails_workflow(self):
        with tempfile.TemporaryDirectory() as directory:
            args = argparse.Namespace(root=ROOT, output=Path(directory),
                                      actionlint="missing", shellcheck="missing")
            with patch.object(analysis, "run", side_effect=FileNotFoundError("missing tool")):
                self.assertFalse(analysis.workflow(args))
            self.assertFalse(json.loads((args.output / "workflow-lint.json").read_text())["complete"])

    def test_missing_msvc_report_and_failed_build_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            source = build / "source.cpp"
            source.write_text("int f() { return 1; }", encoding="utf-8")
            manifest = dict(version="test", rules=str(source), units=[
                dict(source=str(source), report=str(build / "missing.sarif"))])
            (build / "analysis-manifest-test.json").write_text(json.dumps(manifest), encoding="utf-8")
            (build / "analysis-index.json").write_text(json.dumps(dict(
                manifests=[str(build / "analysis-manifest-test.json")], children=[])), encoding="utf-8")
            args = argparse.Namespace(root=ROOT, build=build, output=build, build_status="success")
            self.assertFalse(analysis.msvc(args))
            report = json.loads((build / "msvc.json").read_text())
            self.assertEqual((report["expected"], report["completed"]), (1, 0))
            args.build_status = "failure"
            self.assertFalse(analysis.msvc(args))

    def test_report_cli_preserves_failed_ci_build_status(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            result = subprocess.run([sys.executable, "-I", "-S", "-B", str(ROOT / "tools/quality/static_analysis.py"),
                                     "msvc", "--root", str(ROOT), "--build", str(work), "--output", str(work)],
                                    env={**os.environ, "ANALYSIS_BUILD_STATUS": "failure"}, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Product build did not complete successfully",
                          json.loads((work / "msvc.json").read_text())["errors"])

    def test_tidy_memory_error_and_source_parse_error(self):
        findings = analysis.parse_tidy(
            f"{ROOT.as_posix()}/core/a.cpp:2:3: error: null [clang-analyzer-core.NullDereference,-warnings-as-errors]\n"
            f"{ROOT.as_posix()}/core/a.cpp:4:3: error: expected expression [clang-diagnostic-error]\n", ROOT)
        self.assertEqual([x["rule"] for x in findings],
                         ["clang-analyzer-core.NullDereference", "clang-diagnostic-error"])

    def test_missing_database_replaces_old_success_for_both_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            for name in ("clang-tidy", "cppcheck"):
                (build / f"{name}.json").write_text('{"complete":true}', encoding="utf-8")
            result = subprocess.run([sys.executable, "-I", "-S", "-B", str(ROOT / "tools/quality/static_analysis.py"),
                                     "linux", "--root", str(ROOT), "--build", str(build), "--output", str(build)],
                                    capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            for name in ("clang-tidy", "cppcheck"):
                self.assertFalse(json.loads((build / f"{name}.json").read_text())["complete"])

    def test_cppcheck_still_runs_after_tidy_tool_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            entries = [dict(directory=str(ROOT), file=str(ROOT / name), command="clang++ -c input.cpp")
                       for name in analysis.PORTABLE_SOURCES]
            (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
            result = subprocess.CompletedProcess([], 0, "\n".join(f"Checking {entry['file']} ..." for entry in entries),
                                                 '<results><cppcheck/><errors/></results>')
            args = argparse.Namespace(root=ROOT, build=build, output=build,
                                      tidy="missing", cppcheck="cppcheck")
            with patch.object(analysis, "version", side_effect=[FileNotFoundError("tidy missing"), "Cppcheck 2.22.0"]), \
                 patch.object(analysis, "run", return_value=result) as run:
                self.assertFalse(analysis.portable(args))
            run.assert_called_once()
            self.assertTrue(json.loads((build / "cppcheck.json").read_text())["complete"])

    def test_msvc_missing_child_index_is_not_silently_omitted(self):
        with tempfile.TemporaryDirectory() as directory:
            index = Path(directory) / "analysis-index.json"
            index.write_text(json.dumps(dict(manifests=["target.json"], children=[str(index.parent / "missing.json")])))
            with self.assertRaises(FileNotFoundError):
                analysis.msvc_manifests(index)

    def test_sarif_matches_unit_contents_and_rejects_stale_rules(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source, sarif = work / "source.cpp", work / "unit.sarif"
            source.write_text("int a;", encoding="utf-8")
            record = dict(results=[], tool=dict(driver=dict(version="test")),
                          invocations=[dict(executionSuccessful=True)], artifacts=[dict(
                              location=dict(uri=source.as_uri()), roles=["analysisTarget"],
                              hashes={"sha-256": hashlib.sha256(source.read_bytes()).hexdigest()})])
            sarif.write_text(json.dumps(dict(version="2.1.0", runs=[record])), encoding="utf-8")
            self.assertEqual(analysis.parse_sarif(sarif, work, source), [])
            source.write_text("int b;", encoding="utf-8")
            with self.assertRaises(ValueError):
                analysis.parse_sarif(sarif, work, source)
            manifest = work / "target.json"
            manifest.write_text(json.dumps(dict(version="test", rules=str(source), units=[
                dict(source=str(source), report=str(sarif))])), encoding="utf-8")
            (work / "analysis-index.json").write_text(json.dumps(dict(manifests=[str(manifest)], children=[])))
            os.utime(sarif, (1, 1))
            args = argparse.Namespace(root=work, build=work, output=work, build_status="success")
            self.assertFalse(analysis.msvc(args))
            self.assertIn("Stale SARIF", (work / "msvc.json").read_text())


if __name__ == "__main__":
    unittest.main()
