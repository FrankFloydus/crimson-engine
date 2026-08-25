<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Package Guide

Also follow root `AGENTS.md` UI, localization, API, input, and test rules.

## UI Rules

- `UISystem` composes the retained Qt shell: `QMainWindow`, native chrome, docks, tool windows, and
  the central `QtViewportPanel`.
- `main.cpp` selects `Fusion` with `standardPalette()` as the baseline. Explicitly requested style,
  palette, font, icon, or chrome changes stay narrowly scoped and cover accessibility/control states.
- `UISystem` owns composition, dock registration, window-state persistence, localization, and
  top-level presentation actions, never editor/domain behavior.
- `QtViewportPanel` hosts one to four `QtViewportSurface` native-child panes and owns only Qt event
  adaptation, presentation, render coordination, and interaction routing.
- `UIPanelManager` owns public-panel-to-`QDockWidget` mapping and visibility; widgets own only local
  presentation state.
- User/editor intent routes through `QuaderEditorApi`, `QuaderApi`, commands, or owner services.
- Preserve Qt defaults for focus, navigation, selection, editing, activation, context menus, and
  gestures. Prefer configuration, models, delegates, selection models, and signals. Override events
  only for explicit Quader behavior; call the base for unhandled paths and add an interaction test.
- `ViewportWorkspaceService` owns layout, focused pane, view, shading, grid preset, render
  size/visibility, and camera state. Do not mirror them in widgets.
- Route Qt input through `qt_input_adapter.*`, canonical keybindings, and `ViewportInputFrame`.
  Never add a second command map with `QAction` shortcuts or ad-hoc widget key checks.
- User-facing labels, menu text, panel titles, settings names, and status text use localization.
- Update owner tests for changed chrome, panels, layouts, input, settings, or resources.
- Rebuild `quader` and tests for runtime logos, overlay icons, locales, or materials. Stock Qt
  visuals use no theme assets except a documented, palette-aware widget-local accessibility fix.
