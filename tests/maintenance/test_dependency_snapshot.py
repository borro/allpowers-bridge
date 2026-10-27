import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("dependency_snapshot", ROOT / "cmake/dependency_snapshot.py")
graph = importlib.util.module_from_spec(spec)
spec.loader.exec_module(graph)
CMAKE = os.environ.get("CMAKE_COMMAND", "cmake")
ENV = dict(GITHUB_SHA="a" * 40, GITHUB_REF="refs/heads/main",
           GITHUB_REPOSITORY="example/project", GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="2")


class DependencySnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.values = graph.read_dependencies(ROOT, CMAKE)

    def test_api_contract_and_sdk_containment(self):
        result = graph.snapshot(self.values, ENV, "2026-01-01T00:00:00Z")
        self.assertEqual(result["version"], 0)
        self.assertEqual(result["sha"], ENV["GITHUB_SHA"])
        self.assertEqual(result["job"]["id"], "123-2")
        manifest = result["manifests"][graph.MANIFEST]
        self.assertEqual(manifest["file"]["source_location"], graph.MANIFEST)
        packages = graph.inventory(self.values)
        self.assertEqual(len(packages), 5 + len(self.values["ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES"])
                         + len(self.values["ALLPOWERS_FIRMWARE_BINARY_LICENSES"]))
        purls = {item["package_url"] for item in packages.values()}
        self.assertEqual(len(purls), len(packages))
        self.assertEqual(set(manifest["resolved"]), purls)
        for item in packages.values():
            self.assertIn(item["relationship"], ("direct", "indirect"))
            self.assertIn(item["scope"], ("runtime", "development"))
            self.assertTrue(set(item["dependencies"]) <= purls)
            self.assertIn("@", item["package_url"])
        self.assertEqual(packages["qt/qttools"]["scope"], "development")
        self.assertEqual(packages["microsoft/wdk"]["scope"], "development")
        self.assertEqual(packages["espressif/esp32-arduino-lib-builder"]["scope"], "development")
        firmware = packages["espressif/arduino-esp32"]["dependencies"]
        self.assertIn(packages["espressif/esp-idf"]["package_url"], firmware)
        self.assertIn(packages["espressif/esp32c3-bt-lib"]["package_url"], firmware)
        self.assertEqual(packages["qt/qtconnectivity"]["dependencies"], [packages["qt/qtbase"]["package_url"]])
        self.assertFalse(any("cmake" in purl or "ninja" in purl for purl in purls))

    def test_cmake_expansion_and_version_changes_reach_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(ROOT / "cmake", root / "cmake")
            path = root / graph.MANIFEST
            with path.open("a", encoding="utf-8") as source:
                source.write('\nset(ALLPOWERS_QT_VERSION "9.8.7")\n'
                             'set(ALLPOWERS_QT_SOURCE_URL "https://example.org/${ALLPOWERS_QT_VERSION}")\n')
            packages = graph.inventory(graph.read_dependencies(root, CMAKE))
            self.assertEqual(packages["qt/qtbase"]["package_url"], "pkg:github/qt/qtbase@v9.8.7")
            self.assertIn("https://example.org/9.8.7/", packages["qt/qtbase"]["metadata"]["source_url"])

    def test_rejects_unknown_urls_bad_hashes_and_duplicate_components(self):
        for replacement in ("http://api.github.com", "https://unknown.example", "https://api.github.com:443"):
            values = copy.deepcopy(self.values)
            values["ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES"][0] = values[
                "ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES"][0].replace("https://api.github.com", replacement)
            with self.assertRaises(ValueError):
                graph.inventory(values)
        values = copy.deepcopy(self.values)
        values["ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES"].append(values["ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES"][0])
        with self.assertRaises(ValueError):
            graph.inventory(values)
        values["ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES"] = ["name|https://example.org/a|bad|root"]
        with self.assertRaises(ValueError):
            graph.inventory(values)

    def test_rejects_archive_and_installed_sdk_version_drift(self):
        for variable in ("ALLPOWERS_ESP32_VERSION", "ALLPOWERS_ESP32_IDF_REVISION"):
            values = copy.deepcopy(self.values)
            values[variable] = ["different"]
            with self.assertRaises(ValueError):
                graph.inventory(values)

    def test_rerun_identity_keeps_stable_detector_and_correlator(self):
        first = graph.snapshot(self.values, ENV)
        second = graph.snapshot(self.values, {**ENV, "GITHUB_RUN_ATTEMPT": "3"})
        self.assertEqual(first["detector"], second["detector"])
        self.assertEqual(first["job"]["correlator"], second["job"]["correlator"])
        self.assertNotEqual(first["job"]["id"], second["job"]["id"])

    def test_cli_produces_valid_json_without_sending_a_request(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "nested/snapshot.json"
            env = {**os.environ, **ENV}
            env.pop("GITHUB_STEP_SUMMARY", None)
            subprocess.run([sys.executable, "-I", "-S", "-B", str(ROOT / "cmake/dependency_snapshot.py"),
                            "--cmake", CMAKE, "--output", str(output)], env=env, check=True,
                           capture_output=True, timeout=30)
            self.assertEqual(json.loads(output.read_text())["ref"], ENV["GITHUB_REF"])

    def test_rejects_missing_or_invalid_commit_identity(self):
        for env in ({**ENV, "GITHUB_SHA": "short"}, {**ENV, "GITHUB_REF": "main"},
                    {**ENV, "GITHUB_REPOSITORY": "../invalid/repo"}):
            with self.assertRaises(ValueError):
                graph.snapshot(self.values, env)


if __name__ == "__main__":
    unittest.main()
