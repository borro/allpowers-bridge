"""Coverage integrity checks with synthetic source paths, never real devices."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import subprocess
import sys
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("coverage_report", Path(__file__).resolve().parents[2] / "tools/quality/coverage_report.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class CoverageReportTests(unittest.TestCase):
    def test_thresholds_are_per_area_and_include_the_boundary(self):
        files = {"core/example.cpp": {1: 1},
                 "firmware/R2500Adapter/example.h": {i: int(i < 98) for i in range(100)},
                 "apps/windows/example.cpp": {i: int(i < 90) for i in range(100)}}
        self.assertTrue(all(item['passed'] for item in report.check_thresholds(files, "windows")))
        self.assertFalse(report.check_thresholds(files, "linux")[1]['passed'])
        files["firmware/R2500Adapter/example.h"][98] = 1
        self.assertFalse(report.check_thresholds(files, "linux")[1]['passed'])
        files["firmware/R2500Adapter/example.h"][99] = 1
        self.assertTrue(all(item['passed'] for item in report.check_thresholds(files, "linux")))
        files["apps/windows/example.cpp"][89] = 0
        self.assertFalse(report.check_thresholds(files, "windows")[1]['passed'])
        del files["apps/windows/example.cpp"]
        self.assertFalse(report.check_thresholds(files, "windows")[1]['passed'])

    def test_display_rounding_never_excuses_an_uncovered_core_line(self):
        files = {"core/example.cpp": {i: int(i != 0) for i in range(100000)},
                 "firmware/R2500Adapter/example.h": {1: 1}}
        results = report.check_thresholds(files, "linux")
        self.assertFalse(results[0]['passed'])
        self.assertIn("100.00% (99999/100000)", report.threshold_summary(results, "linux"))

    def test_cli_fails_below_threshold_but_preserves_reports(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            files = {"core/example.cpp": {1: 1},
                     "firmware/R2500Adapter/example.h": {1: 1},
                     "apps/windows/example.cpp": {i + 1: int(i < 89) for i in range(100)}}
            for name in files:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("// synthetic source\n", encoding="utf-8")
            raw, output, summary = root / "raw.xml", root / "coverage.xml", root / "summary.md"
            command = [sys.executable, "-I", "-S", "-B", str(Path(report.__file__)),
                       "--root", str(root), "--platform", "windows", "--input", str(raw),
                       "--output", str(output), "--summary", str(summary)]
            report.write_report(files, raw)
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Coverage thresholds failed: apps/windows/", result.stderr)
            self.assertTrue(output.is_file())
            self.assertIn("FAIL", summary.read_text(encoding="utf-8"))
            files["apps/windows/example.cpp"][90] = 1
            report.write_report(files, raw)
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("FAIL", summary.read_text(encoding="utf-8"))
            result = subprocess.run(command + ["--incomplete"], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Incomplete", summary.read_text(encoding="utf-8"))

    def test_merge_paths_and_filter(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ("core/src/Example.h", "apps/windows/widget.cpp", "tests/fake.cpp"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("// synthetic\n", encoding="utf-8")
            xml = ET.Element("coverage")
            sources = ET.SubElement(xml, "sources")
            ET.SubElement(sources, "source").text = str(root)
            for name, lines in [
                (str(root / "core/src/Example.h").replace("/", "\\").upper(), [(1, 0), (2, 1)]),
                ("core/src/Example.h", [(1, 3), (2, 0), (3, 0)]),
                ("apps/windows/widget.cpp", [(1, 0)]),
                ("tests/fake.cpp", [(1, 1)]),
                ("../outside/core/src/Example.h", [(99, 1)]),
            ]:
                cls = ET.SubElement(xml, "class", filename=name)
                entries = ET.SubElement(cls, "lines")
                for number, hits in lines:
                    ET.SubElement(entries, "line", number=str(number), hits=str(hits))
            source = root / "raw.xml"
            ET.ElementTree(xml).write(source)
            files = report.normalize_report(root, [source, source])
            self.assertEqual(files, {"core/src/Example.h": {1: 1, 2: 1, 3: 0}, "apps/windows/widget.cpp": {1: 0}})
            self.assertEqual(report.counts(files), (2, 4))
            output = root / "coverage.xml"
            report.write_report(files, output)
            self.assertEqual(report.normalize_report(root, [output]), files)
            self.assertIn("50.00%", report.summary(files))
            self.assertIn("Incomplete", report.summary(files, incomplete=True))

    def test_empty_report_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "raw.xml"
            source.write_text("<coverage/>", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "no measured product lines"):
                report.normalize_report(Path(temp), [source])


if __name__ == "__main__":
    unittest.main()
