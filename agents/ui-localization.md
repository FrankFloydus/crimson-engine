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

- Keep `UISystem` as the composition root for the active UI backend and the native viewport shell.
- The current v1 UI is a single full-window viewport hosted by RmlUi. Do not recreate the deleted
  docked panel stack unless a new backend-neutral panel architecture is intentionally introduced.
- UI shell code owns presentation, backend integration, and input routing only.
- Do not make UI code reach sideways into app globals.
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

- Put shared UI colors, font sizes, paddings, and visual constants in backend-neutral theme or layout
  helpers when those surfaces exist.
- Persist only live UI preferences through `UISettings`; viewport rendering preferences belong in
  `ViewportSettings`.
- Keep these under the user profile rather than repo-local files.
- Avoid fragile fill-width widgets in resizable UI surfaces.
- Use responsive fixed/minimum widths or backend-neutral layout helpers so inputs cannot collapse to
  1px.
- Do not create placeholder startup scene content unless a product requirement explicitly asks.
- For scene names, use `SceneEntityNaming`.
- Mesh names start as `Mesh`, then `Mesh_1`, `Mesh_2`, etc.
- Do not show `Runtime Object X`, `Mesh_0`, or `Mesh 0` names.
- Do not append text suffixes like `[Mesh]` or `[Light]` in the outliner.
