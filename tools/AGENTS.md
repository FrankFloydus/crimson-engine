<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Tools Guide

Developer tooling lives here; product behavior stays in its source owner. Root
[AGENTS.md](../AGENTS.md) owns final build requirements and [tests/AGENTS.md](../tests/AGENTS.md)
owns test placement, selection, and behavior contracts.

## Tool Rules

- Prefer deterministic, documented inputs and outputs.
- Avoid destructive filesystem behavior; require explicit user intent for cleanup, migration, or
  overwrite actions.
- If a tool encodes build, dependency, documentation, or test policy, keep it aligned with root
  `AGENTS.md` and the nearest local package guide.

## Build And Verification Commands

Run from the repository root. Use the single native `build/vs2026` tree:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target quader
cmake --build --preset vs2026-release --target quader
ctest --preset vs2026-debug --output-on-failure
```

Allow at least ten minutes per build/test process; asynchronous tool yields must not terminate it.
Both executables are required at handoff after project-file changes, including documentation.
Debug is the optimized Tracy/Profile artifact; only Release ships. Report both results and paths.

- `affected_check.ps1 -PlanOnly -ChangedPath <path>` prints the selected targets and filters.
  Omit `-PlanOnly` to run them; use `-MeasureFull` when claiming compile/test speed improvements.
  Keep changed-path routing aligned with current owners and documentation entry points.
- `check_cpp_style.py` validates C++ paths and `.clang-tidy` identifier policy. Run it only before
  commit/push or when explicitly requested. `.build/cmake-tidy` is a disposable compile database,
  not an alternate native app build.

## Regression Gate

- `regression_gate.ps1` is the read-only, complete command/behavior verification path; do not add
  skip-tests or baseline-update switches. `-NoConfigure` reuses an already configured tree.
  The script builds all test owners, runs full CTest, and builds Debug/Profile `quader` only.
  Run the canonical Release build separately before final handoff.
- `verify_native_build.ps1` checks both canonical executables after their builds: optimization,
  Tracy/Sentry separation, PE symbol policy and the recursively deployed Qt/DLL dependency closure.
  Each runtime also requires the isolated `quader_fatal_diagnostics.exe` companion and its imports.
  It shares read-only assertions with `boot_tracy.ps1` through `native_build_contract.ps1`.
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

## GPU Smoke And Dependency Tools

- `boot_smoke.ps1` launches the app for Vulkan/window checks and collects screenshots and a log
  copy; error/critical/fatal log entries fail the check. Check window chrome/layout, every visible
  viewport, resize, and orbit/pan/zoom when relevant to the change.
- `build_filament_sdk.ps1` builds the pinned, patched Release /MD dependency outside the app tree;
  `verify_filament_source.py` checks cached inputs against the archive and exact patch. Downloads
  require `-AllowDownload`. Flags, artifact identity, ABI requirements, and upgrade checks are in
  [thirdparty/filament/README.md](../thirdparty/filament/README.md).
- With `QUADER_BUILD_QDR_TOOLS=ON`, `qdr_validate <scene.qdr> [...]` validates native QDRB files and
  `qdr_dump --json <scene.qdr>` emits a diagnostic projection. They are inspection tools, not
  replacement persistence APIs.
