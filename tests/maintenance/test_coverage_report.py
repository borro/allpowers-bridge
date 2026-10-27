"""Coverage integrity checks with synthetic source paths, never real devices."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("coverage_report", Path(__file__).resolve().parents[2] / "cmake/CoverageReport.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class CoverageReportTests(unittest.TestCase):
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
