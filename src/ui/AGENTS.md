<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Package Guide

Read `../../agents/ui-localization.md` before UI edits, and `../../agents/ui-components.md` for
controls/buttons. If UI changes editor state, also read
`../../agents/public-api-and-scripting.md`.

## UI Rules

- Current UI is the native ImGui shell: custom chrome, menu bar, docked panels, static bottom bar,
  and multi-pane viewport live under `UISystem` and `ViewportPanel`.
- `UISystem` owns composition glue, panel registration, settings, localization, and top-level
  chrome actions.
- `ViewportPanel` owns viewport pane layout, pane chrome, input frame assembly, render prep,
  overlay submission, context menus, modal-tool viewport context, and transform gizmo routing.
- `UIPanelManager` owns panel registration and visibility; individual panels own their local
  presentation only.
- User/editor intent routes through `QuaderEditorApi`, `QuaderApi`, commands, or owner services.
- `ViewportWorkspaceService` owns viewport layout, focused pane, pane view, shading, grid preset,
  render size/visibility, and pane camera state. Do not mirror those fields in UI widgets.
- User-facing labels, menu text, panel titles, settings names, and status text use localization.
- Add/update owner tests when changing chrome, panels, sidebar sizing, viewport pane layout,
  viewport input routing, UI settings, or resource packaging.
- When changing runtime icons, fonts, locales, or materials, rebuild `quader` as well as tests.
