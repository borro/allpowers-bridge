"""GitHub event selection and final gate. No product build orchestration."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import urllib.parse
import urllib.request

JOBS = ("portable", "sanitizers", "fuzz", "valgrind", "mutation",
        "coverage-linux", "windows-sdk", "windows", "coverage-windows", "firmware")
LEGAL_DOCS = {"docs/firmware-licenses.md", "docs/windows-runtime-terms.md", "docs/relinking.md"}


def documentation_only(paths):
    def allowed(path):
        if path in LEGAL_DOCS or path.startswith("docs/licenses/"):
            return False
        return (path in {"README.md", "README.ru.md", "README.zh-CN.md", "README.zh-TW.md"}
                or path.startswith("docs/") and path.endswith(".md")
                or path.startswith("docs/images/") and path.endswith(".png"))
    return bool(paths) and all(allowed(path) for path in paths)


def select_mode(event_name, ref, paths=None, delegated=False):
    if event_name == "workflow_dispatch" or ref.startswith("refs/tags/"):
        return "full"
    if delegated:
        return "delegated"
    return "docs" if paths is not None and documentation_only(paths) else "full"


def gate(mode, needs):
    errors = []
    if mode not in {"full", "docs"}:
        return ["No independently verified CI mode"]
    expected = {"audit": "success", **{job: "success" if mode == "full" else "skipped" for job in JOBS}}
    for job, result in expected.items():
        actual = needs.get(job, {}).get("result", "missing")
        if actual != result:
            errors.append(f"{job}: expected {result}, got {actual}")
    return errors


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def api(path):
    request = urllib.request.Request(f"https://api.github.com/repos/{os.environ['GITHUB_REPOSITORY']}/{path}",
        headers={"Authorization": f"Bearer {os.environ['GH_TOKEN']}",
                 "Accept": "application/vnd.github+json", "User-Agent": "allpowers-ci"})
    with urllib.request.urlopen(request, timeout=20) as response:
        return json.load(response)


def verified_base(branch):
    query = urllib.parse.urlencode({"branch": branch, "event": "push", "status": "success", "per_page": 30})
    runs = api(f"actions/workflows/build.yml/runs?{query}")["workflow_runs"]
    for run in runs:
        if str(run["id"]) == os.environ["GITHUB_RUN_ID"]:
            continue
        # Delegated branch runs are not evidence that their head was tested.
        jobs = api(f"actions/runs/{run['id']}/jobs?per_page=100")["jobs"]
        if any(j["name"] == "CI result" and j["conclusion"] == "success" for j in jobs):
            candidate = run["head_sha"]
            if subprocess.run(["git", "merge-base", "--is-ancestor", candidate, "HEAD"],
                              capture_output=True).returncode == 0:
                return candidate
    return None


def plan(event_name, ref, event):
    paths, delegated = None, False
    try:
        if event_name == "pull_request":
            base = git("merge-base", event["pull_request"]["base"]["sha"], "HEAD")
        elif event_name == "push" and ref.startswith("refs/heads/"):
            branch = ref.removeprefix("refs/heads/")
            if branch != event["repository"]["default_branch"]:
                owner = event["repository"]["owner"].get("login") or event["repository"]["owner"]["name"]
                query = urllib.parse.urlencode({"state": "open", "head": f"{owner}:{branch}", "per_page": 100})
                pulls = api(f"pulls?{query}")
                for pr in pulls:
                    if (pr["head"]["sha"] == os.environ["GITHUB_SHA"]
                            and pr["head"]["repo"]["full_name"] == os.environ["GITHUB_REPOSITORY"]):
                        detail = api(f"pulls/{pr['number']}")
                        # Conflicting PRs do not run pull_request workflows.
                        # Unknown mergeability or a changing head means full CI.
                        if detail.get("mergeable") is True and detail["head"]["sha"] == os.environ["GITHUB_SHA"]:
                            delegated = True
                            break
            base = None if delegated else verified_base(branch)
        else:
            base = None
        if base:
            # --no-renames retains both sides of a rename in classification.
            paths = git("diff", "--name-only", "--no-renames", "-z", base, "HEAD").split("\0")
            paths = [path for path in paths if path]
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError):
        # Fail open into the expensive checks, never into a false green skip.
        print("::warning::Cannot establish a verified change range; running full CI")
        paths, delegated = None, False
    return select_mode(event_name, ref, paths, delegated)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("plan", "gate"))
    args = parser.parse_args()
    if args.command == "gate":
        errors = gate(os.environ.get("CI_MODE", ""), json.loads(os.environ["CI_NEEDS"]))
        if errors:
            raise SystemExit("\n".join(errors))
        print("All expected CI results verified")
        return
    event_name, ref = os.environ["GITHUB_EVENT_NAME"], os.environ["GITHUB_REF"]
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    mode = plan(event_name, ref, event)
    release = event_name == "push" and ref.startswith("refs/tags/v")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"mode={mode}\nrelease={str(release).lower()}\n")


if __name__ == "__main__":
    main()
