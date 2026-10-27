import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("ci_policy", Path(__file__).resolve().parents[2] / "cmake/ci_policy.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class PolicyTests(unittest.TestCase):
    def test_docs_and_legal_inputs(self):
        self.assertTrue(ci.documentation_only(["README.ru.md", "docs/building.md"]))
        for path in ["cmake/A.cmake", "docs/firmware-licenses.md", "docs/relinking.md",
                     "docs/windows-runtime-terms.md", "docs/licenses/GPL-3.0.md", "docs/new.py",
                     "THIRD_PARTY_NOTICES.md", ".github/workflows/build.yml"]:
            self.assertFalse(ci.documentation_only(["README.md", path]), path)

    def test_selection(self):
        self.assertEqual(ci.select_mode("push", "refs/heads/a", None), "full")
        self.assertEqual(ci.select_mode("push", "refs/heads/a", [], True), "delegated")
        self.assertEqual(ci.select_mode("pull_request", "refs/pull/1/merge", ["README.md"]), "docs")
        for event, ref in [("push", "refs/tags/v1.0.0"), ("push", "refs/tags/other"),
                           ("workflow_dispatch", "refs/heads/main")]:
            self.assertEqual(ci.select_mode(event, ref, ["README.md"], True), "full")

    def test_every_required_failure_is_rejected(self):
        needs = {job: {"result": "success"} for job in ("audit", *ci.JOBS)}
        self.assertEqual(ci.gate("full", needs), [])
        for job in needs:
            for result in ("failure", "cancelled", "skipped", "missing"):
                broken = {**needs, job: {"result": result}}
                self.assertTrue(ci.gate("full", broken), (job, result))

    def test_docs_gate_is_explicit(self):
        needs = {job: {"result": "skipped"} for job in ci.JOBS}
        needs["audit"] = {"result": "success"}
        self.assertFalse(ci.gate("docs", needs))
        self.assertTrue(ci.gate("delegated", needs))
        del needs["firmware"]
        self.assertTrue(ci.gate("docs", needs))

    def test_api_failure_falls_back_to_full(self):
        with patch.object(ci, "verified_base", side_effect=OSError("offline")):
            self.assertEqual(ci.plan("push", "refs/heads/main", {"repository": {"default_branch": "main"}}), "full")

    def test_push_uses_verified_base_not_previous_push(self):
        with patch.object(ci, "verified_base", return_value="verified") as base, \
             patch.object(ci, "git", return_value="core/changed.cpp\0README.md\0") as git:
            self.assertEqual(ci.plan("push", "refs/heads/main", {"before": "untested", "repository": {"default_branch": "main"}}), "full")
            base.assert_called_once_with("main")
            self.assertEqual(git.call_args.args[-2:], ("verified", "HEAD"))

    def test_verified_history_ignores_delegated_and_nonancestor_runs(self):
        runs = {"workflow_runs": [{"id": 3, "head_sha": "delegated"},
                                  {"id": 2, "head_sha": "rewritten"},
                                  {"id": 1, "head_sha": "verified"}]}
        replies = [runs, {"jobs": [{"name": "CI result", "conclusion": "skipped"}]},
                   {"jobs": [{"name": "CI result", "conclusion": "success"}]},
                   {"jobs": [{"name": "CI result", "conclusion": "success"}]}]
        with patch.dict(ci.os.environ, {"GITHUB_RUN_ID": "4"}), \
             patch.object(ci, "api", side_effect=replies), \
             patch.object(ci.subprocess, "run") as process:
            process.side_effect = [ci.subprocess.CompletedProcess([], 1), ci.subprocess.CompletedProcess([], 0)]
            self.assertEqual(ci.verified_base("main"), "verified")

    def test_branch_pr_delegation_requires_exact_commit(self):
        event = {"repository": {"default_branch": "main", "owner": {"login": "owner"}}}
        pull = {"number": 1, "mergeable": True, "head": {"sha": "current", "repo": {"full_name": "owner/repo"}}}
        with patch.dict(ci.os.environ, {"GITHUB_SHA": "current", "GITHUB_REPOSITORY": "owner/repo"}), \
             patch.object(ci, "api", side_effect=lambda path: [pull] if "?" in path else pull), \
             patch.object(ci, "verified_base", return_value=None):
            self.assertEqual(ci.plan("push", "refs/heads/topic", event), "delegated")
            for mergeable in [False, None]:
                pull["mergeable"] = mergeable
                self.assertEqual(ci.plan("push", "refs/heads/topic", event), "full")
            pull["mergeable"] = True
            pull["head"]["sha"] = "previous"
            self.assertEqual(ci.plan("push", "refs/heads/topic", event), "full")
