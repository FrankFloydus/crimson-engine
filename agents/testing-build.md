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

The native test suite is split into the GoogleTest `quader_tests` target and the Catch2
`quader_modeling_domain_tests` aggregate. Both run through `ctest --preset vs2026-debug`. Do not
add SDK-specific configure, build, or test presets back to the native app.

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
