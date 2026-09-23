<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI Package Guide

## Owners and presentation

- `UISystem` composes `QMainWindow`, docks, tool windows, localization, and retained presentation.
  `UIPanelManager` maps public panels to docks. Widgets dispatch editor intent through public APIs
  and commands; they own only presentation state.
- Outliner drag/drop uses Qt's native `InternalMove` view and model MIME/drop hooks, with
  `scene().move_entities()` owning atomic hierarchy/order edits and history. The model must not
  apply Qt's trailing source-row removal after the scene has already moved those entities.
  Names depict visibility/lock state; the tree has no property checkbox columns.
- The material browser presents the project catalog's physical folders, including empty ones.
  All is a synthetic recursive filter and the root import/drop destination. Folder and material
  drags carry a project generation; mutations go through `QuaderProjectAssetsApi`, and refreshed
  owner snapshots supply the resulting tree. Permanent folder deletion requires the public
  dependency preview and its matching revision. Picker mode only browses folders.
  Ctrl+wheel within a focused browser adjusts its existing thumbnail-size setting;
  ordinary wheel scrolling and input outside that browser retain their ownership.
- `QtFaceTexturePanel` owns the single-face canvas and retained texture controls.
  Both material-browser hosts reuse one implementation, but
  only the embedded browser assigns explicit materials to selected faces. Canvas gestures use
  the public modeling preview lifecycle; widget state never becomes UV or material truth.
  With no selected face, retain the bound controls with disabled actions, numeric fields,
  and field labels; numeric values show em dashes and the canvas stays empty. Unbinding still
  abandons their presentation.
  Visible browsers receive revision-gated catalog and usage updates during normal refresh,
  even after workspace events were consumed while hidden or the scene was already dirty.
  A browser generation change invalidates its usage/dependency presentation stamps even when
  the corresponding owner revisions are unchanged.
- `QtViewportPanel` hosts one to four native-child `QtViewportSurface` panes. Workspace state and
  cameras belong to `ViewportWorkspaceService`; Filament resources belong to render.
  Hover presents tool feedback without taking keyboard focus from panel editors. The first pointer
  press acquires focus and performs its action in the same gesture. One masked native orange border
  marks the focused pane, including single/maximized layouts; panel focus clears it. It shares the
  current-material outline color and gates geometry/mask/stacking updates before widget work.
- Its retained settings overlay shows only actual settings controls and their actions, with the
  operation title in the disclosure header. No descriptions or empty shell are shown. Expansion
  starts expanded for each activation; an explicit collapse lasts for that activation. Scene-owned origin
  metadata pins the overlay to the initiating pane; hover and parameter edits never move it.
  Collapse settles field drafts without terminating the operation. See
  `tests/qt_viewport_settings_overlay_tests.cpp` and the settings-control tests.
- `main.cpp` selects Fusion; `qt_application_theme.*` supplies the Obsidian palette and live system
  High Contrast fallback. Preserve native control behavior and accessible default, hover, selected,
  pressed, focused, inactive, and disabled states when styling.
- Reuse localization, settings descriptors, models, delegates, and signals. Call base event handlers
  on unhandled paths. Input follows [INPUT_KEYBINDINGS.md](../../INPUT_KEYBINDINGS.md): Qt adaptation,
  canonical keybindings, then semantic viewport frames. Do not add a parallel `QAction` shortcut map.
- The viewport RMB menu presents applicable modeling commands directly, separated by dividers.
  Apply the explicit operand scope in `viewport_context_menu_policy` to catalog, supplemental,
  and fixed actions alike before checking native availability. Activation eligibility is not menu
  relevance: component/control selections must not expose whole-object operations. All toolbar
  tool launchers stay outside RMB; selection operations with previews remain eligible.
  Clipboard requires supported mesh/instance targets. Supplement
  from canonical presentation metadata only on popup opening, deduplicate stable command IDs,
  and keep disabled commands out. General selection, view, and UV-editing commands stay outside
  this menu. Its three face material/texture transfers
  retain the selected source/destinations separately from the clicked face and reuse their public
  modeling API routes; the nested menu's revision fence protects both targets.
- `QtStatusMessage` owns error contrast and the portable Qt system alert. Announce new failures once;
  unchanged frames, localization and runtime rebinding must not replay sounds. `UISystem` retains
  direct notifications separately from command status, with errors taking priority over tool hints.
  Direct user actions publish failures through `editor.ui().notify`; command dispatch already owns
  its status. Tool result ports identify explicit attempts so hover/preview refreshes remain silent.
- Floating numeric inputs use `QtDoubleSpinBox`; other numeric control text uses
  `qt_format_number`. Show at most four decimal places without trailing zeroes, while retaining
  owner-defined precision and ranges. Unedited rounded text must not change the stored value.
  Face texture Scale U/V deliberately retain four decimal places and share bounded binary steps
  with the SDK texture actions; Shift and Rotate retain compact formatting.
- Settings categories group existing descriptors by user purpose. Undo/autosave share
  `SessionPreferences`: stage changes, merge per field, persist, then publish SessionCommit.
  File > Recover > Auto Save opens the native file picker in the owner-provided autosave folder;
  use `scene_documents().recover_autosave_native_path()` and reject stale runtime bindings.
  File > Open Recent loads its five scene records on menu opening, never during frame refresh.
  Give each group one purpose: language and interface scaling are separate groups; do not join
  unrelated controls under a combined heading merely because they share a page.
  Settings contains preferences without equivalent controls in other menus, panels, or toolbars.
  Workspace toggles for snapping, backface selection, texture locks, and display visibility belong
  in their menus/toolbars. Appearance values, picking tolerances, and gizmo size are separate
  preferences and remain in Settings when no other surface edits them.

## Retained frame performance

- Build static controls once. In `UISystem::refresh` and viewport callbacks, check owner revisions
  before descriptors, predicates, shortcut resolution, formatting, snapshots, or widget setters.
  `RetainedUiPresenter` and the existing presentation stamps are the retained synchronization path.
- Query fixed commands with `commands().descriptor(id)`; never enumerate `commands().available()`
  in a frame callback. Share cheap owner observations and skip unchanged Qt setters.
- Explicitly invalidate on rebind, localization, keybinding, and other non-revision transitions.
  Do not compensate for UI polling by reducing render cadence or input responsiveness.
- Preview-backed overlay stamps must carry the modeling owner's projection signature through the
  outer submission gate; follow the [preview projection contract](../editor/modeling/runtime/README.md#live-preview-projection-contract).
  A fresh inner overlay cache cannot correct a stale outer stamp.
- Frame synchronization changes require a regression test for the gate and relevant UI coverage,
  included in the root-selected verification path.
  Reported performance regressions also require same-build, same-scene before/after measurements.

## Runtime replacement and nested events

- `install_runtime_hooks()` abandons outgoing panel/interactor state before binding replacements.
  Rebind abandonment must not call old runtime owners; normal shutdown may cancel through live
  owners and must clear runtime hooks before destroying callback owners.
- `QtViewportPanel::abandon_interactions()` invalidates `ViewportRuntimeBindingToken`, drops raw
  owners/cameras, and clears queued/held surface input plus trailing releases. After synchronous
  commands, tool phases, public mutations, and nested presenters, check the token before using
  previously sampled state. Stop remaining same-frame input after a rebind.
- Retained menus and command filters rebind in place. Outliner, Inspector, and Layers use owner-free
  abandonment and generation fences around mutations and nested event loops.

Verification pointers: `tests/qt_input_adapter_tests.cpp`, `tests/qt_project_workspace_tests.cpp`,
the affected `tests/qt_*_tests.cpp`, `tests/ui_layout_guardrail_tests.cpp`, and
`tests/qt_application_theme_tests.cpp`. Follow root build/test requirements; use
`tools/boot_smoke.ps1` for native layout, GPU, and pixel behavior.
