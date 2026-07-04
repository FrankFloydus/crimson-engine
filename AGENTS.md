<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Agent Guide

This file routes agents to the detailed instructions under `agents/`.
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

## Duplication And Source-Of-Truth Rule

Before adding new behavior, search for existing implementations, helpers, services, command paths,
projection code, cached state, and tests that already solve the same problem. Prefer extending or
moving the owner implementation over copying similar logic into another layer.

Treat these as architecture risks, not cleanup preferences:

- repeated logic that could be one helper/service/command;
- overlapping code with the same behavior but different names or ordering;
- parallel sources of truth for scene, modeling, viewport, UI, history, asset, or render state;
- copied cache/projection/conversion helpers that can drift;
- similar command, selection, transform, hierarchy, or asset-validation behavior written
  differently in public API, UI, services, and low-level owners.

When you find duplication, route the call site to the existing owner if one exists. If no clear
owner exists, create the narrowest owner in the domain that owns the data and invariants, then update
call sites to use it. Add or update guardrail tests when the change prevents future drift.

## Restore Rule

When the user asks to restore previous working behavior, do not redesign, reinterpret, or replace
the behavior. Find the current app behavior, tests, or project history that defines it and integrate
the same logic into the current architecture while preserving ordering, state transitions, side
effects, cache invalidation, and error handling.

## Hard-Resolved Task Memory

After this file, read root `memory.md` for non-trivial or regression-prone work. When resolving a
hard-to-rediscover task, update `memory.md` with the invariant, owners, fix, and verification.
Current code, tests, AGENTS files, and the user request override memory.

## Domain Routing

- API, scripting, command palette, automation, public examples:
  read `agents/public-api-and-scripting.md`, `agents/domain-ownership.md`,
  `agents/docs-public-examples.md`.
- UI, panels, menus, localization, dock layout:
  read `agents/ui-localization.md`; for UI controls/buttons, also read
  `agents/ui-components.md`; if the task mutates editor state, also read `agents/public-api-and-scripting.md`.
  For native ImGui shell work, also read `src/ui/AGENTS.md` and `resources/AGENTS.md` before
  touching UI settings, viewport frame routing, or runtime UI resources.
- Input, keybindings, shortcuts, user rebinding, keybinding contexts, mouse gestures, or shortcut
  persistence:
  read `INPUT_KEYBINDINGS.md`, `agents/public-api-and-scripting.md`, and the relevant owner
  domain file. For viewport navigation input, also read `agents/viewport-tools.md`.
- Modeling tools, Polygon-backed operations, component selection, transform previews:
  read `agents/modeling-tools.md`, `agents/public-api-and-scripting.md`; also read
  `agents/rendering-overlays.md` for hover, source-wire picking, overlays, or face fills.
- Viewport tools, camera, navigation, shortcuts, gizmos, modal tool input:
  read `agents/viewport-tools.md`; if the task touches input routing, shortcut contexts, or user
  customization, also read `INPUT_KEYBINDINGS.md`.
- Rendering, overlays, Filament resources, materials, viewport draw behavior:
  read `agents/rendering-overlays.md`.
- OOP refactors, class boundaries, ownership, command/service extraction:
  read `agents/oop-architecture.md`, `agents/domain-ownership.md`.
- Tests, guardrails, build failures, smoke checks:
  read `agents/testing-build.md` plus the relevant domain file.
- Dependencies, licenses, SDK/third-party changes:
  read `agents/dependencies.md`.

## Nested Package Routing

This repo uses nested `AGENTS.md` files for package guidance. When a root session edits a
specialized subtree, read the nearest nested `AGENTS.md` before editing.

- For source changes, read `src/AGENTS.md`, then `src/<package>/AGENTS.md` when present.
- For tests, docs, examples, resources, CMake, tooling, benchmarks, or third-party changes, read the
  `AGENTS.md` in that top-level directory when present.
- Keep nested files focused on local ownership, routing, and verification rules. Do not duplicate
  project identity, restore, worktree, or build rules from this root file.

## Public API Rule

Use the public API when code expresses user/editor intent, such as
`editor.modeling().create_box(...)`, `editor.scene().batch(...)`,
`editor.commands().execute(quader::quader_command::kEditUndo)`, or
`editor.main_viewport().set_shading_mode(...)`.

Prefer typed command IDs from `quader::quader_command` in C++ call sites. Stable string command IDs remain
the script/command-palette representation. Prefer `QuaderObjectHandle` and `QuaderSceneBatch` for
fluent object workflows and grouped scene edits. `QuaderApiResult`/`QuaderApiVoidResult` support
callable helpers such as `ok()`, `changed()`, `value()`, and `error_message()`.

Keep code low-level only when it owns invariants, hot-path mechanics, render resources, SDK
semantics, storage, or platform state. If a UI/test/script needs a missing capability, add the
owner behavior first and expose it through a clean public API method instead of bypassing the API.

## Worktree Rule

The worktree may already be dirty. Do not revert changes you did not make unless the user explicitly
asks. Work with existing edits when they affect the task, and ignore unrelated edits.

## Versioning Rule

Before final handoff builds after project-file changes, follow the app version bump rule in
`agents/testing-build.md`.

## Expected Commands

Set build/test command timeouts to at least 10 minutes (`600000` ms); short defaults often fail here.

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
cmake --build --preset vs2026-debug --target quader
ctest --preset vs2026-debug
```

After any file change, build the native app executable before handoff:
`cmake --build --preset vs2026-debug --target quader`. Tests alone are not enough.

More build/test/smoke details live in `agents/testing-build.md`.

## Synthetic Behavior Tests

When a change would normally need manual verification of editor behavior, first try to turn that
workflow into a fast, headless synthetic behavior test. Use the public API or
`tests/support/BehaviorScenario` to express real user/editor intent, then inspect document,
selection, mesh, history, overlay, or viewport state.

Keep manual smoke checks for real GPU/window/driver behavior only: Vulkan swapchain creation,
native window chrome/layout appearance, actual material rendering, and pixel output until a
render readback harness exists. Do not leave manual clicking as the only coverage for state that can
be inspected programmatically. More detail lives in `agents/testing-build.md`.
