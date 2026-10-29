"""Publish already-built, verified assets; no build or device operations."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import quote


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def publish(repository, tag, assets, notes_file=None, github_cli=("gh",)):
    require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository), "Invalid repository")
    require(re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", tag),
            "Invalid release tag")
    assets = Path(assets).resolve(strict=True)
    files = sorted(path for path in assets.iterdir() if path.is_file())
    asset_version = tag[1:].replace("+", "_")
    payloads = {
        f"AllpowersMonitor-{asset_version}-sources-with-qt.zip",
        f"AllpowersMonitor-{asset_version}-win-x64.exe",
        f"AllpowersWidget-{asset_version}-win-x64.exe",
        f"AllpowersFirmware-{asset_version}-waveshare-147.zip",
        f"AllpowersFirmware-{asset_version}-pocket-096.zip",
    }
    names = []
    for line in (assets / "SHA256SUMS").read_text(encoding="utf-8-sig").splitlines():
        match = re.fullmatch(r"([a-f0-9]{64})  ([A-Za-z0-9._+-]+)", line)
        require(match, "Invalid SHA256SUMS")
        digest, name = match.groups()
        require(name in payloads, "Release asset version/name does not match the tag")
        require(sha256(assets / name) == digest, f"Local release checksum mismatch: {name}")
        names.append(name)
    require(len(names) == len(payloads) and set(names) == payloads,
            "SHA256SUMS must cover each release payload exactly once")
    expected = payloads | {"SHA256SUMS"}
    require({path.name for path in files} == expected, "Unexpected or incomplete release assets")

    def gh(*arguments):
        return subprocess.run([*github_cli, *arguments], check=True, capture_output=True,
                              text=True, encoding="utf-8").stdout

    def api(*arguments):
        return json.loads(gh("api", *arguments))

    commit = subprocess.run(["git", "rev-parse", "HEAD"], check=True, capture_output=True,
                            text=True, encoding="utf-8").stdout.strip()
    marker = f"<!-- allpowers-release-commit: {commit} -->"
    encoded = quote(tag, safe="")
    endpoint = f"repos/{repository}/releases"

    def confirm_remote_tag():
        remote = gh("api", f"repos/{repository}/commits/{encoded}", "--jq", ".sha").strip()
        require(remote == commit, "Remote tag moved or does not identify the built commit")

    def assert_draft(release):
        require(type(release.get("id")) is int and release["id"] > 0
                and release.get("tag_name") == tag and release.get("draft") is True
                and marker in (release.get("body") or ""),
                "Draft identity does not match this tag and commit")
        require(all(asset["name"] in expected for asset in release["assets"]),
                "Existing draft contains unexpected assets; refusing to modify it")

    def matching(asset, path):
        return (asset.get("state") == "uploaded" and asset.get("size") == path.stat().st_size
                and asset.get("digest") == "sha256:" + sha256(path))

    def assert_uploaded(release):
        require(len(release["assets"]) == len(files), "Uploaded release asset count mismatch")
        for path in files:
            remote = [asset for asset in release["assets"] if asset["name"] == path.name]
            require(len(remote) == 1 and matching(remote[0], path),
                    f"Uploaded release asset failed verification: {path.name}")

    confirm_remote_tag()
    # Authenticated paginated listing includes drafts, unlike the tag endpoint.
    pages = api(endpoint + "?per_page=100", "--paginate", "--slurp")
    matches = [item for page in pages for item in page if item["tag_name"] == tag]
    require(len(matches) <= 1, "Multiple releases identify this tag")
    release = matches[0] if matches else None
    if release:
        if release["draft"] is False:
            assert_uploaded(release)
            print(f"Release already published with matching assets: {tag}")
            return
        assert_draft(release)
    notes_file = Path(notes_file) if notes_file else Path(__file__).resolve().parent.parent / "docs/releases" / f"{tag}.md"
    changes = notes_file.read_text(encoding="utf-8-sig").strip()
    require(changes.startswith(f"# ALLPOWERS Bridge {tag}\n"),
            "Release notes must start with a heading matching the exact tag")
    require(re.search(r"^- \S", changes, re.MULTILINE), "Release notes must describe the changes in this version")
    body = f"""{changes}

## Downloads

Built and tested from **{tag}** on Windows and Ubuntu.
{marker}

- Windows Monitor and desktop Widget: two standalone EXEs.
- Separate Waveshare 1.47 and Pocket 0.96 firmware packages, with matching board manifests.
- Firmware packages contain installation images, manifests, a licenses/ folder with a component table and retained copyright/NOTICE records, and FIRMWARE-SOURCES.md with exact source/dependency links and rebuild instructions.
- Firmware project sources: [{tag}](https://github.com/{repository}/tree/{tag}). Pinned third-party source archives and SDK versions: [Dependencies.cmake](https://github.com/{repository}/blob/{tag}/cmake/Dependencies.cmake). See the [firmware build and modification guide](https://github.com/{repository}/blob/{tag}/docs/firmware-licenses.md).
- Complete sources for rebuilding (application + Qt): AllpowersMonitor-{asset_version}-sources-with-qt.zip.
- One SHA256SUMS file covering all five downloads.

GitHub's automatic Source code archives contain only the repository.

The applications work without the optional Windows UPS driver. That integration remains
experimental, unsigned and unverified in a VM. Firmware updates require the matching
board profile; settings and private recovery images are not part of these assets.
"""
    prerelease = "-" in tag.split("+", 1)[0]
    if release is None:
        stage = assets / ".stage"
        stage.mkdir(exist_ok=True)
        (stage / "release-notes.md").write_text(body, encoding="utf-8")
        request = stage / "create-release.json"
        # The tag must already exist. target_commitish can require unnecessary
        # workflow-write permissions when main has advanced since this commit.
        request.write_text(json.dumps(dict(tag_name=tag, name=f"ALLPOWERS Bridge {tag}",
                                           draft=True, prerelease=prerelease, body=body)), encoding="utf-8")
        confirm_remote_tag()
        # Retain POST's ID; list visibility may lag behind draft creation.
        release = api(endpoint, "--method", "POST", "--input", str(request))
    assert_draft(release)
    release_id = release["id"]
    for path in files:
        existing = [asset for asset in release["assets"] if asset["name"] == path.name]
        require(len(existing) <= 1, "Duplicate draft asset names")
        if existing:
            if matching(existing[0], path):
                continue
            asset_id = existing[0].get("id")
            require(type(asset_id) is int and asset_id > 0, "Invalid draft asset ID")
            gh("api", f"{endpoint}/assets/{asset_id}", "--method", "DELETE")
        gh("api", f"https://uploads.github.com/{endpoint}/{release_id}/assets?name={quote(path.name, safe='')}",
           "--method", "POST", "--header", "Content-Type: application/octet-stream", "--input", str(path))
    uploaded = api(f"{endpoint}/{release_id}")
    assert_draft(uploaded)
    require(uploaded["id"] == release_id, "Draft ID changed during upload")
    assert_uploaded(uploaded)
    confirm_remote_tag()
    published = api(f"{endpoint}/{release_id}", "--method", "PATCH", "-f", "body=" + body,
                    "-F", "draft=false", "-F", f"prerelease={str(prerelease).lower()}",
                    "-f", f"make_latest={str(not prerelease).lower()}")
    require(published["id"] == release_id and published["tag_name"] == tag
            and published["draft"] is False and published["prerelease"] is prerelease
            and published["body"] == body, "Release publication was not confirmed")
    assert_uploaded(published)
    print(f"Published verified release: {tag}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--notes-file", type=Path)
    parser.add_argument("--github-cli", default="gh")
    args = parser.parse_args()
    try:
        publish(args.repository, args.tag, args.assets, args.notes_file, (args.github_cli,))
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError, TypeError) as error:
        print(f"Release publication failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
