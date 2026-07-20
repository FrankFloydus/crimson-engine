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

`tools/check_cpp_style.py` is the clang-tidy style gate. It validates project-owned C++ path naming,
generates its disposable compile database in `.build/cmake-tidy`, and enforces the root
`.clang-tidy` identifier-naming policy.

## Regression Gate

- `regression_gate.ps1` is the read-only, complete command/behavior verification path; do not add
  skip-tests or baseline-update switches.
- `update_command_behavior_baseline.ps1` is the separate intentional-change path. It must require a
  non-empty reason and leave the resulting baseline diff for review.

## Tracy Launcher

- `boot_tracy.ps1` is fixed to the canonical `build/vs2026/Debug/quader.exe`. On a normal launch it
  configures `vs2026`, incrementally builds only `vs2026-debug --target quader`, validates that
  Debug uses Release-speed runtime/dependencies with Tracy/on-demand enabled and Release has no
  Tracy definitions, starts the Tracy profiler first, then starts Quader and waits until both
  application windows are ready.
- The `Debug` path is the Profile artifact compatibility path: Release-speed code and dependencies,
  embedded symbols, and Tracy. It is not a traditional checked Debug build and is never shipped.
- Launcher failures remain visible until Enter is pressed. Automated callers can pass
  `-NoPauseOnError` to preserve non-interactive failure behavior.
- `-ValidateOnly` checks an already configured/built canonical tree without rebuilding or launching.
  The profiler path may be overridden, but the executable, configuration, cache, and build preset
  may not: profiling captures always use the optimized Tracy-enabled Debug executable. Validation
  must reject the Debug CRT, checked STL, Qt `*d`, vcpkg `debug/lib`, Filament `mdd`, and `matdbg`.
- Launcher validation must verify the required Release DLL identities and recursively inspect the
  deployed executable, DLLs, and Qt plugins for missing dependencies before starting Quader. A
  direct-DLL checklist is not a replacement for dependency-closure validation.
