"""Exercise release publication offline, with an isolated Git repo and fake API."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from urllib.parse import unquote


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def fake_gh(arguments):
    path = Path(os.environ["ALLPOWERS_RELEASE_TEST_STATE"])
    state = json.loads(path.read_text(encoding="utf-8"))
    state["calls"].append(arguments)

    def save():
        path.write_text(json.dumps(state), encoding="utf-8")

    save()
    mode = state["mode"]
    assert arguments[0] == "api", "Do not rediscover a draft through gh release"
    endpoint = arguments[1]
    method = arguments[arguments.index("--method") + 1] if "--method" in arguments else "GET"
    if "/commits/" in endpoint:
        if mode == "missing-tag":
            raise RuntimeError("Unknown tag (HTTP 404)")
        print("0" * 40 if mode == "moved-tag" and state["uploaded"] else state["commit"])
        return

    files = sorted(p for p in Path(state["assets"]).iterdir() if p.is_file())

    def release():
        remote = []
        for index, file in enumerate(files, 1):
            if file.name in state["uploadedNames"]:
                hash_value = digest(file)
                if mode in ("bad-digest", "published-mismatch") or (
                        mode == "resume-draft" and index == 2 and not state["replaced"]):
                    hash_value = "bad"
                remote.append(dict(id=100 + index, name=file.name, state="uploaded",
                                   size=file.stat().st_size, digest="sha256:" + hash_value))
        if mode == "partial-upload" and remote:
            remote = remote[1:]
        if mode == "foreign-draft":
            remote.append(dict(name="foreign.bin", state="uploaded", size=1, digest="unknown"))
        return dict(id=42, tag_name=state["tag"], draft=state["draft"],
                    target_commitish=state["commit"], prerelease="-" in state["tag"].split("+", 1)[0],
                    body=state["body"], assets=remote)

    if endpoint.endswith("/releases?per_page=100") and method == "GET":
        if mode == "lookup-forbidden":
            raise RuntimeError("Forbidden (HTTP 403)")
        assert "--paginate" in arguments and "--slurp" in arguments
        if not state["exists"] or mode == "stale-list-after-create":
            print("[[]]")
            return
        page = [release()]
        if mode == "duplicate-tag":
            page.append(release())
        pages = [page]
        if mode == "resume-draft":
            pages.insert(0, [dict(id=41, tag_name="v0.0.1", draft=False)])
        print(json.dumps(pages))
        return
    if endpoint == "repos/example/project/releases" and method == "POST":
        assert not state["exists"], "Retry must resume existing draft"
        request = json.loads(Path(arguments[arguments.index("--input") + 1]).read_text(encoding="utf-8"))
        assert "target_commitish" not in request, "Old workflow target requires workflow-write permission"
        assert request["tag_name"] == state["tag"] and request["draft"] is True
        assert request["prerelease"] == ("-" in state["tag"].split("+", 1)[0])
        assert f'<!-- allpowers-release-commit: {state["commit"]} -->' in request["body"]
        assert "- Synthetic version-specific change." in request["body"]
        assert "FIRMWARE-SOURCES.md" in request["body"]
        assert f'https://github.com/example/project/blob/{state["tag"]}/cmake/Dependencies.cmake' in request["body"]
        state.update(exists=True, draft=True, body=request["body"])
        save()
        result = release()
        if mode == "created-wrong-tag":
            result["tag_name"] = "v9.9.9"
        elif mode == "created-wrong-commit":
            result["body"] = "Unrelated draft"
        elif mode == "created-no-id":
            result["id"] = None
        elif mode == "created-not-draft":
            result["draft"] = False
        print(json.dumps(result))
        return
    assert state["exists"], "Cannot operate on unknown draft"
    if endpoint == "repos/example/project/releases/assets/102" and method == "DELETE":
        assert mode == "resume-draft", "Unexpected asset deletion"
        state["replaced"] = True
        state["uploadedNames"].remove(files[1].name)
        save()
        return
    if endpoint.startswith("https://uploads.github.com/repos/example/project/releases/42/assets?name=") and method == "POST":
        if mode == "upload-failure":
            raise RuntimeError("Synthetic upload interruption")
        file = Path(arguments[arguments.index("--input") + 1])
        name = unquote(endpoint.split("?name=", 1)[1])
        assert name == file.name and "Content-Type: application/octet-stream" in arguments
        assert name not in state["uploadedNames"], "Unchanged assets must not be uploaded again"
        state["uploaded"] = True
        state["uploadedNames"].append(name)
        save()
        print("{}")
        return
    if endpoint == "repos/example/project/releases/42":
        if method == "PATCH":
            prerelease = "-" in state["tag"].split("+", 1)[0]
            for field in ("draft=false", f"prerelease={str(prerelease).lower()}",
                          f"make_latest={str(not prerelease).lower()}"):
                assert field in arguments, "Incorrect publication flags"
            bodies = [arg[5:] for arg in arguments if arg.startswith("body=")]
            assert len(bodies) == 1 and "- Synthetic version-specific change." in bodies[0]
            state.update(body=bodies[0], draft=False, published=True)
            save()
        else:
            assert method == "GET"
        result = release()
        if mode == "changed-id":
            result["id"] = 43
        print(json.dumps(result))
        return
    raise RuntimeError("Unexpected fake API invocation")


def run_tests(root, work, assets, tag):
    work.mkdir(parents=True, exist_ok=True)
    repo = work / "repo"
    repo.mkdir(exist_ok=True)
    previous = Path.cwd()
    previous_state = os.environ.get("ALLPOWERS_RELEASE_TEST_STATE")
    try:
        os.chdir(repo)
        for args in (("init", "-q"), ("config", "user.name", "Release Test"),
                     ("config", "user.email", "release@example.invalid"),
                     ("config", "commit.gpgsign", "false"), ("config", "core.hooksPath", str(work / "no-hooks"))):
            subprocess.run(["git", *args], check=True)
        (repo / "README.md").write_text("Synthetic publisher test\n", encoding="utf-8")
        subprocess.run(["git", "add", "README.md"], check=True)
        subprocess.run(["git", "commit", "--allow-empty", "-qm", "Synthetic release fixture"], check=True)
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
        spec = importlib.util.spec_from_file_location("publisher", root / "tools/release/publish_release.py")
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)
        modes = ("stale-list-after-create", "success", "resume-draft", "upload-failure", "partial-upload", "bad-digest",
                 "created-wrong-tag", "created-wrong-commit", "created-no-id", "created-not-draft", "changed-id",
                 "moved-tag", "missing-tag", "lookup-forbidden", "foreign-draft", "old-draft", "duplicate-tag",
                 "published-match", "published-mismatch", "missing-notes", "empty-notes", "wrong-version-notes",
                 "no-changes-notes", "duplicate-checksum", "missing-checksum", "checksum-mismatch",
                 "redundant-runtime", "redundant-sidecar")
        for mode in modes:
            mode_assets = assets
            if mode in ("duplicate-checksum", "missing-checksum", "checksum-mismatch", "redundant-runtime", "redundant-sidecar"):
                mode_assets = work / (mode + "-assets")
                mode_assets.mkdir(exist_ok=True)
                for path in assets.iterdir():
                    if path.is_file():
                        shutil.copyfile(path, mode_assets / path.name)
                sums = mode_assets / "SHA256SUMS"
                records = sums.read_text(encoding="utf-8-sig").splitlines()
                if mode == "duplicate-checksum":
                    records.append(records[0])
                elif mode == "missing-checksum":
                    records.pop(0)
                elif mode == "checksum-mismatch":
                    records[0] = "0" * 64 + records[0][64:]
                elif mode == "redundant-runtime":
                    (mode_assets / "AllpowersMonitor-redundant-win-x64.zip").write_text("Duplicate EXEs")
                else:
                    (mode_assets / "duplicate.sha256").write_text(records[0])
                sums.write_text("\n".join(records) + "\n", encoding="utf-8")
            state_path = work / (mode + ".json")
            os.environ["ALLPOWERS_RELEASE_TEST_STATE"] = str(state_path)
            published = mode.startswith("published-")
            names = sorted(path.name for path in assets.iterdir() if path.is_file())
            state = dict(mode=mode, commit=commit, assets=str(mode_assets), calls=[], published=False, tag=tag,
                         exists=published or mode in ("resume-draft", "foreign-draft", "old-draft", "duplicate-tag"),
                         uploaded=published, draft=not published, replaced=False,
                         uploadedNames=names if published else names[:2] if mode == "resume-draft" else [],
                         body=f'<!-- allpowers-release-commit: {"0" * 40 if mode == "old-draft" else commit} -->')
            state_path.write_text(json.dumps(state), encoding="utf-8")
            notes = work / (mode + "-notes.md")
            content = f"# ALLPOWERS Bridge {tag}\n\n- Synthetic version-specific change.\n"
            if mode == "empty-notes":
                content = ""
            elif mode == "wrong-version-notes":
                content = "# ALLPOWERS Bridge v9.9.9\n\n- Unrelated changes.\n"
            elif mode == "no-changes-notes":
                content = f"# ALLPOWERS Bridge {tag}\n\n"
            if mode != "missing-notes":
                notes.write_text(content, encoding="utf-8")
            success = True
            try:
                publisher.publish("example/project", tag, mode_assets, notes,
                                  (sys.executable, "-I", "-S", "-B", str(Path(__file__).resolve()), "--fake-gh"))
            except (ValueError, OSError, subprocess.CalledProcessError, KeyError, TypeError) as error:
                success = False
                (work / (mode + ".log")).write_text(str(error), encoding="utf-8")
            expected = mode in ("stale-list-after-create", "success", "resume-draft", "published-match")
            assert success == expected, f"Wrong publisher result for {mode}; inspect test log"
            state = json.loads(state_path.read_text(encoding="utf-8"))
            mutations = [call for call in state["calls"] if "--method" in call
                         and call[call.index("--method") + 1] in ("POST", "DELETE", "PATCH")]
            if published or mode in ("missing-tag", "lookup-forbidden", "foreign-draft", "old-draft", "duplicate-tag",
                                     "missing-notes", "empty-notes", "wrong-version-notes", "no-changes-notes",
                                     "duplicate-checksum", "missing-checksum", "checksum-mismatch", "redundant-runtime", "redundant-sidecar"):
                assert not mutations, f"Existing/unknown release was changed: {mode}"
            if mode in ("stale-list-after-create", "success", "resume-draft"):
                assert state["published"] and len(state["uploadedNames"]) == 6
                assert sum(call[1].endswith("/releases?per_page=100") for call in state["calls"]) == 1
                assert mode != "resume-draft" or state["replaced"], "Interrupted upload was not repaired"
            elif mode.startswith("created-"):
                assert len(mutations) == 1, "Invalid creation response must stop before upload"
            else:
                assert not state["published"], f"Incomplete/mismatched release published: {mode}"
        print(f"{len(modes)} publication scenarios passed (mock API only)")
    finally:
        os.chdir(previous)
        if previous_state is None:
            os.environ.pop("ALLPOWERS_RELEASE_TEST_STATE", None)
        else:
            os.environ["ALLPOWERS_RELEASE_TEST_STATE"] = previous_state


if __name__ == "__main__":
    if sys.argv[1:2] == ["--fake-gh"]:
        fake_gh(sys.argv[2:])
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--root", type=Path, required=True)
        parser.add_argument("--work", type=Path, required=True)
        parser.add_argument("--assets", type=Path, required=True)
        parser.add_argument("--tag", required=True)
        args = parser.parse_args()
        run_tests(args.root.resolve(), args.work.resolve(), args.assets.resolve(), args.tag)
