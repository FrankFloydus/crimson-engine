# Quader Agent Instructions

This folder contains domain-specific instructions for agents working in the native Quader app.

Read `../AGENTS.md` first, then read only the files needed for the current task. Prefer this routing
over loading every instruction file.

## Files

- `project-identity.md`: repository identity, source-of-truth paths, restore rules.
- `public-api-and-scripting.md`: when to add public API, whole-editor API direction, Lua rules.
- `domain-ownership.md`: where behavior belongs across document, modeling, UI, viewport, render,
  SDK, and assets.
- `oop-architecture.md`: class/interface/service/command refactor rules.
- `sdk-unity-parity.md`: Unity parity, SDK boundaries, transform/selection behavior.
- `modeling-tools.md`: modeling command tool ownership and catalog rules.
- `viewport-tools.md`: viewport tool contracts and interaction ownership.
- `rendering-overlays.md`: Filament, overlay, depth, wire/point/face-fill rules.
- `ui-localization.md`: UI architecture, localization, dock/panel rules.
- `testing-build.md`: TDD workflow, build commands, smoke checks.
- `dependencies.md`: CMake/vcpkg/dependency/license rules.
- `docs-public-examples.md`: public documentation and example rules.
