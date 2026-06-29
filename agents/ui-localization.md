<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI And Localization

Use `UI` casing for UI infrastructure types:

- `UISystem`
- `UISettings`

## UI Ownership

- The current standalone UI is the native ImGui shell: `UISystem` owns top-level frame
  composition, custom chrome, menu/status bars, panel registration, settings, and localization.
- `ViewportPanel` owns viewport pane drawing, pane chrome, viewport input assembly, render-target
  preparation, overlay submission, context menus, transform gizmo routing, and modal-tool viewport
  context.
- `UIPanelManager` owns dock panel registration and visibility. Individual panels own their own
  presentation state and must not duplicate editor/modeling/render state.
- UI shell code owns presentation, backend integration, and input routing only.
- Do not make UI code reach sideways into app globals.
- For whole-editor UI intent, prefer `QuaderEditorApi`. Use `QuaderApi` only for
  document/modeling-only paths that do not need runtime editor domains.
- Stable panel IDs are backend-neutral identifiers such as `viewport`, `outliner`, `inspector`,
  and `console`. Localized labels are display text only and must not become state or routing IDs.
- Window placement and native resize/maximize behavior belong to `PlatformWindow` and
  `AppWindowSettings`; UI code should only surface chrome actions.

## Layout And Panels

- Persist only live UI preferences through `UISettings`; viewport rendering preferences belong in
  `ViewportSettings`, and window placement belongs in `AppWindowSettings`.
- Keep user settings under the user profile rather than repo-local files.
- Avoid fragile fill-width widgets in resizable UI surfaces.
- Use fixed/minimum widths, table columns, or local layout helpers so controls cannot collapse to
  1px or shift when scrollbars appear/disappear.
- `ViewportWorkspaceService` owns viewport layout, focused pane, pane view, pane shading, grid
  preset, render size/visibility bookkeeping, and pane cameras. Do not mirror that state in UI.
- `ViewportPanel` should call into the workspace service for layout and pane state, then use
  render/overlay owners for GPU-facing data.
- Do not create placeholder startup scene content unless a product requirement explicitly asks.
- For scene names, use `SceneEntityNaming` except for explicit product rules such as default group
  creation names.
- Mesh names start as `Mesh`, then `Mesh_1`, `Mesh_2`, etc.
- Do not show `Runtime Object X`, `Mesh_0`, or `Mesh 0` names.
- Do not append text suffixes like `[Mesh]` or `[Light]` in the outliner.

## Localization

Always keep localization in mind when adding/changing user-facing UI text.

Route these through `LocalizationService`:

- labels;
- menu items;
- panel titles;
- tool names;
- settings names;
- status text;
- undo/history text.

Update `resources/locales/*.lang` and localization/guardrail tests in the same change.

Raw English literals are acceptable for stable internal identifiers, logs, and non-user-facing
fallback/debug text.

## Frame And Resources

- `App` frame order matters: let `UISystem` draw and update the ImGui shell, prepare viewport
  render data before the Filament frame, then render viewport surfaces.
- Runtime UI resources currently include ImGui material output, fonts, icons, and locales copied by
  the `quader` target post-build step. Rebuild `quader`, not only `quader_tests`, when changing
  those resources.
- Verify copied files under `build/vs2026/Debug` when debugging a running UI mismatch.

## UI Guardrail Tests

- Update focused tests when changing window chrome, top/bottom bars, panel visibility, panel
  sizing, viewport pane layout, viewport input routing, or UI settings persistence.
- Cover `UISystem`, `UIPanelManager`, `ViewportPanel`, `UISettings`, `ViewportSettings`, and
  `PlatformWindow` at the owner that owns the rule being changed.
- Use synthetic behavior tests for inspectable editor/modeling state.
- Use manual smoke checks only for actual OS cursor appearance, GPU/window output, material
  rendering, or visual polish that cannot be asserted headlessly yet.
