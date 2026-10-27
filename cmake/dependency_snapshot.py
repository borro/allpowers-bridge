"""Publish the pinned C/C++ inventory, not a claim of a complete binary SBOM."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import quote, urlsplit

MANIFEST = "cmake/Dependencies.cmake"


def read_dependencies(root, cmake):
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "dependencies.json"
        subprocess.run([cmake, f"-DOUTPUT={output}", "-P",
                        str(root / "cmake/ExportDependencies.cmake")], check=True)
        return json.loads(output.read_text(encoding="utf-8"))


def inventory(values):
    def scalar(name):
        items = values[f"ALLPOWERS_{name}"]
        if len(items) != 1 or not items[0]:
            raise ValueError(f"Expected one value for {name}")
        return items[0]

    packages = {}

    def add(name, purl, relationship="indirect", scope="runtime", **metadata):
        if name in packages or not purl.rsplit("@", 1)[-1]:
            raise ValueError(f"Duplicate or unversioned dependency: {name}")
        packages[name] = dict(package_url=purl, relationship=relationship,
                              scope=scope, dependencies=[], metadata=metadata)

    def github(owner, repo, revision):
        return f"pkg:github/{owner.lower()}/{repo.lower()}@{quote(revision, safe='')}"

    version = scalar("QT_VERSION")
    for module in ("qtbase", "qtconnectivity", "qttools"):
        add(f"qt/{module}", github("qt", module, f"v{version}"), "direct",
            "development" if module == "qttools" else "runtime",
            source_url=f"{scalar('QT_SOURCE_URL')}/{module}-everywhere-src-{version}.tar.xz",
            sha256=scalar(f"{module.upper()}_SHA256"))
    for sdk, variable in (("windows-sdk", "WINDOWS_SDK_VERSION"), ("wdk", "WDK_VERSION")):
        add(f"microsoft/{sdk}", f"pkg:generic/microsoft/{sdk}@{scalar(variable)}",
            "direct", "development")

    builder = "espressif/esp32-arduino-lib-builder"
    # These are vendored SDK components, not separate package-manager installs.
    sources = values["ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES"]
    licenses = values["ALLPOWERS_FIRMWARE_BINARY_LICENSES"]
    for record, evidence in [(x, "source-archive") for x in sources] + [
            (x, "binary-license-source") for x in licenses]:
        parts = record.split("|")
        if len(parts) != (4 if evidence == "source-archive" else 3):
            raise ValueError("Malformed firmware dependency record")
        _, url, checksum = parts[:3]
        if not re.fullmatch(r"[0-9a-f]{64}", checksum):
            raise ValueError("Invalid firmware dependency checksum")
        parsed = urlsplit(url)
        archive = re.fullmatch(r"/repos/([^/]+)/([^/]+)/tarball/([^/]+)", parsed.path)
        license_source = re.fullmatch(r"/([^/]+)/([^/]+)/([^/]+)/LICENSE", parsed.path)
        component = re.fullmatch(r"/components/([^/]+)/([^/]+)/([^/]+)/[^/]+", parsed.path)
        if parsed.scheme != "https" or parsed.query or parsed.fragment:
            raise ValueError(f"Unsupported dependency URL: {url}")
        if parsed.netloc == "api.github.com" and archive:
            owner, repo, revision = archive.groups()
            name, purl = f"{owner}/{repo}".lower(), github(owner, repo, revision)
        elif parsed.netloc == "raw.githubusercontent.com" and license_source:
            owner, repo, revision = license_source.groups()
            name, purl = f"{owner}/{repo}".lower(), github(owner, repo, revision)
        elif parsed.netloc == "components-file.espressif.com" and component:
            owner, repo, revision = component.groups()
            name = f"{owner}/{repo}"
            purl = f"pkg:generic/{owner}/{repo}@{quote(revision, safe='')}"
        else:
            raise ValueError(f"Unsupported dependency URL: {url}")
        add(name, purl, "direct" if name == "espressif/arduino-esp32" else "indirect",
            "development" if name == builder else "runtime",
            source_url=url, sha256=checksum, evidence=evidence)

    arduino = "espressif/arduino-esp32"
    if packages[arduino]["package_url"] != github("espressif", "arduino-esp32", scalar("ESP32_VERSION")):
        raise ValueError("Arduino source archive disagrees with ESP32_VERSION")
    if packages["espressif/esp-idf"]["package_url"] != github("espressif", "esp-idf", scalar("ESP32_IDF_REVISION")):
        raise ValueError("ESP-IDF source archive disagrees with ESP32_IDF_REVISION")
    # Model SDK containment; do not invent a full upstream component graph.
    packages[arduino]["dependencies"] = [item["package_url"] for item in packages.values()
                                         if item["relationship"] == "indirect"]
    for module in ("qtconnectivity", "qttools"):
        packages[f"qt/{module}"]["dependencies"] = [packages["qt/qtbase"]["package_url"]]
    return packages


def snapshot(values, env, scanned=None):
    sha, ref = env["GITHUB_SHA"], env["GITHUB_REF"]
    if not re.fullmatch(r"[0-9a-f]{40}", sha) or not ref.startswith("refs/"):
        raise ValueError("Expected a full commit SHA and Git ref")
    repository = env["GITHUB_REPOSITORY"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid GitHub repository")
    server = env.get("GITHUB_SERVER_URL", "https://github.com")
    packages = inventory(values)
    return dict(version=0, sha=sha, ref=ref,
                job=dict(correlator="allpowers-cpp-dependencies",
                         id=f"{env['GITHUB_RUN_ID']}-{env.get('GITHUB_RUN_ATTEMPT', '1')}",
                         html_url=f"{server}/{repository}/actions/runs/{env['GITHUB_RUN_ID']}"),
                detector=dict(name="allpowers-cmake", version="1.0.0",
                              url=f"{server}/{repository}/blob/{sha}/cmake/dependency_snapshot.py"),
                scanned=scanned or datetime.now(timezone.utc).isoformat(timespec="seconds"),
                manifests={MANIFEST: dict(name=MANIFEST, file=dict(source_location=MANIFEST),
                                         resolved={item["package_url"]: item for item in packages.values()})})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = snapshot(read_dependencies(args.root, args.cmake), os.environ)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    count = len(result["manifests"][MANIFEST]["resolved"])
    print(f"Generated {count} pinned C/C++ dependencies: {args.output}")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as output:
            output.write(f"## C/C++ dependency snapshot\n\nValidated {count} pinned dependencies "
                         f"from `{MANIFEST}` for `{result['sha']}`.\n")


if __name__ == "__main__":
    main()
