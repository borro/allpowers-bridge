"""Publish the pinned C/C++ inventory, not a claim of a complete binary SBOM."""
import argparse
from datetime import datetime, timezone
import hashlib
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
            sha256=scalar(f"{module.upper()}_SHA256"), evidence="source-archive",
            repository_url=f"https://github.com/qt/{module}", source_ref=f"v{version}",
            platform="windows-x64", component="build-tool" if module == "qttools" else "library")
    add("microsoft/windows-sdk", f"pkg:generic/microsoft/windows-sdk@{scalar('WINDOWS_SDK_VERSION')}",
        "direct", "development", evidence="host-sdk-version", component="sdk", platform="windows-x64",
        documentation_url="https://learn.microsoft.com/en-us/windows/apps/windows-sdk/")
    wdk = scalar("WDK_VERSION")
    # SetupWdk.cmake really downloads this NuGet package, so retain its native identity.
    add("microsoft/wdk", f"pkg:nuget/Microsoft.Windows.WDK.x64@{wdk}", "direct", "development",
        source_url=f"https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/{wdk}/microsoft.windows.wdk.x64.{wdk}.nupkg",
        sha256=scalar("WDK_SHA256"), evidence="nuget-package", component="sdk", platform="windows-x64",
        documentation_url="https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk",
        package_url=f"https://www.nuget.org/packages/Microsoft.Windows.WDK.x64/{wdk}")

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
            origin = dict(repository_url=f"https://github.com/{owner}/{repo}", source_ref=revision)
        elif parsed.netloc == "raw.githubusercontent.com" and license_source:
            owner, repo, revision = license_source.groups()
            name, purl = f"{owner}/{repo}".lower(), github(owner, repo, revision)
            origin = dict(repository_url=f"https://github.com/{owner}/{repo}", source_ref=revision)
        elif parsed.netloc == "components-file.espressif.com" and component:
            owner, repo, revision = component.groups()
            name = f"{owner}/{repo}"
            purl = f"pkg:generic/{owner}/{repo}@{quote(revision, safe='')}"
            origin = dict(package_url=f"https://components.espressif.com/components/{owner}/{repo}/versions/{revision}")
            if name == "espressif/esp_diagnostics":
                origin["repository_url"] = "https://github.com/espressif/esp-insights"
        else:
            raise ValueError(f"Unsupported dependency URL: {url}")
        add(name, purl, "direct" if name == "espressif/arduino-esp32" else "indirect",
            "development" if name == builder else "runtime",
            source_url=url, sha256=checksum, evidence=evidence, platform="esp32-s3",
            component="build-tool" if name == builder else "library", **origin)

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
    commit_url = f"{server}/{repository}/blob/{sha}"
    for name, package in packages.items():
        notice = "docs/firmware-licenses.md" if package["metadata"]["platform"] == "esp32-s3" else "THIRD_PARTY_NOTICES.md"
        package["metadata"]["notice_url"] = f"{commit_url}/{notice}"
        check_metadata(package["metadata"])
    manifest_metadata = dict(source_url=f"{commit_url}/{MANIFEST}",
                             languages="C;C++", platforms="windows-x64;esp32-s3",
                             inventory="explicitly-pinned-components", complete_sbom=False)
    snapshot_metadata = dict(repository_url=f"{server}/{repository}",
                             workflow=env.get("GITHUB_WORKFLOW", "C/C++ dependency graph"),
                             run_attempt=env.get("GITHUB_RUN_ATTEMPT", "1"),
                             component_count=len(packages), complete_sbom=False)
    check_metadata(manifest_metadata)
    check_metadata(snapshot_metadata)
    return dict(version=0, sha=sha, ref=ref,
                job=dict(correlator="allpowers-cpp-dependencies",
                         id=f"{env['GITHUB_RUN_ID']}-{env.get('GITHUB_RUN_ATTEMPT', '1')}",
                         html_url=f"{server}/{repository}/actions/runs/{env['GITHUB_RUN_ID']}"),
                detector=dict(name="allpowers-cmake", version="1.1.0",
                              url=f"{server}/{repository}/blob/{sha}/cmake/dependency_snapshot.py"),
                metadata=snapshot_metadata,
                scanned=scanned or datetime.now(timezone.utc).isoformat(timespec="seconds"),
                manifests={MANIFEST: dict(name=MANIFEST, file=dict(source_location=MANIFEST), metadata=manifest_metadata,
                                         resolved={item["package_url"]: item for item in packages.values()})})


def check_metadata(metadata):
    if len(metadata) > 8 or any(not isinstance(value, (str, int, float, bool)) for value in metadata.values()):
        raise ValueError("Dependency Submission metadata requires at most eight scalar values")


def summary(result):
    lines = ["## C/C++ dependency snapshot", "",
             f"Commit: `{result['sha']}`. Inventory of explicitly pinned components.", "",
             "| Component | Version / revision | Scope | Sources / notices |",
             "| --- | --- | --- | --- |"]
    for item in result["manifests"][MANIFEST]["resolved"].values():
        purl = item["package_url"]
        name, revision = purl.split("/", 1)[1].rsplit("@", 1)
        metadata = item["metadata"]
        url = metadata.get("repository_url") or metadata.get("package_url") or metadata["documentation_url"]
        links = []
        if metadata.get("source_url"):
            links.append(f"[Pinned source]({metadata['source_url']})")
        links.append(f"[Notices]({metadata['notice_url']})")
        lines.append(f"| [{name}]({url}) | `{revision}` | {item['scope']} | {' · '.join(links)} |")
    lines.extend(["", "Repository links are explicit metadata. GitHub controls repository mapping and ecosystem labels in its graph UI.", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = snapshot(read_dependencies(args.root, args.cmake), os.environ)
    result["manifests"][MANIFEST]["metadata"]["sha256"] = hashlib.sha256((args.root / MANIFEST).read_bytes()).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    report = summary(result)
    args.output.with_suffix(".md").write_text(report, encoding="utf-8")
    count = len(result["manifests"][MANIFEST]["resolved"])
    print(f"Generated {count} pinned C/C++ dependencies: {args.output}")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as output:
            output.write(report)


if __name__ == "__main__":
    main()
