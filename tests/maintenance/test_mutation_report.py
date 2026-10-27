"""Integrity of mutation summaries: incomplete reports must never look green."""
import importlib.util
import copy
from pathlib import Path
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "mutation_report", Path(__file__).resolve().parents[2] / "cmake/MutationReport.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def sample(*statuses):
    return {"files": {name: {"source": "a >= b;\n" * len(statuses), "mutants": [
        {"id": f"cxx_ge_to_gt:{name}:{i + 1}:3", "replacement": ">", "status": status,
         "location": {"start": {"line": i + 1}},
         "mutatorName": "cxx_ge_to_gt"} for i, status in enumerate(statuses)]}
        for name in ("core/native/api.cpp", "firmware/R2500Adapter/example.h", "apps/windows/ups_state.h")}}


def acceptance(data):
    return {"version": 1, "files": {name: {
        "sha256": report.source_digest(file["source"]),
        "mutants": [{"id": m["id"], "replacement": m["replacement"],
                     "category": "equivalent", "reason": "Equal operands yield the same value."}
                    for m in file["mutants"] if m["status"] == "Survived"]}
        for name, file in data["files"].items()}}


class MutationReportTests(unittest.TestCase):
    def test_reviewed_survivors_keep_original_score(self):
        data = sample("Killed", "Survived")
        original = copy.deepcopy(data)
        review, text = report.review_survivors(data, acceptance(data))
        self.assertEqual(len(review["accepted"]), 3)
        self.assertEqual(review["unexpected"], [])
        self.assertEqual(review["inactive"], [])
        self.assertEqual(data, original)
        self.assertIn("50.00%", report.summarize(data, Path.cwd()))
        self.assertIn("Accepted survivors: **3**", text)

    def test_changed_source_identity_and_coverage_need_review(self):
        baseline_data = sample("Survived")
        baseline = acceptance(baseline_data)
        for change in ("source", "operator", "replacement", "uncovered"):
            with self.subTest(change=change):
                data = copy.deepcopy(baseline_data)
                file = data["files"]["core/native/api.cpp"]
                if change == "source":
                    file["source"] += "// Changed surrounding invariants\n"
                elif change == "operator":
                    file["mutants"][0]["id"] += ":different"
                elif change == "replacement":
                    file["mutants"][0]["replacement"] = "<"
                else:
                    file["mutants"][0]["status"] = "NoCoverage"
                review, _ = report.review_survivors(data, baseline)
                self.assertEqual(len(review["unexpected"]), 1)
                self.assertEqual(len(review["accepted"]), 2)
                self.assertEqual(len(review["inactive"]), 1)

    def test_new_survivor_fails_and_fixed_entries_are_inactive(self):
        data = sample("Killed", "Survived")
        baseline = acceptance(data)
        file = data["files"]["core/native/api.cpp"]
        file["mutants"][0]["status"] = "Survived"
        file["mutants"][1]["status"] = "Killed"
        review, _ = report.review_survivors(data, baseline)
        self.assertEqual(len(review["unexpected"]), 1)
        self.assertEqual(review["unexpected"][0]["id"], file["mutants"][0]["id"])
        self.assertEqual(len(review["inactive"]), 1)

    def test_acceptance_requires_unique_reasoned_entries(self):
        for change in ("reason", "category", "duplicate", "version", "digest"):
            with self.subTest(change=change):
                data = sample("Survived")
                baseline = acceptance(data)
                file = baseline["files"]["core/native/api.cpp"]
                if change in {"reason", "category"}:
                    file["mutants"][0][change] = ""
                elif change == "duplicate":
                    file["mutants"].append(copy.deepcopy(file["mutants"][0]))
                elif change == "version":
                    baseline["version"] = 2
                else:
                    file["sha256"] = "bad"
                with self.assertRaises(ValueError):
                    report.review_survivors(data, baseline)

    def test_source_digest_ignores_checkout_newline_style(self):
        self.assertEqual(report.source_digest("a\nb\n"), report.source_digest("a\r\nb\r\n"))

    def test_score_includes_uncovered_and_reports_timeouts(self):
        text = report.summarize(sample("Killed", "Survived", "Timeout", "NoCoverage"), Path.cwd())
        self.assertIn("50.00%", text)
        self.assertIn("(6/12)", text)
        self.assertIn("| Timeout | 3 |", text)
        self.assertIn("core/native/api.cpp:2", text)

    def test_missing_and_erroneous_results_fail(self):
        for data in ({"files": {}}, sample("RuntimeError"), sample("CompileError")):
            with self.subTest(data=data), self.assertRaises(ValueError):
                report.summarize(data, Path.cwd())
        data = sample("Killed")
        del data["files"]["core/native/api.cpp"]
        with self.assertRaises(ValueError):
            report.summarize(data, Path.cwd())

    def test_missing_shared_library_is_not_hidden_by_header_results(self):
        data = sample("Killed")
        data["files"]["core/src/example.h"] = data["files"].pop("core/native/api.cpp")
        with self.assertRaisesRegex(ValueError, "Missing shared-library"):
            report.summarize(data, Path.cwd())

    def test_offline_html_escapes_source(self):
        data = sample("Survived")
        for file in data["files"].values():
            file["source"] = "a < b; // <script>"
            file["mutants"][0]["replacement"] = "a <= b"
        html = report.standalone_html(data)
        self.assertIn("&lt;script&gt;", html)
        self.assertNotIn("<script>", html)
        self.assertNotIn("https://", html)

    def test_test_sources_cannot_inflate_score(self):
        data = sample("Killed")
        data["files"]["tests/native/example.cpp"] = {"mutants": []}
        with self.assertRaisesRegex(ValueError, "Unexpected mutation source"):
            report.summarize(data, Path.cwd())

    def test_baseline_failure_stops_execution_and_retains_log(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "baseline.log"
            with self.assertRaisesRegex(RuntimeError, "Command failed"):
                report.run([sys.executable, "-c", "print('baseline failed'); exit(1)"], log, temp)
            self.assertIn("baseline failed", log.read_text())


if __name__ == "__main__":
    unittest.main()
