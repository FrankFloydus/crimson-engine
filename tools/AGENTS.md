<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Tools Guide

`tools` is for developer tooling and repository maintenance scripts.

## Tool Rules

- Keep tools separate from product runtime code.
- Prefer deterministic, documented inputs and outputs.
- Avoid destructive filesystem behavior; require explicit user intent for cleanup, migration, or
  overwrite actions.
- If a tool encodes build, dependency, documentation, or test policy, keep it aligned with root
  `AGENTS.md` and the nearest local package guide.

## C++ Style Tool

`tools/check_cpp_style.py` validates project-owned C++ path naming and the root `.clang-tidy`
identifier policy. Its compile database under `.build/cmake-tidy` is disposable.

## Regression Gate

- `regression_gate.ps1` is the read-only, complete command/behavior verification path; do not add
  skip-tests or baseline-update switches.
- `update_command_behavior_baseline.ps1` is the separate intentional-change path. It must require a
  non-empty reason and leave the resulting baseline diff for review.

## Tracy Launcher

- `boot_tracy.ps1` targets only `build/vs2026/Debug/quader.exe`: the optimized Profile artifact
  with embedded symbols and on-demand Tracy. It configures `vs2026`, incrementally builds
  `vs2026-debug --target quader`, validates the profile contract, starts Tracy first, then Quader.
- The launcher must reject Debug CRT/STL, debug Qt/vcpkg, Filament `mdd`/`matdbg`, and any
  noncanonical executable, configuration, cache, or preset. Only the profiler path is overridable.
- `-ValidateOnly` performs validation without building or launching. `-NoPauseOnError` keeps
  automation noninteractive.
- Validate the recursively deployed DLL and Qt-plugin dependency closure, not just direct DLLs.
