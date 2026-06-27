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
- The current v1 UI is the RmlUi MainLayout shell: a toolbar, central viewport host, optional
  sidebar panels, and status bar projected into `resources/ui/rml/viewport-shell.rml` plus
  generated MainLayout RML. Do not recreate the deleted docked panel stack unless a new
  backend-neutral panel architecture is intentionally introduced.
- UI shell code owns presentation, backend integration, and input routing only.
- Do not make UI code reach sideways into app globals.
- For whole-editor UI intent, prefer `QuaderEditorApi`. Use `QuaderApi` only for
  document/modeling-only paths that do not need runtime editor domains.
- `UISystem` wires lifecycle, settings, localization, RmlUi, and `ViewportHost`, but it should not
  grow into the owner of panel domain behavior, viewport workspace state, or renderer algorithms.
- RmlUi backend hosts own generic Rml context, resource loading, event translation, rendering, and
  element measurement. They must not own MainLayout-specific resize gesture state.
- MainLayout resize gestures are owned by measured UI hit regions and `ResizableSplitController`;
  keep pointer capture, cancellation, cursor state, and drag lifecycle in that single path.
- Stable panel IDs are lowercase backend-neutral identifiers such as `viewport`, `outliner`, and
  `inspector`. Localized labels are display text only and must not become state or routing IDs.

## MainLayout Docking And Resize

- Treat `MainLayoutModel` as the persisted shell state and `DockLayoutModel` as the
  backend-neutral source of truth for panel location, panel order, active tabs, and split panels.
  Do not cache those facts again in RML builders, RCSS, `UISystem`, or ad-hoc UI maps.
- Route layout mutations through `MainLayoutController` or a narrower common model owner. Add new
  dock, split, tab, collapse, or resize operations there before wiring UI events to them.
- Keep `MainLayoutRmlBuilder` as a projection of `MainLayoutModel`, localized labels, and transient
  interaction state. It may choose markup and classes for the current presentation, but it must not
  decide drop policy, resize clamps, active-tab repair, or persistence defaults.
- `MainLayoutSashRegistry` owns measured MainLayout sash registrations and converts measured RML
  rectangles into `ResizableSplitHandle` constraints. `ResizableSplitController` owns the generic
  drag math. Do not add sidebar-only or bottom-only resize paths outside this measured-sash path.
- Side docks and the bottom dock must share the same collapse/expand semantics. Preserve collapsed
  edge resizers, snap-collapse behavior, and measured-bound normalization when changing either one.
- `MainLayoutDockDragController` owns MainLayout-specific mapping from measured panel, tab, header,
  and dock-edge regions to dock operations. `UIDragDropController` and `UIDragDrop` remain generic
  and reusable for outliner trees, asset manager items, viewport drops, and future multi-selection
  drags; they must not know MainLayout panel IDs or RmlUi class names.
- Use `UITabStripModel` for tab-strip normalization and active-item repair. Do not hand-roll active
  tab defaults or duplicate removal in builders, controllers, or settings code.
- Moving a panel between left/right side docks and the bottom dock must remove it from its previous
  location, preserve the stable panel ID, repair active tab state, and keep split-panel membership
  coherent through `DockLayoutModel` helpers.
- The bottom dock belongs inside `main-layout-editor-column` with the viewport, not full-width under
  the sidebars or status bar. Its height resizer, tab strip, split panes, and drop indicator must be
  constrained to that editor-column coordinate space.
- Keep visual feedback separate from hit testing. RCSS/RML may render lines, overlays, ghost
  previews, zero-width split visuals, or insertion markers, but measured hit regions, accepted drop
  targets, non-droppable state, and resize constraints belong in the controllers/models.

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
- Prefer token-backed utility classes for RML/RCSS colors and sizes. Do not duplicate palette hex
  values in static RCSS when generated utility classes can express the same styling.
- Persist only live UI preferences through `UISettings`; viewport rendering preferences belong in
  `ViewportSettings`.
- Keep these under the user profile rather than repo-local files.
- Normalize persisted MainLayout dimensions through MainLayout helpers/controllers so settings,
  generated RML, and resize behavior do not drift.
- `App` frame order matters: update/measure RmlUi layout before viewport input is consumed, then
  prepare viewport render targets and register viewport textures before Rml render data.
- When RML, RCSS, UI token, locale, or material resources change, rebuild the `quader` target, not
  only `quader_tests`, because runtime resources are copied by the app target post-build step.
  Verify copied files under `build/vs2026/Debug` when debugging a running UI mismatch.
- Avoid fragile fill-width widgets in resizable UI surfaces.
- Use responsive fixed/minimum widths or backend-neutral layout helpers so inputs cannot collapse to
  1px.
- Do not create placeholder startup scene content unless a product requirement explicitly asks.
- For scene names, use `SceneEntityNaming`.
- Mesh names start as `Mesh`, then `Mesh_1`, `Mesh_2`, etc.
- Do not show `Runtime Object X`, `Mesh_0`, or `Mesh 0` names.
- Do not append text suffixes like `[Mesh]` or `[Light]` in the outliner.

## UI Guardrail Tests

- Update focused tests when changing docking, tab strips, sashes, drag/drop, panel placement,
  collapse behavior, or persisted MainLayout settings.
- Cover `DockLayoutModel`, `UITabStripModel`, `MainLayoutController`, `MainLayoutSashRegistry`,
  `ResizableSplitController`, `UIDragDropController`, `MainLayoutDockDragController`, and
  `MainLayoutRmlBuilder` at the owner that owns the rule being changed.
- Test bottom-dock placement as part of the editor column, panel moves removing the source
  placement, tab insertion line feedback, split-panel resizing, collapsed edge resizers, and
  non-droppable drag targets when those workflows change.
- Use manual smoke checks only for actual RmlUi rendering, OS cursor appearance, GPU/window output,
  or visual polish that cannot be asserted headlessly yet.
