# Quader Agent Guide

This file is intentionally thin. It routes agents to the detailed instructions under `agents/`.
Read this file first, then read the domain files that match the task before editing code.

## Project Identity

- This repository is the native standalone Windows Quader app.
- It is not the Unity plugin and should not preserve old plugin/DLL protection behavior unless a
  native-app requirement explicitly asks for it.
- `QuaderEditorApi` is the default whole-editor API for new user/editor intent in the native app.
  Use `QuaderApi` for document/modeling-only code that does not need runtime editor domains.
- Low-level systems remain the owner layers for SDK, document, asset, render, UI, and platform
  mechanics.

Always read:

- `agents/project-identity.md`
- `agents/public-api-and-scripting.md`
- `agents/domain-ownership.md`
- `agents/testing-build.md`

## Restore And Parity Rule

When the user asks to restore previous working behavior or asks for 100% Unity parity, do not
redesign, reinterpret, or replace the behavior. Find the source behavior and integrate the same
logic into the current architecture while preserving ordering, state transitions, side effects,
cache invalidation, and error handling.

For parity/restoration work, also read:

- `agents/sdk-unity-parity.md`

## Domain Routing

- API, scripting, command palette, automation, public examples:
  read `agents/public-api-and-scripting.md`, `agents/domain-ownership.md`,
  `agents/docs-public-examples.md`.
- UI, panels, menus, localization, dock layout:
  read `agents/ui-localization.md`; if the task mutates editor state, also read
  `agents/public-api-and-scripting.md`.
- Modeling tools, SDK-backed operations, component selection, transform previews:
  read `agents/sdk-unity-parity.md`, `agents/modeling-tools.md`,
  `agents/public-api-and-scripting.md`.
- Viewport tools, camera, navigation, shortcuts, gizmos, modal tool input:
  read `agents/viewport-tools.md`, `agents/sdk-unity-parity.md`.
- Rendering, overlays, Filament resources, materials, viewport draw behavior:
  read `agents/rendering-overlays.md`, `agents/sdk-unity-parity.md`.
- OOP refactors, class boundaries, ownership, command/service extraction:
  read `agents/oop-architecture.md`, `agents/domain-ownership.md`.
- Tests, guardrails, build failures, smoke checks:
  read `agents/testing-build.md` plus the relevant domain file.
- Dependencies, licenses, SDK/third-party changes:
  read `agents/dependencies.md`, and for SDK behavior also read `agents/sdk-unity-parity.md`.

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

For SDK tests:

```powershell
ctest --preset vs2026-sdk-tests-debug --output-on-failure
```

More build/test/smoke details live in `agents/testing-build.md`.
