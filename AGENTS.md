<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Agent Guide

This file is intentionally thin. It routes agents to the detailed instructions under `agents/`.
Read this file first, then read the domain files that match the task before editing code.

## Project Identity

- This repository is the native standalone Windows Quader app.
- `QuaderEditorApi` is the default whole-editor API for new user/editor intent in the native app.
  Use `QuaderApi` for document/modeling-only code that does not need runtime editor domains.
- Low-level systems remain the owner layers for absorbed modeling, document, asset, render, UI, and platform
  mechanics.

Always read:

- `agents/project-identity.md`
- `agents/public-api-and-scripting.md`
- `agents/domain-ownership.md`
- `agents/testing-build.md`

## Restore Rule

When the user asks to restore previous working behavior, do not redesign, reinterpret, or replace
the behavior. Find the current app behavior, tests, or project history that defines it and integrate
the same logic into the current architecture while preserving ordering, state transitions, side
effects, cache invalidation, and error handling.

## Domain Routing

- API, scripting, command palette, automation, public examples:
  read `agents/public-api-and-scripting.md`, `agents/domain-ownership.md`,
  `agents/docs-public-examples.md`.
- UI, panels, menus, localization, dock layout:
  read `agents/ui-localization.md`; if the task mutates editor state, also read
  `agents/public-api-and-scripting.md`.
- Modeling tools, Polygon-backed operations, component selection, transform previews:
  read `agents/modeling-tools.md`, `agents/public-api-and-scripting.md`.
- Viewport tools, camera, navigation, shortcuts, gizmos, modal tool input:
  read `agents/viewport-tools.md`.
- Rendering, overlays, Filament resources, materials, viewport draw behavior:
  read `agents/rendering-overlays.md`.
- OOP refactors, class boundaries, ownership, command/service extraction:
  read `agents/oop-architecture.md`, `agents/domain-ownership.md`.
- Tests, guardrails, build failures, smoke checks:
  read `agents/testing-build.md` plus the relevant domain file.
- Dependencies, licenses, SDK/third-party changes:
  read `agents/dependencies.md`.

## Public API Rule

Use the public API when code expresses user/editor intent:

```cpp
editor.modeling().createBox(...);
editor.object(object).rename("Wall").select();
editor.scene().batch("Arrange").setTransform(entity, moved).commit();
editor.commands().execute(QuaderCommand::HistoryUndo);
editor.mainViewport().setShadingMode(QuaderShadingMode::Wireframe);
```

Prefer typed command IDs from `QuaderCommand` in C++ call sites. Stable string command IDs remain
the script/command-palette representation. Prefer `QuaderObjectHandle` and `QuaderSceneBatch` for
fluent object workflows and grouped scene edits. `QuaderApiResult`/`QuaderApiVoidResult` support
callable helpers such as `ok()`, `changed()`, `value()`, and `errorMessage()`.

Keep code low-level only when it owns invariants, hot-path mechanics, render resources, SDK
semantics, storage, or platform state. If a UI/test/script needs a missing capability, add the
owner behavior first and expose it through a clean public API method instead of bypassing the API.

## Worktree Rule

The worktree may already be dirty. Do not revert changes you did not make unless the user explicitly
asks. Work with existing edits when they affect the task, and ignore unrelated edits.

## Expected Commands

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
ctest --preset vs2026-debug
```

More build/test/smoke details live in `agents/testing-build.md`.

## Synthetic Behavior Tests

When a change would normally need manual verification of editor behavior, first try to turn that
workflow into a fast, headless synthetic behavior test. Use the public API or
`tests/support/BehaviorScenario` to express real user/editor intent, then inspect document,
selection, mesh, history, overlay, or viewport state. Examples include creating a box, entering
face/edge/vertex mode, picking by ray, dragging a transform gizmo through
`beginTransformDrag`/`updateTransformDrag`/`commitTransformDrag`, and asserting the compiled mesh
bounds/topology and selection summary.

Keep manual smoke checks for real GPU/window/driver behavior only: Vulkan swapchain creation,
dockspace/window appearance, actual material rendering, and pixel output until a render readback
harness exists. Do not leave "open the app and click it" as the only coverage for selection,
commands, modeling operations, transform previews, tool lifecycles, overlays, or history when
those states can be inspected programmatically. Do not add save/load or QDR roundtrip behavior
tests until the editor save/load surface exists; once it does, cover it with the same synthetic
state-inspection style instead of manual verification.
