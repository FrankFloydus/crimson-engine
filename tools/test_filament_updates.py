################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
"""Offline workflow regressions for the daily Filament upstream check."""

import copy
from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timedelta, timezone
import html
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlsplit

import check_filament_updates as checker


def sha(number):
    return f"{number:040x}"


def commit(number, subject=None):
    return {
        "sha": sha(number),
        "html_url": f"https://github.com/google/filament/commit/{sha(number)}",
        "commit": {
            "message": subject or f"Fix renderer issue {number}",
            "committer": {"date": "2026-09-14T09:00:00Z"},
            "author": {"date": "2026-09-14T09:00:00Z"},
        },
    }


def release(version="1.76.1"):
    return {
        "id": 1,
        "tag_name": f"v{version}",
        "name": f"Filament v{version}",
        "html_url": f"https://github.com/google/filament/releases/tag/v{version}",
        "published_at": "2026-09-09T18:00:00Z",
        "draft": False,
        "prerelease": False,
        "body": "Renderer fixes.",
    }


class FakeGitHub:
    """Serve immutable GitHub fixtures without allowing an accidental live call."""

    def __init__(self, head=10):
        self.head = head
        self.latest = release()
        self.comparisons = {}
        self.calls = []
        self.fail = None
        self.alter = None

    def compare(self, base, head, commits=None, *, status="ahead", behind=0, files=None):
        if commits is None:
            commits = [commit(number) for number in range(base + 1, head + 1)]
        self.comparisons[(sha(base), sha(head))] = {
            "status": status,
            "ahead_by": len(commits),
            "behind_by": behind,
            "total_commits": len(commits),
            "base_commit": commit(base),
            "merge_base_commit": commit(base if behind == 0 else 1),
            "commits": commits,
            "files": files or [],
        }

    def __call__(self, route):
        self.calls.append(route)
        if self.fail and self.fail(route):
            raise OSError("Simulated GitHub outage")
        parsed = urlsplit(route)
        path = parsed.path
        if path == "/repos/google/filament/releases/latest":
            result = copy.deepcopy(self.latest)
        elif path == "/repos/google/filament/commits/main":
            result = commit(self.head)
        elif path.startswith("/repos/google/filament/compare/"):
            pair = path.rsplit("/compare/", 1)[1]
            base, head = pair.split("...", 1)
            if (base, head) not in self.comparisons:
                if base != head:
                    raise AssertionError(f"Unexpected compare request: {route}")
                self.compare(int(base, 16), int(head, 16), [], status="identical")
            result = copy.deepcopy(self.comparisons[(base, head)])
            query = parse_qs(parsed.query)
            page = int(query.get("page", [1])[0])
            page_size = int(query.get("per_page", [30])[0])
            start = (page - 1) * page_size
            result["commits"] = result["commits"][start:start + page_size]
            if page != 1:
                result.pop("files", None)
        else:
            raise AssertionError(f"Unexpected GitHub request: {route}")
        if self.alter:
            self.alter(route, result)
        return result


class FilamentUpdateTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="quader-filament-updates-test-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.repo = self.directory / "repo"
        self.state_dir = self.directory / "state"
        self.filament = self.repo / "thirdparty" / "filament"
        self.filament.mkdir(parents=True)
        (self.repo / "cmake").mkdir()
        (self.repo / "cmake" / "QuaderFilamentSdk.cmake").write_text(
            'set(pin_path "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/filament/fixture.json")\n',
            encoding="utf-8",
        )
        self.pin = {
            "artifact": "filament-v1.76.1-quader-vsync5-windows",
            "upstream_version": "1.76.1",
            "upstream_commit": sha(5),
            "source_sha256": "a" * 64,
            "patch_version": 5,
            "patch_file": "fixture.patch",
        }
        self.save_pin()
        self.patch = self.filament / "fixture.patch"
        self.patch.write_text(
            "diff --git a/filament/src/Renderer.cpp b/filament/src/Renderer.cpp\n"
            "--- a/filament/src/Renderer.cpp\n"
            "+++ b/filament/src/Renderer.cpp\n"
            "@@ -1 +1 @@\n-before\n+after\n",
            encoding="utf-8",
        )
        self.now = datetime(2026, 9, 14, 12, 0, tzinfo=timezone.utc)
        self.api = FakeGitHub()
        self.api.compare(5, 10)

    def save_pin(self):
        (self.filament / "fixture.json").write_text(json.dumps(self.pin), encoding="utf-8")

    def check(self, **options):
        arguments = {"now": self.now, "allow_network": True, "get_json": self.api}
        arguments.update(options)
        return checker.check_updates(self.repo, self.state_dir, **arguments)

    def read_state(self, result):
        return json.loads(Path(result["state_path"]).read_text(encoding="utf-8"))

    def snapshot(self, result):
        paths = [Path(result["state_path"]), Path(result["report_path"])]
        return {path: path.read_bytes() for path in paths}

    def assert_unchanged(self, previous):
        for path, content in previous.items():
            self.assertEqual(path.read_bytes(), content, str(path))

    def test_cli_git_failure_exposes_cause_and_preserves_successful_watermark(self):
        initial = self.check()
        before = self.snapshot(initial)
        self.api.calls.clear()
        diagnostics = [
            ("fatal: detected dubious ownership in repository at 'C:/repo'\n"
             "'C:/repo' is owned by user SID1001; current user is sandbox SID1003\n"
             "To add an exception, call git config --global --add safe.directory C:/repo\n",
             "detected dubious ownership", True),
            ("fatal: cannot open '.git/config': Permission denied\n", "Permission denied", True),
            ("error: Access is denied\n", "Access is denied", True),
            ("fatal: not a git repository (or any parent): .git\n", "not a git repository", False),
            ("", "Git returned no diagnostic output", False),
        ]
        for diagnostic, expected, needs_host_retry in diagnostics:
            with self.subTest(diagnostic=diagnostic):
                failure = subprocess.CalledProcessError(128, ["git", "rev-parse"],
                                                       stderr=diagnostic)
                stdout, stderr = io.StringIO(), io.StringIO()
                with patch.object(checker.sys, "argv", ["check_filament_updates.py", "--allow-network"]), \
                        patch.object(checker, "__file__", str(self.repo / "tools/check_filament_updates.py")), \
                        patch.object(checker.subprocess, "run", side_effect=failure) as git, \
                        patch.object(checker, "GitHubApi") as github, \
                        redirect_stdout(stdout), redirect_stderr(stderr):
                    self.assertEqual(checker.main(), 1)
                git.assert_called_once()
                github.assert_not_called()
                self.assertEqual(stdout.getvalue(), "")
                self.assertIn(expected, stderr.getvalue())
                self.assertIn("exit 128", stderr.getvalue())
                self.assertIn("watermark was not advanced", stderr.getvalue())
                self.assertEqual("authorized host-context execution" in stderr.getvalue(), needs_host_retry)
                self.assertNotIn("safe.directory", stderr.getvalue())
                self.assert_unchanged(before)

    def test_cli_success_preserves_opt_in_and_quiet_same_day_cache(self):
        git_directory = self.directory / "git"
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(checker, "__file__", str(self.repo / "tools/check_filament_updates.py")), \
                patch.object(checker.subprocess, "run", return_value=subprocess.CompletedProcess(
                    ["git"], 0, stdout=str(git_directory) + "\n")), \
                patch.object(checker, "GitHubApi", return_value=self.api) as github, \
                redirect_stdout(stdout), redirect_stderr(stderr):
            with patch.object(checker.sys, "argv", ["check_filament_updates.py"]):
                self.assertEqual(checker.main(), 0)
            github.assert_not_called()
            self.assertIn("pass --allow-network", stdout.getvalue())
            with patch.object(checker.sys, "argv", ["check_filament_updates.py", "--allow-network"]):
                self.assertEqual(checker.main(), 0)
            self.assertIn("Filament baseline saved", stdout.getvalue())
            github.assert_called_once()
            self.api.calls.clear()
            stdout.seek(0)
            stdout.truncate()
            with patch.object(checker.sys, "argv", ["check_filament_updates.py", "--allow-network"]):
                self.assertEqual(checker.main(), 0)
            self.assertEqual(self.api.calls, [])
            self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")

    def test_network_requires_explicit_opt_in(self):
        result = self.check(allow_network=False)
        self.assertEqual(result["status"], "network-disabled")
        self.assertEqual(self.api.calls, [])
        self.assertFalse(list(self.state_dir.glob("*.json")))
        self.assertFalse((self.filament / "UPSTREAM_STATUS.md").exists())

    def test_same_day_uses_cache_without_http_and_force_refreshes(self):
        first = self.check()
        self.assertEqual(first["status"], "checked")
        before = self.snapshot(first)
        self.api.calls.clear()
        cached = self.check(now=self.now + timedelta(hours=1))
        self.assertEqual(cached["status"], "cached")
        self.assertEqual(self.api.calls, [])
        self.assert_unchanged(before)

        forced = self.check(force=True)
        self.assertEqual(forced["status"], "checked")
        self.assertIn("/repos/google/filament/commits/main", self.api.calls)

    def test_new_day_keeps_pending_candidates_when_main_is_unchanged(self):
        first = self.check()
        initial = self.read_state(first)
        self.api.calls.clear()
        second = self.check(now=self.now + timedelta(days=1))
        state = self.read_state(second)
        self.assertEqual(second["status"], "checked")
        self.assertTrue(self.api.calls)
        self.assertEqual(state["day"], "2026-09-15")
        self.assertEqual(state["candidates"], initial["candidates"])
        self.assertEqual(state["new_commits"], [])
        self.assertEqual(second["new_commit_count"], 0)
        self.assertEqual(len(state["candidates"]["commits"]), 5)

    def test_daily_changes_page_past_250_using_frozen_main_sha(self):
        self.check()
        self.api.head = 277
        self.api.compare(5, 277)
        self.api.compare(10, 277)
        self.api.calls.clear()

        result = self.check(now=self.now + timedelta(days=1))
        state = self.read_state(result)
        self.assertEqual(result["new_commit_count"], 267)
        self.assertEqual([item["sha"] for item in state["new_commits"]],
                         [sha(number) for number in range(11, 278)])
        self.assertEqual(len(state["candidates"]["commits"]), 272)
        self.assertEqual(state["main"]["sha"], sha(277))
        comparisons = [route for route in self.api.calls if "/compare/" in route]
        self.assertTrue(any(f"{sha(10)}...{sha(277)}?per_page=100&page=3" in route
                            for route in comparisons))
        self.assertTrue(all(f"...{sha(277)}?" in route for route in comparisons))

    def test_late_http_failure_preserves_prior_state_and_report(self):
        initial = self.check()
        before = self.snapshot(initial)
        self.api.head = 277
        self.api.compare(5, 277)
        self.api.compare(10, 277)
        self.api.fail = lambda route: "/compare/" in route and "page=2" in route
        with self.assertRaises(checker.UpdateCheckError):
            self.check(now=self.now + timedelta(days=1))
        self.assert_unchanged(before)

    def test_incomplete_or_duplicate_pagination_preserves_successful_baseline(self):
        initial = self.check()
        before = self.snapshot(initial)
        self.api.head = 277
        self.api.compare(5, 277)
        self.api.compare(10, 277)

        def empty_page(route, response):
            if "/compare/" in route and "page=2" in route:
                response["commits"] = []

        def duplicate_page(route, response):
            if "/compare/" in route and "page=2" in route:
                response["commits"][0] = commit(100)

        for alter in [empty_page, duplicate_page]:
            with self.subTest(response=alter.__name__):
                self.api.alter = alter
                with self.assertRaises(checker.UpdateCheckError):
                    self.check(now=self.now + timedelta(days=1))
                self.assert_unchanged(before)

    def test_release_branch_divergence_from_pin_is_a_valid_candidate(self):
        self.api.compare(5, 10, status="diverged", behind=3)
        result = self.check()
        state = self.read_state(result)
        self.assertEqual(result["status"], "checked")
        self.assertEqual(state["candidates"]["status"], "diverged")
        self.assertEqual(state["candidates"]["behind_by"], 3)
        self.assertEqual(len(state["candidates"]["commits"]), 5)

    def test_rewritten_main_is_rejected_until_explicit_baseline_reset(self):
        initial = self.check()
        before = self.snapshot(initial)
        self.api.head = 20
        self.api.compare(5, 20)
        self.api.compare(10, 20, status="diverged", behind=2)
        tomorrow = self.now + timedelta(days=1)
        with self.assertRaises(checker.UpdateCheckError):
            self.check(now=tomorrow)
        self.assert_unchanged(before)

        reset = self.check(now=tomorrow, reset_main_baseline=True)
        self.assertEqual(reset["status"], "checked")
        self.assertEqual(self.read_state(reset)["main"]["sha"], sha(20))

    def test_changed_pin_and_patch_bypass_same_day_cache(self):
        initial = self.check()
        self.pin["upstream_commit"] = sha(9)
        self.save_pin()
        self.api.compare(9, 10)
        self.api.calls.clear()
        repinned = self.check()
        self.assertEqual(repinned["status"], "checked")
        self.assertNotEqual(repinned["state_path"], initial["state_path"])
        self.assertTrue(self.api.calls)
        self.assertEqual(self.read_state(repinned)["candidates"]["base"], sha(9))

        self.patch.write_text(self.patch.read_text(encoding="utf-8") + "\n", encoding="utf-8")
        self.api.calls.clear()
        repatched = self.check()
        self.assertEqual(repatched["status"], "checked")
        self.assertNotEqual(repatched["state_path"], repinned["state_path"])
        self.assertTrue(self.api.calls)

    def test_release_change_is_detected_without_new_main_commits(self):
        self.check()
        self.api.latest = release("1.77.0")
        result = self.check(now=self.now + timedelta(days=1))
        state = self.read_state(result)
        self.assertTrue(result["release_changed"])
        self.assertEqual(result["new_commit_count"], 0)
        self.assertEqual(state["release"]["tag"], "v1.77.0")

    def test_main_pin_at_current_head_is_identical_and_release_label_is_separate(self):
        self.pin.update({
            "upstream_ref": "main",
            "upstream_commit": sha(self.api.head),
            "artifact": f"filament-main-{sha(self.api.head)[:12]}-quader-vsync6-windows",
            "patch_version": 6,
        })
        self.save_pin()
        result = self.check()
        state = self.read_state(result)
        report = Path(result["report_path"]).read_text(encoding="utf-8")
        self.assertEqual(state["pin"]["upstream_ref"], "main")
        self.assertEqual(state["candidates"]["status"], "identical")
        self.assertEqual(state["candidates"]["commits"], [])
        self.assertEqual(result["candidate_count"], 0)
        self.assertEqual(result["new_commit_count"], 0)
        self.assertEqual(state["release"]["tag"], "v1.76.1")
        self.assertIn("**main snapshot**", report)
        self.assertIn("upstream version label **v1.76.1**", report)
        self.assertIn(self.pin["artifact"], report)
        self.assertIn(self.pin["upstream_commit"], report)
        release_line = next(line for line in report.splitlines() if "Latest stable release:" in line)
        self.assertIn("tracked separately", release_line)
        self.assertNotIn("matches", release_line)

        self.api.calls.clear()
        cached = self.check()
        self.assertEqual(cached["status"], "cached")
        self.assertEqual(self.api.calls, [])
        self.assertEqual(self.read_state(cached)["candidates"], state["candidates"])
        self.assertEqual(Path(cached["report_path"]).read_text(encoding="utf-8"), report)

    def test_report_preserves_apostrophes_while_escaping_upstream_markup(self):
        subject = "Don't double-escape 'quoted' text & <tags> #12"
        self.api.compare(5, 10, [commit(10, subject)])
        result = self.check()
        report = Path(result["report_path"]).read_text(encoding="utf-8")
        line = next(line for line in report.splitlines() if " — " in line)
        self.assertEqual(html.unescape(line.split(" — ", 1)[1]), subject)
        self.assertNotIn("<tags>", line)

    def test_empty_and_leading_blank_commit_messages_remain_checkable(self):
        for message, subject in [("", "(no commit message)"),
                                 ("\n\n  Renderer fix after blank lines  \nDetails",
                                  "Renderer fix after blank lines")]:
            with self.subTest(message=message):
                def replace_message(route, response):
                    if route.endswith("/commits/main"):
                        response["commit"]["message"] = message
                    elif "/compare/" in route:
                        for item in response["commits"]:
                            item["commit"]["message"] = message

                self.api.alter = replace_message
                # A distinct head avoids reusing the prior iteration's cached candidates.
                self.api.head += 1
                self.api.compare(5, self.api.head)
                self.api.compare(self.api.head - 1, self.api.head)
                result = self.check(force=True)
                state = self.read_state(result)
                self.assertEqual(result["status"], "checked")
                self.assertEqual(state["candidates"]["commits"][-1]["subject"], subject)
                self.assertIn(subject, Path(result["report_path"]).read_text(encoding="utf-8"))

    def test_report_escapes_upstream_text_and_identifies_truncated_renamed_overlap(self):
        unsafe = '<img src=x> [click](https://example.invalid) | `command`'
        files = [{"filename": f"unrelated/file_{number}.cpp", "status": "modified"}
                 for number in range(299)]
        files.append({
            "filename": "filament/src/RenamedRenderer.cpp",
            "previous_filename": "filament/src/Renderer.cpp",
            "status": "renamed",
        })
        self.api.compare(5, 10, [commit(10, unsafe)], files=files)
        result = self.check()
        state = self.read_state(result)
        report = Path(result["report_path"]).read_text(encoding="utf-8")
        self.assertTrue(state["candidates"]["files_truncated"])
        self.assertIn("300", report)
        self.assertRegex(report.lower(), r"truncat|incomplete|capped|limit")
        self.assertIn("filament/src/Renderer.cpp", report)
        self.assertIn("filament/src/RenamedRenderer.cpp", report)
        self.assertNotIn("<img src=x>", report)
        self.assertNotIn("[click](https://example.invalid)", report)
        self.assertNotIn("`command`", report)
        self.assertIn("click", report)


if __name__ == "__main__":
    unittest.main()
