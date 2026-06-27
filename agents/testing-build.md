<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Testing And Build

## TDD

- Add or update GoogleTest coverage before implementing non-trivial core behavior.
- Keep tests fast and headless.
- Scene data, assets, descriptors, logger sinks, and viewport navigation should be testable without
  opening a window.
- GPU/window behavior is validated with manual smoke tests until a render-test harness exists.
- When fixing duplicated logic or source-of-truth drift, add a behavioral test for the state that
  used to desynchronize and, where useful, a focused guardrail test that points future agents at the
  canonical owner.
- Prefer synthetic tests that verify undo/redo, revision stamps, selection sync, cache invalidation,
  mesh/material component settings, viewport derived state, and command aliases after the shared
  owner path runs.
- Programmatic API and `BehaviorScenario` tests are always built in the app test target. Do not add
  `QUADER_TEST_HAS_PROGRAMMATIC_API` guards, skipped stubs, or fallback scenario shells.

## Expected Commands

When launching these commands through Codex tools, set `timeout_ms` to at least 10 minutes
(`600000`). Clean builds and full verification scripts may need longer. Do not run
`cmake --build`, `ctest`, or wrappers around them with 120-second timeouts, because this project
often needs more than 5 minutes before the build finishes.

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
ctest --preset vs2026-debug
```

For local prototyping, the optional Ninja Multi-Config path keeps artifacts in `build/ninja-msvc`.
Run it from a Visual Studio Developer PowerShell/CMD so `cl.exe` is on `PATH`; do not hardcode
local compiler paths into presets.

```powershell
cmake --preset ninja-msvc
cmake --build --preset ninja-msvc-debug --target quader
ctest --preset ninja-msvc-debug
```

The native test suite uses GoogleTest only. It is split into `quader_guardrail_tests` for pure
source-policy scans, `quader_tests` for app/runtime behavior, and the GoogleTest
`quader_modeling_domain_tests` aggregate for modeling-domain behavior. All three run through
`ctest --preset vs2026-debug`. Do not add SDK-specific configure, build, or test presets back to the
native app.

`QUADER_ENABLE_PCH` is enabled by default and adds private target-local precompiled headers with
stable C++ standard-library headers only. Keep project headers, SDK headers, and GoogleTest out of
the common PCH list unless a separate measurement proves the benefit and the affected owner agrees.

Modeling-domain prototyping loops may build developer-only `EXCLUDE_FROM_ALL` targets:
`quader_modeling_domain_polygon_loop_tests`, `quader_modeling_domain_runtime_loop_tests`, and
`quader_modeling_domain_metadata_loop_tests`. These exist so `tools/affected_check.ps1` can compile
and run only the relevant modeling-domain bucket. Do not register them with CTest; full verification
stays on the aggregate `quader_modeling_domain_tests` entry.
For direct polygon or runtime operation source edits, the affected workflow should prefer
operation-family GoogleTest filters before falling back to broad polygon/runtime filters.

## Affected Build/Test Workflow

For prototyping or agent loops, prefer the affected workflow before full verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -PlanOnly -ChangedPath src/render/overlay/overlay_renderable_builder.cpp
powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -ChangedPath src/render/overlay/overlay_renderable_builder.cpp
```

The tool prints the old full workflow and the affected workflow side by side. Use `-MeasureFull`
when a task needs a concrete before -> after timing comparison. Use `-IncludeApp` when manual
in-app verification or runtime resource copying is part of the change. In a very dirty worktree,
pass comma-separated `-ChangedPath` values for the intended edit instead of letting the tool infer
the whole diff. Docs, tooling, and source-policy changes should route to `quader_guardrail_tests`
instead of forcing the app/runtime test executable to build.
To use the optional Ninja tree with the affected checker, pass the matching presets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -BuildPreset ninja-msvc-debug -TestPreset ninja-msvc-debug -ChangedPath src/render/overlay/overlay_renderable_builder.cpp
```

If PowerShell inherits conflicting `PATH`/`Path` entries and CMake cannot find tools, use:

```powershell
$p = $env:Path
# Use timeout_ms >= 600000 when running this workaround through Codex tools.
cmd /c "set PATH=& set Path=$p& cmake --build --preset vs2026-debug --target quader"
```

## Manual Smoke Checklist

- Vulkan backend initializes.
- Main SDL window opens without a separate console window.
- RmlUi MainLayout shell appears with toolbar/chrome, central viewport host, configured side/bottom
  dock surfaces, and status bar.
- The viewport texture renders app-created polygon meshes with the default material.
- Window resize keeps the viewport texture visible without stale or blank UI content.
- MMB orbits, Shift+MMB pans, mouse wheel zooms.

Use `quader_tests`/`ctest` for headless behavior, rebuild the `quader` target for
resources/materials/runtime UI changes, and use `tools/boot_smoke.ps1` for GPU/window/RmlUi smoke
checks with screenshot and log-copy output when a visual runtime check is required.
