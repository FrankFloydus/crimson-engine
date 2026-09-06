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
[AGENTS.md](../AGENTS.md) owns handoff requirements and [tests/AGENTS.md](../tests/AGENTS.md)
owns test placement, selection, and behavior contracts.

## Tool Rules

- Prefer deterministic, documented inputs and outputs.
- Avoid destructive filesystem behavior; require explicit user intent for cleanup, migration, or
  overwrite actions.
- If a tool encodes build, dependency, documentation, or test policy, keep it aligned with root
  `AGENTS.md` and the nearest local package guide.

## Build And Verification Commands

Run from the repository root in the single `build/vs2026` tree. For an app change:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-release --target quader
```

Configure only when needed. Release is the default and only required native executable; build
Debug/Profile only on explicit request. Test targets can use `vs2026-debug` without building the
Debug app. Documentation/tooling changes need their own checks, not app builds. Report only the
artifact actually built. Allow ten minutes per process; asynchronous yields must not terminate it.

- `affected_check.ps1 -PlanOnly -ChangedPath <path>` prints targets and filters for current-task
  paths. Omit `-PlanOnly` to run them. Include affected tests in one pass; do not run the same suites
  separately first or follow them with an equivalent full gate. Without `-ChangedPath`, the tool
  deliberately includes the whole dirty worktree for integration checks.
- Read broad fallback reasons. Source-list-only CMake edits and command-specific test fixtures can
  be verified with explicitly selected affected targets/suites after reviewing the diff; compiler,
  dependency, shared infrastructure or unknown changes require full coverage. Do not silently drop
  unknown/deleted paths or accept a filter selecting zero tests.
- Reuse an already current guardrail binary for prose/source-only checks. Build it if its test
  implementation or dependencies changed. Batch required test targets in one build invocation;
  app handoff uses the Release build separately. `-IncludeApp` requests that handoff, not GUI control.
  `-AppBuildPreset vs2026-debug` is reserved for an explicitly requested Debug/Profile app.
- `-MeasureFull` deliberately runs an additional full workflow for benchmarking. Use it only when
  that measurement is needed; existing logs can establish redundant work without rerunning it.
- `check_cpp_style.py` validates C++ paths and `.clang-tidy` identifier policy. Run it only before
  commit/push or when explicitly requested. `.build/cmake-tidy` is a disposable compile database,
  not an alternate native app build.

## Regression Gate

- `regression_gate.ps1` is the complete, read-only test path for broad risks in root policy, not an
  additional pass after equivalent affected coverage. Keep its full suite and baseline protections.
  `-NoConfigure` reuses the configured tree. The script builds test targets and runs full CTest;
  it does not build an app. Build Release once for app changes.
- `verify_native_build.ps1` checks the Release artifact by default: optimization,
  Tracy/Sentry separation, PE symbol policy and the recursively deployed Qt/DLL dependency closure.
  Pass `-Configuration Debug` only for an explicitly requested Debug/Profile build, or select both
  after explicitly requested dual builds. It must not require an unused configuration's executable.
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
