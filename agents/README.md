<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Agent Instructions

This folder contains domain-specific instructions for agents working in the native Quader app.

Read `../AGENTS.md` first, then root `../memory.md` when it exists and the task is non-trivial or
touches a historically subtle system. Then read only the domain files needed for the current task.
Prefer this routing over loading every instruction file.

## Files

- `project-identity.md`: repository identity, source-of-truth paths, restore rules.
- `public-api-and-scripting.md`: when to add public API, whole-editor API direction, Lua rules.
- `../INPUT_KEYBINDINGS.md`: input, mouse/keybinding, context, and future user rebinding rules.
- `domain-ownership.md`: where behavior belongs across document, modeling, UI, viewport, render,
  SDK, and assets.
- `oop-architecture.md`: class/interface/service/command refactor rules.
- `modeling-tools.md`: modeling command tool ownership and catalog rules.
- `viewport-tools.md`: viewport tool contracts and interaction ownership.
- `rendering-overlays.md`: Filament, overlay, depth, wire/point/face-fill rules.
- `ui-localization.md`: UI architecture, localization, dock/panel rules.
- `testing-build.md`: TDD workflow, build commands, smoke checks.
- `dependencies.md`: CMake/vcpkg/dependency/license rules.
- `docs-public-examples.md`: public documentation and example rules.
