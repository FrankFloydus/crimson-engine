################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
"""Check public Filament metadata once per local day; never modify the SDK or pin."""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import errno
import hashlib
import html
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


API = "/repos/google/filament"
WEB = "https://github.com/google/filament"
SCHEMA_VERSION = 1
REPORT_LIMIT = 60


class UpdateCheckError(RuntimeError):
    """A check did not complete; its last successful watermark must be retained."""


def _sha(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{40}", value):
        raise UpdateCheckError("Expected an exact 40-character Git commit SHA.")
    return value


def _commit(value):
    subject = next((line.strip() for line in value["commit"]["message"].splitlines()
                    if line.strip()), "(no commit message)")
    return {"sha": _sha(value["sha"]),
            "subject": subject,
            "date": value["commit"]["committer"]["date"]}


def _pin(repo_root):
    cmake = (repo_root / "cmake/QuaderFilamentSdk.cmake").read_text(encoding="utf-8")
    matches = re.findall(
        r'set\(\s*pin_path\s+"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^"\n]+)"\s*\)', cmake)
    if len(matches) != 1:
        raise UpdateCheckError("Cannot identify the active Filament pin in CMake.")
    directory = (repo_root / "thirdparty/filament").resolve()
    manifest = (repo_root / matches[0]).resolve()
    if manifest.parent != directory:
        raise UpdateCheckError("The Filament manifest must be in thirdparty/filament.")
    pin = json.loads(manifest.read_text(encoding="utf-8"))
    _sha(pin["upstream_commit"])
    patch = (directory / pin["patch_file"]).resolve()
    if patch.parent != directory:
        raise UpdateCheckError("The Filament patch must be in thirdparty/filament.")
    patch_bytes = patch.read_bytes()
    pin["manifest"] = manifest.name
    pin["patch_sha256"] = hashlib.sha256(patch_bytes).hexdigest()
    fingerprint = hashlib.sha256(json.dumps(pin, sort_keys=True).encode()).hexdigest()
    patched_paths = set()
    for old, new in re.findall(r"^diff --git a/(.+) b/(.+)$",
                               patch_bytes.decode("utf-8").replace("\r\n", "\n"), re.M):
        patched_paths.update((old, new))
    if not patched_paths:
        raise UpdateCheckError("No patched paths found in the active Filament patch.")
    return pin, fingerprint, patched_paths


class GitHubApi:
    def __init__(self):
        self.deadline = time.monotonic() + 45

    def __call__(self, route):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise UpdateCheckError("GitHub check exceeded its request time budget; retry later.")
        if not route.startswith(API + "/"):
            raise UpdateCheckError("Unexpected GitHub API route.")
        headers = {"Accept": "application/vnd.github+json",
                   "User-Agent": "Quader-Filament-Update-Checker",
                   "X-GitHub-Api-Version": "2026-03-10"}
        token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
        if token:
            headers["Authorization"] = "Bearer " + token
        try:
            request = Request("https://api.github.com" + route, headers=headers)
            with urlopen(request, timeout=min(10, remaining)) as response:
                body = response.read(16 * 1024 * 1024 + 1)
                if len(body) > 16 * 1024 * 1024:
                    raise UpdateCheckError("GitHub response exceeded the metadata size limit.")
                return json.loads(body)
        except HTTPError as error:
            raise UpdateCheckError(
                f"GitHub returned HTTP {error.code}; retry later (403/429 may be rate limits)."
            ) from error
        except (URLError, TimeoutError) as error:
            raise UpdateCheckError("GitHub is unavailable or timed out; retry later.") from error


def _compare(get_json, base, head):
    base, head = _sha(base), _sha(head)
    result = None
    seen = set()
    for page in range(1, 101):
        data = get_json(f"{API}/compare/{base}...{head}?per_page=100&page={page}")
        signature = {key: data[key] for key in
                     ("status", "ahead_by", "behind_by", "total_commits")}
        if signature["status"] not in ("ahead", "behind", "diverged", "identical"):
            raise UpdateCheckError("Unknown GitHub comparison status.")
        for key in ("ahead_by", "behind_by", "total_commits"):
            if type(signature[key]) is not int or not 0 <= signature[key] <= 1000000:
                raise UpdateCheckError("Invalid GitHub comparison count.")
        if signature["total_commits"] != signature["ahead_by"]:
            raise UpdateCheckError("Inconsistent GitHub comparison counts.")
        if result is None:
            files = [{key: item[key] for key in ("filename", "previous_filename", "status")
                      if key in item} for item in data["files"]]
            result = dict(signature, base=base, head=head, commits=[], files=files,
                          files_truncated=len(files) >= 300)
        elif any(result[key] != value for key, value in signature.items()):
            raise UpdateCheckError("GitHub comparison changed during pagination.")
        commits = [_commit(item) for item in data["commits"]]
        for commit in commits:
            if commit["sha"] in seen:
                raise UpdateCheckError("Duplicate commit in GitHub comparison pagination.")
            seen.add(commit["sha"])
        result["commits"].extend(commits)
        if len(seen) == result["total_commits"]:
            if result["total_commits"] and head not in seen:
                raise UpdateCheckError("GitHub comparison did not include the captured main SHA.")
            return result
        if not commits or len(seen) > result["total_commits"]:
            raise UpdateCheckError("Incomplete GitHub comparison; keeping the previous watermark.")
    raise UpdateCheckError("Comparison exceeds 10,000 commits; keeping the previous watermark.")


def _text(value):
    # Upstream titles and filenames are display data, never executable Markdown/HTML.
    # Quotes are plain inline text; numeric quote entities would be escaped again below.
    text = html.escape(" ".join(str(value).split())[:240], quote=False)
    return re.sub(r"[\\`*_{\[\]}|!#]", lambda match: f"&#{ord(match[0])};", text)


def _commit_lines(commits):
    lines = []
    for commit in reversed(commits[-REPORT_LIMIT:]):
        lines.append(f"- [{commit['sha'][:12]}]({WEB}/commit/{commit['sha']}) "
                     f"{_text(commit['date'][:10])} — {_text(commit['subject'])}")
    if len(commits) > REPORT_LIMIT:
        lines.append(f"\nShowing the latest {REPORT_LIMIT} of {len(commits)} commits; "
                     "the comparison link and local cache retain the full range.")
    return lines


def _report(state, patched_paths):
    pin, main, release = state["pin"], state["main"], state["release"]
    candidates = state["candidates"]
    main_pin = pin.get("upstream_ref") == "main"
    comparison_url = f"{WEB}/compare/{pin['upstream_commit']}...{main['sha']}"
    release_url = f"{WEB}/releases/tag/{quote(release['tag'], safe='')}"
    if main_pin:
        pin_label = f"**main snapshot**, upstream version label **v{_text(pin['upstream_version'])}**"
        release_status = "tracked separately from our pinned main snapshot"
    else:
        pin_label = f"**v{_text(pin['upstream_version'])}**"
        release_status = ("matches our pinned release label" if release["tag"].lstrip("v") ==
                          pin["upstream_version"].lstrip("v")
                          else "differs from our pinned release label; review required")
    lines = ["# Filament upstream status", "",
             "Generated by `tools/check_filament_updates.py`. Upstream titles are untrusted data.",
             "This report identifies review candidates. It does not establish patch compatibility.", "",
             f"- Last successful check (local day): **{state['day']}**",
             f"- Checked at (UTC): {_text(state['checked_at'])}",
             f"- Our pin: {pin_label}, patch revision {pin['patch_version']} "
             f"([authoritative manifest]({quote(pin['manifest'])})).",
             f"- Pinned artifact: {_text(pin['artifact'])}",
             f"- Pinned upstream commit: [{pin['upstream_commit']}]({WEB}/commit/{pin['upstream_commit']})",
             f"- Patch SHA-256: `{pin['patch_sha256']}`",
             f"- Latest stable release: [{_text(release['tag'])}]({release_url}), "
             f"published {_text(release['published_at'])}; {release_status}.",
             f"- Main snapshot: [{main['sha']}]({WEB}/commit/{main['sha']}), "
             f"commit date {_text(main['date'])}.", "", "## Changes since the previous check", ""]
    previous = state["previous_main"]
    if previous is None:
        lines.append("Initial baseline for this pin and patch (or an explicitly reset main baseline). "
                     "The candidates below are existing history, not commits newly published today.")
    else:
        lines.append(f"{len(state['new_commits'])} new main commits since "
                     f"[{previous[:12]}]({WEB}/compare/{previous}...{main['sha']}).")
        lines.extend(_commit_lines(state["new_commits"]))
    if state["release_changed"]:
        lines.append("\nThe latest stable release changed since the previous successful check.")
    lines += ["", "## Candidate history relative to our pin", "",
              f"[Full comparison]({comparison_url}): {candidates['ahead_by']} commits on the main "
              f"side; {candidates['behind_by']} commits on the pin side. "
              f"Graph status: **{candidates['status']}**.", "",
              "This list is retained across daily checks until the pin changes. It is not a "
              "review ledger: inspecting a candidate does not remove it.", ""]
    if candidates["status"] == "diverged":
        if main_pin:
            lines += ["**Pinned and current main histories diverge.** Review upstream history "
                      "changes before selecting a new source snapshot.", ""]
        else:
            lines += ["**Release and main histories diverge.** Release branches may already contain "
                      "equivalent cherry-picked changes. These commits are ancestry candidates, "
                      "not a list of confirmed missing functionality.", ""]
    lines.extend(_commit_lines(candidates["commits"]) or ["No main-side candidate commits."])
    lines += ["", "## Potential overlap with our patches", ""]
    if candidates["files_truncated"]:
        lines += ["**Incomplete file coverage:** GitHub returned its 300-file comparison limit. "
                  "Additional overlaps may exist; this cannot establish that any patch is unaffected.", ""]
    else:
        lines += ["These are net comparison paths, not a per-commit conflict analysis. "
                  "Reverted changes and equivalent backports require separate review.", ""]
    overlaps = []
    for item in candidates["files"]:
        if {item["filename"], item.get("previous_filename", "")} & patched_paths:
            old = item.get("previous_filename")
            overlaps.append(f"- {_text(item['filename'])}" +
                            (f" (renamed from {_text(old)})" if old else ""))
    lines.extend(overlaps or ["No overlaps found in the returned net file list; this is not a compatibility verdict."])
    lines += ["", "See [the update workflow](README.md#daily-upstream-check) for checking and reviewing "
              "updates. Applying a candidate requires a separate authorized upgrade and verification.", ""]
    return "\n".join(lines)


def _atomic_write(path, content):
    data = content.encode("utf-8")
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


@contextmanager
def _lock(path):
    # An OS lock releases on exit/crash; persistent lock files never require stale-PID cleanup.
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as stream:
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()
        stream.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            if error.errno not in (errno.EACCES, errno.EAGAIN):
                raise
            yield False
            return
        try:
            yield True
        finally:
            stream.seek(0)
            if os.name == "nt":
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def check_updates(repo_root, state_dir, *, now=None, allow_network=False, force=False,
                  reset_main_baseline=False, get_json=None):
    """Return checked/cached/busy/network-disabled; failures preserve the successful state."""
    repo_root, state_dir = Path(repo_root), Path(state_dir)
    now = now or datetime.now().astimezone()
    try:
        pin, fingerprint, patched_paths = _pin(repo_root)
        state_path = state_dir / (fingerprint + ".json")
        report_path = repo_root / "thirdparty/filament/UPSTREAM_STATUS.md"
        result = {"state_path": str(state_path), "report_path": str(report_path)}
        with _lock(state_path.with_suffix(".lock")) as acquired:
            if not acquired:
                return dict(result, status="busy")
            previous = None
            if state_path.exists():
                previous = json.loads(state_path.read_text(encoding="utf-8"))
                if (previous["schema_version"] != SCHEMA_VERSION or
                        previous["fingerprint"] != fingerprint):
                    raise UpdateCheckError("Unsupported Filament cache; preserve it for manual review.")
            day = now.date().isoformat()
            if previous and previous["day"] == day and not (force or reset_main_baseline):
                _atomic_write(report_path, _report(previous, patched_paths))
                return dict(result, status="cached")
            if not allow_network:
                return dict(result, status="network-disabled")
            get_json = get_json or GitHubApi()
            head = _commit(get_json(API + "/commits/main"))
            latest = get_json(API + "/releases/latest")
            if latest["draft"] or latest["prerelease"] or not latest["tag_name"]:
                raise UpdateCheckError("GitHub did not return a stable published release.")
            release = {"tag": latest["tag_name"], "published_at": latest["published_at"]}
            previous_main = previous["main"]["sha"] if previous and not reset_main_baseline else None
            new_commits = []
            if previous_main and previous_main != head["sha"]:
                delta = _compare(get_json, previous_main, head["sha"])
                if delta["status"] != "ahead" or delta["behind_by"]:
                    raise UpdateCheckError("Main history was rewritten. Review it before explicitly "
                                           "running --reset-main-baseline --allow-network.")
                new_commits = delta["commits"]
            candidates = (previous["candidates"] if previous and previous["main"]["sha"] == head["sha"]
                          else _compare(get_json, pin["upstream_commit"], head["sha"]))
            state = {"schema_version": SCHEMA_VERSION, "fingerprint": fingerprint, "day": day,
                     "checked_at": now.astimezone(timezone.utc).isoformat(), "pin": pin,
                     "main": {"sha": head["sha"], "date": head["date"]}, "release": release,
                     "candidates": candidates, "previous_main": previous_main,
                     "new_commits": new_commits,
                     "release_changed": bool(previous and previous["release"]["tag"] != release["tag"])}
            # Publish only after every request/page succeeds. Report first: a failed state write
            # leaves the old watermark so a later task retries instead of losing the update.
            _atomic_write(report_path, _report(state, patched_paths))
            _atomic_write(state_path, json.dumps(state, indent=2, ensure_ascii=False) + "\n")
            return dict(result, status="checked", new_commit_count=len(new_commits),
                        release_changed=state["release_changed"], baseline=previous_main is None,
                        candidate_count=candidates["total_commits"], release=release["tag"])
    except UpdateCheckError:
        raise
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        raise UpdateCheckError(f"Filament check could not complete ({type(error).__name__}); "
                               "the successful watermark was not advanced.") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-network", action="store_true", help="Allow public GitHub metadata GET requests")
    parser.add_argument("--force", action="store_true", help="Check again even if successful today")
    parser.add_argument("--reset-main-baseline", action="store_true",
                        help="Explicitly accept rewritten main history after manual review")
    args = parser.parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    try:
        common = subprocess.run(["git", "-C", str(repo_root), "rev-parse", "--path-format=absolute",
                                 "--git-common-dir"], check=True, capture_output=True, text=True)
        result = check_updates(repo_root, Path(common.stdout.strip()) / "quader/filament-updates",
                               allow_network=args.allow_network, force=args.force,
                               reset_main_baseline=args.reset_main_baseline)
    except (UpdateCheckError, OSError, subprocess.CalledProcessError) as error:
        print(f"Filament update check: {error}", file=sys.stderr)
        return 1
    status = result["status"]
    if status == "checked":
        if result["baseline"]:
            print(f"Filament baseline saved: release {result['release']}; "
                  f"{result['candidate_count']} main-side ancestry candidates. "
                  "Read thirdparty/filament/UPSTREAM_STATUS.md.")
        elif result["new_commit_count"] or result["release_changed"]:
            print(f"Filament updates: {result['new_commit_count']} new main commits; "
                  f"latest release {result['release']}. Read thirdparty/filament/UPSTREAM_STATUS.md.")
        else:
            print("Filament checked today: no new release or main commits.")
    elif status == "network-disabled":
        print("Filament check is due; pass --allow-network to read GitHub metadata.")
    elif status == "busy":
        print("Another task is checking Filament; continue this task and retry later if needed.")
    # A same-day cache hit is intentionally quiet.
    return 0


if __name__ == "__main__":
    sys.exit(main())
