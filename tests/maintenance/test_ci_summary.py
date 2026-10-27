import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location("ci_summary", Path(__file__).resolve().parents[2] / "cmake/ci_summary.py")
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class SummaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def test_junit_failures_errors_skips_and_fuzz_counts(self):
        path = self.write("tests.xml", '''<testsuites><testsuite>
          <testcase name="fuzz-ble"><system-out>Done 1234 runs in 120 second(s)</system-out></testcase>
          <testcase name="bad|&lt;script&gt;"><failure>details</failure></testcase>
          <testcase name="crash"><error/></testcase>
          <testcase name="disabled"><skipped/></testcase>
        </testsuite></testsuites>''')
        text = summary.junit(path)
        self.assertIn("4 checks:** 1 passed, 2 failed, 1 skipped", text)
        self.assertIn("bad&#124;&lt;script&gt;", text)
        self.assertIn("1,234 inputs", text)
        self.assertNotIn("<script>", text)

    def test_setup_failure_and_missing_reports_never_look_like_passes(self):
        text = summary.render(self.root, "portable", "failure",
                              {"configure": {"outcome": "failure", "conclusion": "failure"}}, {})
        self.assertIn("portable — failure", text)
        self.assertIn("**failure:** configure", text)
        self.assertIn("Report unavailable or incomplete", text)
        self.assertNotIn("0 passed", text)

    def test_malformed_report_does_not_hide_failure_or_artifact(self):
        self.write("build/native/ci-tests.xml", "<broken")
        text = summary.render(self.root, "portable", "cancelled", {
            "save-test-reports": {"outcome": "success", "outputs": {
                "artifact-url": "https://github.com/owner/repo/actions/runs/1/artifacts/2"}}}, {})
        self.assertIn("cancelled", text)
        self.assertIn("Report unavailable", text)
        self.assertIn("[save test reports](https://github.com/", text)

    def test_python_failure_and_incomplete_log(self):
        path = self.write("python.log", "FAIL: test_failure (Suite)\nRan 3 tests in 0.12s\nFAILED (failures=1)\n")
        self.assertIn("FAILED (failures=1)", summary.unittest_log(path))
        self.assertIn("test_failure (Suite)", summary.unittest_log(path))
        path.write_text("Ran 3 tests in 0.12s\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            summary.unittest_log(path)

    def test_reviewed_survivors_do_not_inflate_mutation_score(self):
        directory = self.root / "mutation"
        self.write("mutation/mutations.json", json.dumps({"files": {"a.cpp": {"mutants": [
            {"status": status} for status in ["Killed", "Timeout", "Survived", "NoCoverage"]]}}}))
        self.write("mutation/review.json", json.dumps({"accepted": [{}], "unexpected": [{}]}))
        text = summary.mutation(directory)
        self.assertIn("50.00%", text)
        self.assertIn("Accepted survivors: **1**", text)
        self.assertIn("uncovered mutations: **1**", text)

    def test_memcheck_uses_current_tag_and_reports_defects(self):
        self.write("Testing/TAG", "current\nExperimental\n")
        self.write("Testing/current/DynamicAnalysis-Test.xml", '''<Site><Testing>
            <Test Status="passed"><Name>clean</Name></Test>
            <Test Status="failed"><Name>leak</Name></Test></Testing></Site>''')
        self.write("Testing/current/DynamicAnalysis.xml", '''<Site><DynamicAnalysis><Test><Results>
            <Defect type="Memory Leak">2</Defect></Results></Test></DynamicAnalysis></Site>''')
        text = summary.memcheck(self.root)
        self.assertIn("1 passed, 1 failed", text)
        self.assertIn("2 reported memory defects", text)

    def test_gate_shows_skips_and_advisory_upload_failures_are_distinct(self):
        text = summary.render(self.root, "ci-result", "success", {
            "codecov": {"outcome": "failure", "conclusion": "success"}},
            {"audit": {"result": "success"}, "windows": {"result": "skipped"}})
        self.assertIn("| windows | skipped |", text)
        self.assertIn("failure (non-blocking)", text)

    def test_sdk_cache_restore_is_distinct_from_validation(self):
        text = summary.render(self.root, "windows-sdk", "failure", {
            "qt": {"outcome": "success", "outputs": {"cache-hit": "true"}},
            "qt-sources": {"outcome": "success", "outputs": {"cache-hit": ""}},
            "wdk": {"outcome": "failure", "conclusion": "success"},
            "prepare-and-validate-complete-sdks": {"outcome": "failure"}}, {})
        self.assertIn("SDK validation: **failure**", text)
        self.assertIn("qt: **hit**", text)
        self.assertIn("qt-sources: **miss**", text)
        self.assertIn("wdk: **unavailable**", text)

    def test_partial_firmware_build_shows_each_board_separately(self):
        base = "build/firmware-linux/dist/firmware/waveshare-147/"
        self.write(base + "manifest.json", json.dumps({"project_version": "1.2.3", "host_platform": "Linux", "files": {"app": "hash"}}))
        self.write(base + "R2500Adapter.ino.bin", "x" * 1024)
        text = summary.render(self.root, "firmware", "failure", {}, {})
        self.assertIn("Version **1.2.3**", text)
        self.assertIn("application **1 KiB**", text)
        self.assertIn("### pocket-096\n\n**Report unavailable", text)

    def test_prepared_release_files_are_not_reported_as_published(self):
        self.write("build/release-assets/app.zip", "zip")
        text = summary.render(self.root, "release", "failure", {
            "prepare-and-verify-release-assets": {"outcome": "success"},
            "publish-complete-release": {"outcome": "failure"}}, {})
        self.assertIn("publication: **failure**", text)
        self.assertIn("| app.zip |", text)


if __name__ == "__main__":
    unittest.main()
