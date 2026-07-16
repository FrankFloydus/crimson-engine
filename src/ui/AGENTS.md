<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Package Guide

Also follow root `AGENTS.md` UI, localization, API, viewport/input, and testing rules.

## UI Rules

- The retained Qt Widgets shell uses `UISystem` to compose `QMainWindow`, native menu/status bars,
  `QDockWidget` panels, settings/tool windows, and the central `QtViewportPanel`.
- `main.cpp` selects `Fusion` with its `standardPalette()` as the current default baseline, not an
  immutable style restriction. Make style, palette, font, icon, or chrome changes when explicitly
  directed by the user's current request. Prefer narrowly scoped presentation owners and verify
  accessibility plus every affected native control state.
- `UISystem` owns composition, dock registration, window-state persistence, localization, and
  top-level presentation actions, never editor/domain behavior.
- `QtViewportPanel` hosts one to four panes. Each embeds a native child `QWindow` through
  `QtViewportSurface` and owns only Qt event adaptation, presentation, render submission
  coordination, and interaction routing.
- `UIPanelManager` owns public-panel-to-`QDockWidget` mapping and visibility; widgets own only local
  presentation state.
- User/editor intent routes through `QuaderEditorApi`, `QuaderApi`, commands, or owner services.
- `ViewportWorkspaceService` owns layout, focused pane, view, shading, grid preset, render
  size/visibility, and camera state. Do not mirror them in widgets.
- Route Qt key, pointer, wheel, focus, and text-capture state through `qt_input_adapter.*`, the
  canonical keybinding service, and semantic `ViewportInputFrame`. Never create a second command
  map with `QAction` shortcuts or ad-hoc widget key checks.
- User-facing labels, menu text, panel titles, settings names, and status text use localization.
- Update owner tests for chrome, panels, sidebar sizing, pane layout/input, settings, or resources.
- For runtime logos, overlay icons, locales, or materials, rebuild `quader` and tests. Stock Qt UI
  visuals do not use packaged theme fonts or icon sets.
