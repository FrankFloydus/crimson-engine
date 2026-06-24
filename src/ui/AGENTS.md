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
retained components/buttons. If UI changes editor state, also read
`../../agents/public-api-and-scripting.md`.

## UI Rules

- UI shell code owns presentation, backend integration, routing, and frame input assembly only.
- User/editor intent routes through `QuaderEditorApi`, `QuaderApi`, commands, or owners.
- Current v1 UI is the RmlUi MainLayout shell: static chrome RML slots, generated MainLayout body,
  measured viewport frame, and `ViewportHost` rendering/input.
- Keep `UISystem` as composition glue. MainLayout policy belongs in controllers/models/builders;
  generic Rml setup, events, measurement, and rendering belong in `RmlUiContextHost`.
- MainLayout resize stays on measured hit regions through `ResizableSplitController`.
- `DockLayoutModel` owns dock location, tab order, active tabs, and split panels; do not mirror
  that state in `UISystem`, RML builders, or resources.
- Use `MainLayoutController`, `MainLayoutSashRegistry`, and `MainLayoutDockDragController` for
  MainLayout mutation, measurement, and drag/drop semantics. `MainLayoutRmlBuilder` is projection.
- Keep `UIDragDrop`, `ResizableSplit`, `UITabStripModel`, and `DockLayoutModel` backend-neutral.
- The bottom dock belongs in the editor column with the viewport, including sashes, tabs, and drop
  indicators.
- Preserve shared collapse behavior for primary side dock, auxiliary side dock, and bottom dock.
- Do not recreate the deleted docked panel stack without a new backend-neutral panel architecture.
- User-facing labels, menu text, panel titles, settings names, and status text use localization.
- Add/update owner tests when changing docking, tabs, split panes, drag/drop, resize, collapse, or generated RML.
- When changing RML, RCSS, UI tokens, or locale resources, rebuild `quader` as well as tests.
