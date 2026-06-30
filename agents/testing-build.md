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

## App Versioning

Root `VERSION` is the app version source of truth, and `vcpkg.json` `version-string` must mirror it.
Use unpadded `x.y.z` numeric format; the app displays it as `vX.Y.Z`.

For now, keep major/minor fixed at `0.0` and bump only the patch/build component. Patch must stay
within `1..9999`; if it is already `9999`, stop and ask before rolling minor or major.

After the initial `0.0.1` versioning implementation, agents must bump both `VERSION` and
`vcpkg.json` once before the final handoff `quader` build when a task changes project files. Do not
bump for planning, review, inspection-only, failed/no-change, or explanation-only turns.

## Expected Commands

When launching these commands through Codex tools, set `timeout_ms` to at least 10 minutes
(`600000`). Clean builds and full verification scripts may need longer. Do not run
`cmake --build`, `ctest`, or wrappers around them with 120-second timeouts, because this project
often needs more than 5 minutes before the build finishes.

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
cmake --build --preset vs2026-debug --target quader
ctest --preset vs2026-debug
```

Whenever a task changes any project file, final verification must include building the native app
executable with `cmake --build --preset vs2026-debug --target quader`. Do this even when the
behavior was already covered by `quader_tests`; otherwise the user may launch an older
`build/vs2026/Debug/quader.exe` that does not contain the fix.

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

## Test Placement

Choose the test owner from the behavior being protected, not from the file that happened to change.

- Put source-policy, architecture, dependency, naming, docs, CMake, and tooling invariants in
  `tests/*guardrail_tests.cpp`, usually `tests/build_guardrail_tests.cpp`,
  `tests/programmatic_api_guardrail_tests.cpp`, or `tests/oop_architecture_guardrail_tests.cpp`.
  These build in `quader_guardrail_tests`.
- Put user/editor behavior that goes through `QuaderEditorApi`, `QuaderApi`, commands,
  `BehaviorScenario`, runtime UI models, render extraction data, localization, viewport state, or
  app services in root `tests/*_tests.cpp`. These build in `quader_tests`; register new files in
  `CMakeLists.txt`.
- Put polygon/document internals, geometry, polygon operations, QDR document IO, and low-level
  modeling invariants under `tests/modeling_domain/`. Operation tests should live in
  `tests/modeling_domain/operation/qdr_document_operation_<feature>_tests.cpp` and build in
  `quader_modeling_domain_tests`.
- Put runtime modeling operations, runtime command contracts, session behavior, tool lifecycles,
  modeling overlays/picking, repeat/undo/redo, and runtime QDR compatibility under
  `tests/modeling_domain/operation/` or `tests/modeling_domain/tool/`. Runtime operation tests
  should use `qdr_modeling_runtime_operation_<feature>_tests.cpp`.
- Put metadata catalog/schema/placement checks under `tests/modeling_domain/editor_metadata/`.
- Put shared app-test helpers under `tests/support/`; put modeling-domain-only helpers and fixtures
  under `tests/modeling_domain/` or `tests/modeling_domain/fixtures/`. Shared helper changes
  intentionally fan out to broader test runs.
- Keep manual smoke checks only for behavior that still needs a real GPU/window/driver/pixel path:
  Vulkan swapchain creation, native window chrome/layout appearance, material rendering, and screenshot
  or log verification through `tools/boot_smoke.ps1`.

When adding a new test file, register it in the owning CMake target immediately. Root app tests and
guardrails are listed in `CMakeLists.txt`; modeling-domain tests are categorized in
`cmake/QuaderModeling.cmake`. Do not leave tests unbuilt, skipped, or hidden behind local-only
compile definitions.

## Test Selection

Use the smallest test set that covers the changed owner, then run full `ctest` before pushing risky
or cross-cutting changes.

- First ask the planner:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -PlanOnly -ChangedPath path/to/changed_file.cpp
```

- Then run the affected workflow with explicit `-ChangedPath` values when the worktree is dirty or
  the intended change is narrower than the whole diff.
- For a new or edited GoogleTest file, build its owning target and run the declared suite with
  `--gtest_filter=SuiteName.*` before broadening.
- For direct polygon operation source edits, prefer the polygon loop target plus the matching
  `QdrDocumentOperation...` suite. For direct runtime operation source edits, prefer the runtime loop
  target plus the matching `QdrModelingRuntimeOperation...` suite and metadata loop when operation
  descriptors can change.
- For public API, command, scene, selection, history, viewport, UI, render, resource, or app service
  changes, use `tools/affected_check.ps1`; it already maps those source areas to the relevant
  build targets and GoogleTest filters.
- For CMake, dependency, test-infrastructure, or affected-check changes, expect the plan to escalate
  to configure, all three test targets, and `ctest --preset vs2026-debug --output-on-failure`.
- Use `-MeasureFull` whenever the task claims a compile/test speed improvement and record the
  concrete before -> after result in the relevant task summary or `memory.md` entry.

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
This routing only affects test selection; it does not replace the final `quader` executable build
required after any changed file.
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
- Native ImGui shell appears with custom chrome, top menu, docked panels, viewport, and status bar.
- The viewport texture renders app-created polygon meshes with the default material.
- Window resize keeps the viewport texture visible without stale or blank UI content.
- MMB orbits, Shift+MMB pans, mouse wheel zooms.

Use `quader_tests`/`ctest` for headless behavior, rebuild the `quader` target for
resources/materials/runtime UI changes, and use `tools/boot_smoke.ps1` for GPU/window smoke
checks with screenshot and log-copy output when a visual runtime check is required.
