<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Controls

Read this before creating or changing ImGui controls, buttons, top-bar controls, status bars,
chrome controls, panel headers, sidebars, or viewport pane overlays.

## Control Model

- Prefer small local ImGui helpers when a control has repeated styling or behavior.
- Keep helpers close to the owner unless more than one panel or surface genuinely shares them.
- Use `ui_style_tokens` for shared colors, spacing, and control sizing rules.
- Use `UIPanel`/`UIDockPanel` for dockable panels and `UIFloatingPanel` for floating overlays.
- Route commands through `QuaderEditorApi`, typed commands, controllers, or the domain owner.

## Buttons And Icons

- Use icon buttons for common chrome/tool commands when an icon communicates the command clearly.
- Use text buttons for explicit commands whose labels need to be read.
- Keep button hit areas stable across hover/active/disabled states.
- Keep checkbox, combo, and numeric input sizes stable so sidebar content does not shift when
  scrollbars appear.
- Store window chrome actions in `UISystem` and handle them in `App`; do not put native window
  behavior in individual controls.

## Ownership Boundaries

- UI controls own presentation state and event capture only.
- `UISystem` is the composition root. It registers panels, draws top-level chrome, and dispatches
  menu/status actions, but should not duplicate panel internals or editor domain state.
- `ViewportPanel` owns viewport pane UI, overlay controls, context menus, and modal-tool viewport
  context.
- User-facing labels must come from `LocalizationService` or locale resources. Stable internal IDs
  and action names are not localized.

## Tests And Build

- Add focused tests for reusable control helpers and guardrails for shell-level behavior.
- Add new `.cpp` files to the owning target in `CMakeLists.txt`.
- When changing runtime icons, fonts, locales, or materials, rebuild `quader` so post-build copied
  resources are refreshed.
