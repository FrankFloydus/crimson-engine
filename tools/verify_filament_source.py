################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
"""Verify cached Filament inputs against the pinned archive plus the exact patch."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import tempfile


def digest(stream):
    result = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        result.update(block)
    return result.hexdigest()


def safe_relative(name, prefix):
    if not name.startswith(prefix):
        raise ValueError(f"Unexpected archive root: {name}")
    relative = PurePosixPath(name[len(prefix):])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"Unsafe archive path: {name}")
    return relative.as_posix()


def extract(archive_path, source_root, commit):
    # Python preserves Unicode names on Windows; the bundled tar.exe can decode a
    # UTF-8 source name through the active ANSI code page instead.
    with tarfile.open(archive_path, "r:gz") as archive:
        members = []
        for member in archive.getmembers():
            if member.isdir() and member.name == f"filament-{commit}":
                continue
            member.name = safe_relative(member.name, f"filament-{commit}/")
            target = source_root / member.name
            if member.issym():
                target = target.parent / member.linkname
            elif not member.isdir() and not member.isfile():
                raise ValueError(f"Unsupported archive member: {member.name}")
            if not target.resolve().is_relative_to(source_root):
                raise ValueError(f"Archive member escapes source directory: {member.name}")
            members.append(member)
        archive.extractall(source_root, members=members)


def verify(archive_path, source_root, patch_path, commit):
    # Reapply the patch to independent originals, so a reverse check alone cannot
    # certify arbitrary edits outside the patch hunks in a cached source file.
    patch_files = set(re.findall(r"^\+\+\+ b/(.+)$", patch_path.read_text(), re.MULTILINE))
    if not patch_files:
        raise ValueError("Pinned Filament patch has no modified files")
    expected = {}
    failures = []
    with tempfile.TemporaryDirectory(prefix="quader-filament-verify-") as temporary:
        pristine = Path(temporary).resolve()
        if pristine.parent != Path(tempfile.gettempdir()).resolve():
            raise ValueError("Unexpected temporary verification directory")
        with tarfile.open(archive_path, mode="r|gz") as archive:
            for member in archive:
                if member.isdir() and member.name == f"filament-{commit}":
                    continue
                relative = safe_relative(member.name, f"filament-{commit}/")
                if member.isdir():
                    continue
                current = source_root / relative
                if member.issym():
                    expected[relative] = ("symlink", member.linkname)
                    if not current.is_symlink() or current.readlink().as_posix() != member.linkname:
                        failures.append(relative)
                    continue
                if not member.isfile():
                    raise ValueError(f"Unsupported archive member: {relative}")
                with archive.extractfile(member) as original:
                    if relative in patch_files:
                        target = pristine / relative
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(original.read())
                        expected[relative] = ("patched", "")
                    else:
                        wanted = digest(original)
                        expected[relative] = ("file", wanted)
                        if current.is_symlink() or not current.is_file():
                            failures.append(relative)
                        else:
                            with current.open("rb") as actual:
                                if digest(actual) != wanted:
                                    failures.append(relative)
        if not patch_files.issubset(expected):
            raise ValueError("Patch refers to a file absent from the pinned archive")
        # A configured TEMP may also live below the calling repository. Never
        # let parent repository discovery silently filter every patch path.
        patch_environment = dict(os.environ, GIT_CEILING_DIRECTORIES=str(pristine.parent))
        subprocess.run(["git", "-c", "core.autocrlf=false", "apply", "--check", str(patch_path)],
                       cwd=pristine, env=patch_environment, check=True)
        subprocess.run(["git", "-c", "core.autocrlf=false", "apply", str(patch_path)],
                       cwd=pristine, env=patch_environment, check=True)
        for relative in patch_files:
            current = source_root / relative
            if current.is_symlink() or not current.is_file():
                failures.append(relative)
            elif current.read_bytes() != (pristine / relative).read_bytes():
                failures.append(relative)

    # Source caches may contain Git metadata and Python bytecode produced by upstream
    # build scripts. Additional source/build inputs are not certified by this SDK.
    for directory, subdirectories, files in os.walk(source_root, followlinks=False):
        subdirectories[:] = [name for name in subdirectories if name not in {".git", "__pycache__"}]
        for name in files:
            relative = (Path(directory) / name).relative_to(source_root).as_posix()
            if relative in {".quader-filament-source.json", "ImportExecutables-.cmake"}:
                continue
            if relative not in expected:
                failures.append(relative)
    if failures:
        details = "\n".join(sorted(set(failures))[:20])
        raise ValueError(f"Cached Filament inputs differ from pinned source plus patch:\n{details}")
    print(f"Verified {len(expected)} Filament source inputs against pinned archive and patch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--patch", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--extract", action="store_true")
    arguments = parser.parse_args()
    try:
        if arguments.extract:
            extract(arguments.archive.resolve(), arguments.source.resolve(), arguments.commit)
            return
        verify(arguments.archive.resolve(), arguments.source.resolve(),
               arguments.patch.resolve(), arguments.commit)
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
