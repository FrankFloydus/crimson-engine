# ////////////////////////////////////////////////////////////////////////////////
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
# ////////////////////////////////////////////////////////////////////////////////

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
CLANG_TIDY_CONFIG = REPO_ROOT / ".clang-tidy"
BUILD_ROOT = REPO_ROOT / ".build"
TIDY_BUILD_DIR = BUILD_ROOT / "cmake-tidy"
SNAKE_CASE = re.compile(r"^[a-z0-9]+(_[a-z0-9]+)*$")
CPP_EXTENSIONS = { ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx" }


def print_step(message: str) -> None:
	print(f"[cpp-style] {message}")


def command_text(command: list[str]) -> str:
	return " ".join(f'"{part}"' if " " in part else part for part in command)


def run(command: list[str], *, cwd: Path = REPO_ROOT) -> None:
	print_step(command_text(command))
	subprocess.run(command, cwd=cwd, check=True)


def find_executable(name: str) -> str | None:
	candidates = [
		Path.home() / "scoop" / "apps" / "llvm" / "current" / "bin" / name,
		Path("C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin") / name,
		Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin") / name,
	]
	for candidate in candidates:
		if candidate.exists():
			return str(candidate)

	found = shutil.which(name)
	if found:
		return found
	return None


def find_run_clang_tidy() -> str:
	for name in ("run-clang-tidy", "run-clang-tidy.py"):
		found = find_executable(name)
		if found:
			return found
	raise RuntimeError("Unable to find run-clang-tidy. Install LLVM or add it to PATH.")


def find_clang_tidy() -> str:
	found = find_executable("clang-tidy.exe") or find_executable("clang-tidy")
	if found:
		return found
	raise RuntimeError("Unable to find clang-tidy. Install LLVM or add it to PATH.")


def cmake_path(path: Path) -> str:
	return str(path).replace("\\", "/")


def source_filter_for(*directory_names: str) -> str:
	roots = []
	for directory_name in directory_names:
		root = re.escape(str((REPO_ROOT / directory_name).resolve())).replace(r"\\", r"[\\/]")
		roots.append(root + r"[\\/].*\.cpp$")
	return "|".join(roots)


def header_filter_for(*directory_names: str) -> str:
	roots = []
	for directory_name in directory_names:
		root = re.escape(str((REPO_ROOT / directory_name).resolve())).replace(r"\\", r"[\\/]")
		roots.append(root + r"[\\/].*")
	return "|".join(roots)


def path_has_snake_case_parts(path: Path) -> bool:
	for part in path.with_suffix("").parts:
		if part in { "src", "sdk", "include", "tests", "examples" }:
			continue
		if not SNAKE_CASE.fullmatch(part):
			return False
	return True


def check_project_paths() -> None:
	errors: list[str] = []
	for root_name in ("src", "tests", "examples"):
		root = REPO_ROOT / root_name
		for path in root.rglob("*"):
			if path.is_dir():
				relative = path.relative_to(REPO_ROOT)
				if not path_has_snake_case_parts(relative):
					errors.append(f"Directory must use snake_case: {relative}")
				continue

			if path.suffix.lower() not in CPP_EXTENSIONS:
				continue

			relative = path.relative_to(REPO_ROOT)
			if not path_has_snake_case_parts(relative):
				errors.append(f"C++ file path must use snake_case: {relative}")

			if path.name.startswith("q_"):
				errors.append(f"`Q` is a prefix, not a word; use qname style: {relative}")

	if errors:
		for error in errors:
			print(f"ERROR: {error}", file=sys.stderr)
		raise RuntimeError("C++ path naming check failed.")


def generate_cmake_compile_database() -> None:
	user_profile = os.environ.get("USERPROFILE", "")
	vcpkg_toolchain = Path(user_profile) / "vcpkg" / "scripts" / "buildsystems" / "vcpkg.cmake"
	command = [
		"cmake",
		"-S",
		".",
		"-B",
		str(TIDY_BUILD_DIR),
		"-G",
		"Ninja",
		"-DCMAKE_BUILD_TYPE=Debug",
		"-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
		"-DQUADER_BUILD_TESTS=ON",
		"-DQUADER_ENABLE_PCH=OFF",
	]
	if vcpkg_toolchain.exists():
		command.append(f"-DCMAKE_TOOLCHAIN_FILE={cmake_path(vcpkg_toolchain)}")
		command.append("-DVCPKG_TARGET_TRIPLET=x64-windows")
	run(command)


def generate_cmake_build_prerequisites() -> None:
	run([
		"cmake",
		"--build",
		str(TIDY_BUILD_DIR),
		"--target",
		"quader_qdr_flatbuffers_generated",
	])


def run_clang_tidy(
	run_clang_tidy_path: str,
	clang_tidy_path: str,
	compile_database_dir: Path,
	source_filter: str,
	jobs: int,
) -> None:
	command = [
		sys.executable,
		run_clang_tidy_path,
		"-p",
		str(compile_database_dir),
		"-clang-tidy-binary",
		clang_tidy_path,
		"-config-file",
		str(CLANG_TIDY_CONFIG),
		"-source-filter",
		source_filter,
		"-header-filter",
		header_filter_for("src", "tests", "examples"),
		"-exclude-header-filter",
		r".*[\\/](godot-cpp|thirdparty|vcpkg_installed|_deps)[\\/].*",
		"-warnings-as-errors",
		"readability-identifier-naming",
		# LLVM can otherwise select MSVC STL vector helpers for trivially comparable
		# 16-byte class values, whose implementation deliberately rejects that size.
		"-extra-arg=-D_USE_STD_VECTOR_ALGORITHMS=0",
		"-j",
		str(jobs),
		"-quiet",
	]
	run(command)


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Run Quader C++ style checks.")
	parser.add_argument("--jobs", "-j", type=int, default=max(1, (os.cpu_count() or 2) - 1), help="Parallel clang-tidy jobs.")
	parser.add_argument("--skip-compile-db", action="store_true", help="Reuse existing compile_commands.json files.")
	parser.add_argument("--paths-only", action="store_true", help="Only check file and folder naming.")
	return parser.parse_args()


def main() -> int:
	args = parse_args()

	try:
		check_project_paths()
		if args.paths_only:
			return 0

		run_clang_tidy_path = find_run_clang_tidy()
		clang_tidy_path = find_clang_tidy()

		if not args.skip_compile_db:
			generate_cmake_compile_database()
		generate_cmake_build_prerequisites()

		run_clang_tidy(
			run_clang_tidy_path,
			clang_tidy_path,
			TIDY_BUILD_DIR,
			source_filter_for("src", "tests", "examples"),
			args.jobs,
		)
	except (RuntimeError, subprocess.CalledProcessError) as error:
		print(f"ERROR: {error}", file=sys.stderr)
		return 1

	return 0


if __name__ == "__main__":
	raise SystemExit(main())
