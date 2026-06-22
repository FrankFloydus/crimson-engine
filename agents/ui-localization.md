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
- `UIPanel`
- `UIContext`
- `UIPanelManager`
- `UISettings`
- `UIStyleTokens`

## UI Ownership

- Keep `UISystem` as the composition root for ImGui initialization, menus, dockspace, layout
  persistence, and panel registration.
- Put panel contents in concrete `UIPanel` classes such as `ViewportPanel`, `OutlinerPanel`,
  `InspectorPanel`, and `ConsolePanel`.
- Route frame dependencies through `UIContext`.
- Do not make panels reach sideways into app globals.
- UI panels and controllers own presentation and routing only.
- For user-intent mutations, prefer `QuaderApi` or future `QuaderEditorApi`.

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

## Layout Rules

- Put shared UI colors, font sizes, paddings, and visual constants in `UIStyleTokens`.
- Persist dock layout through ImGui's ini file and panel visibility through `UISettings`.
- Keep these under the user profile rather than repo-local files.
- Keep ImGui's dock/window menu button available.
- Avoid fragile fill-width widgets in resizable dock panels.
- Use responsive fixed/minimum widths or `UILayout` helpers so inputs cannot collapse to 1px.
- Do not create placeholder startup scene content unless a product requirement explicitly asks.
- For scene names, use `SceneEntityNaming`.
- Mesh names start as `Mesh`, then `Mesh_1`, `Mesh_2`, etc.
- Do not show `Runtime Object X`, `Mesh_0`, or `Mesh 0` names.
- Do not append text suffixes like `[Mesh]` or `[Light]` in the outliner.
