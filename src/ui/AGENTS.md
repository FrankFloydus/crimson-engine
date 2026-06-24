<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Package Guide

Read `../../agents/ui-localization.md` before changing UI code. If UI changes editor state, also
read `../../agents/public-api-and-scripting.md`.

## UI Rules

- UI shell code owns presentation, backend integration, routing, and frame input assembly only.
- User/editor intent should route through `QuaderEditorApi`, `QuaderApi`, commands, or owners.
- The current v1 shell is the RmlUi MainLayout shell: static chrome RML, generated MainLayout body,
  measured viewport frame, and `ViewportHost` rendering/input.
- Keep `UISystem` as composition glue. MainLayout policy belongs in controllers/models/builders;
  generic Rml setup, events, measurement, and rendering belong in `RmlUiContextHost`.
- MainLayout resize must stay on the measured-hit-region path through `ResizableSplitController`.
- Treat `DockLayoutModel` as the source of truth for dock location, tab order, active tabs, and
  split panels. Do not mirror that state in `UISystem`, RML builders, or resources.
- Use `MainLayoutController` for MainLayout mutations; use `MainLayoutSashRegistry` for measured
  sash registration and resize application; use `MainLayoutDockDragController` for dock drag/drop
  semantics; keep `MainLayoutRmlBuilder` as presentation projection only.
- Keep `UIDragDrop`, `UIDragDropController`, `ResizableSplit`, `ResizableSplitController`,
  `UITabStripModel`, and `DockLayoutModel` backend-neutral and panel-agnostic. MainLayout-specific
  panel IDs, RmlUi classes, and placement rules belong in MainLayout owners.
- The bottom dock is part of the editor column with the viewport. Do not move it to full app width
  under the sidebars/status bar, and keep its height sash, split sashes, tab strip, and drop
  indicator measured in that coordinate space.
- Preserve shared collapse behavior for primary side dock, auxiliary side dock, and bottom dock.
  Collapsed docks still expose edge resizers through the same measured-sash path.
- Keep panel routing IDs stable and backend-neutral (`viewport`, `outliner`, `inspector`); localized
  panel titles are display text only.
- `UISettings` persists live UI shell preferences only; viewport rendering preferences remain in
  `ViewportSettings`.
- Preserve frame order: measure Rml before viewport input, then prepare viewport render data.
- Do not recreate the deleted docked panel stack unless a new backend-neutral panel architecture is
  intentionally introduced.
- User-facing labels, menu text, panel titles, settings names, and status text must go through
  localization resources.
- Prefer backend-neutral helpers or generated utility classes for shared visual constants.
- Add/update localization and UI guardrail tests when user-facing UI text or placement changes.
- Add/update focused owner tests when changing MainLayout docking, bottom tabs, split panes,
  drag/drop targets, resize clamps, collapse/restore, or generated RML projection.
- When changing RML, RCSS, UI tokens, or locale resources, rebuild `quader` as well as tests.
