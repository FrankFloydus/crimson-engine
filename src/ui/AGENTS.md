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
- [QtMainWindow](qt_main_window.cpp) may repost a root `UpdateRequest` at most twice when native
  `QS_KEY` or QPA keys are pending, covering native-to-QPA delivery without pumping events or taking
  input ownership. The next pass must paint even when user input is excluded; no-key updates paint
  immediately. Preserve matching layout/projection and the
  [input-before-paint regression](../../tests/qt_settings_dialog_tests.cpp).
- Immutable product releases filter controls at construction through the shared release feature
  requirements. Omit excluded docks and tool windows so restored layouts cannot reopen them;
  keep null action slots safe in retained toolbar/pane synchronization. Camera settings, mesh
  shadow controls and advanced selection discovery can be hidden independently of their native
  values and APIs. Feature execution gates remain with the native owners.
- Outliner drag/drop uses Qt's native `InternalMove` view and model MIME/drop hooks, with
  `scene().move_entities()` owning atomic hierarchy/order edits and history. The model must not
  apply Qt's trailing source-row removal after the scene has already moved those entities.
  Names depict visibility/lock state; the tree has no property checkbox columns.
- `QtAssetBrowser` presents scenes, materials, textures and physical project folders in a bottom
  dock. Mutations go through `QuaderProjectAssetsApi`; owner snapshots supply the resulting tree.
  Drags and clipboard requests carry project and catalog revisions. Recycling requires the public
  dependency preview and its matching revision. `QtProjectAssetDialog` shares the folder projection
  for project-rooted scene open/save; native owners enforce physical containment as well.
  Clipboard uses canonical command dispatch while native text editors retain their own shortcuts.
  Request material thumbnails from visible item painting only, never model layout or every frame;
  `QtAssetThumbnailCache` bounds queued requests and retains generation/revision/DPI fences.
  Ctrl+wheel within a focused browser adjusts its existing thumbnail-size setting;
  ordinary wheel scrolling and input outside that browser retain their ownership.
  Reveal reuses retained rows and defers one owner discovery until the current exposure's
  first item-view paint. Closing before that paint abandons pending discovery; unchanged
  frames never repeat it. Retain no filesystem-watch handles that block folder replacement.
  Composed viewport layout keeps the main window's existing update state; do not globally disable
  and re-enable updates around browser visibility. Coalesce viewport resize painting until the
  final layout so the next common composition uses the matching pane projection. The native-child
  fallback retains synchronous update suppression and restores the prior update/animation flags.
  Preserve the animation setting in either route. On reveal, activate the dock's layout
  immediately after setting visibility, before raising it or activating the main-window layout;
  its minimum must reflect current child metrics before the first paint.
- `QtFaceTexturePanel` owns the single-face canvas and retained texture controls.
  Texture Settings keeps Scale U/V on one row, Shift U/V on the next, then Rotation,
  leaving vertical space for the canvas and material browser.
  Its embedded material browser has no folders or retained folder filtering and assigns explicit
  materials to selected faces. Canvas gestures use
  the public modeling preview lifecycle; widget state never becomes UV or material truth.
  With no selected face, retain the bound controls with disabled actions, numeric fields,
  and field labels; numeric values show em dashes and the canvas stays empty. Unbinding still
  abandons their presentation.
  Visible browsers receive revision-gated catalog and usage updates during normal refresh,
  even after workspace events were consumed while hidden or the scene was already dirty.
  A browser generation change invalidates its usage/dependency presentation stamps even when
  the corresponding owner revisions are unchanged.
- `QtViewportPanel` hosts one to four panes. Production Vulkan panes use
  `QtVulkanCompositionWidget` (`QRhiWidget`) so scene textures, dock layout and ordinary overlays
  reach one Qt window composition. Workspace state/cameras remain in `ViewportWorkspaceService`;
  Filament resources and retained offscreen backing belong to render. Each pane retains its fixed
  control strip above the visible render area.
  `QtViewportSurface` remains the single input adapter and renderer owner. In composed mode its
  QWindow object has no native handle or window container: widget events feed the same input
  accumulator, and focus, grabs, cursor, picking, popup and HUD coordinates use the visible widget.
  Do not introduce a second input state or accidentally call `winId()` on that adapter.
  A widget resize updates empty-size state and queues painting; it must not trigger immediate GPU
  work. After the resize burst settles, the queued paint prepares the current projection, then Qt
  ensures/initializes the texture and draws once at the final geometry before common composition.
  Gate preparation by the panel's geometry revision; unchanged paints must not repeat scene work.
  Preserve this ordering when reviewing the pinned Qt resize handler. The widget lends its current
  Qt texture to render for the GPU copy. The pinned Qt private adapter
  drains external command producers before offscreen painting and root `beginCompose`, including
  nested event loops. Review those hooks when changing Qt versions. Qt owns composed-window VSync;
  native previews retain render's native presentation path.
  The retained bootstrap anchor keeps Qt's device alive across home/workspace changes and until
  render teardown. Disconnect callbacks and release render resources before destroying that device.
  Native-child panes remain available for standalone render/tests and other backends. Preserve
  `Qt::AA_DontCreateNativeWidgetSiblings`, `Qt::WA_DontCreateNativeAncestors`, bounded ancestor
  observation and cached OS geometry queries for that route. Its reserved backing stays at `(0, 0)`
  inside the visible clip; input from both clip and inner container uses the same surface adapter.
  Verify actual composed transition pixels: changed bounds with an old crop followed by a new
  projection is a failure. See the [presentation contract](../../thirdparty/filament/README.md#qt-composed-viewport-presentation)
  and [queued resize, real App and legacy geometry fixtures](../../tests/qt_project_workspace_tests.cpp).
  Protect resize deferral with the GPU-free `ComposedViewportResizeDefersPreparationUntilEnabledPaint`
  test, then measure actual App native-message delivery and coherent captured frames. Dispatch or renderer
  extent timings alone cannot establish input-to-display latency.
  Strip actions have independent
  checked-state presentation and dispatch through the focused pane's public API; never share
  checkable QActions between panes. Camera and Shading stay visible when icons overflow.
  Hover presents tool feedback without taking keyboard focus from panel editors. The first pointer
  press acquires focus and performs its action in the same gesture. One orange border marks the
  focused pane, including single/maximized layouts; panel focus clears it. It is an ordinary widget
  in composed mode and a masked native child over native panes. It shares the
  current-material outline color and gates geometry/mask/stacking updates before widget work.
  Dimension HUDs observe the bounded QWidget ancestor chain through the top-level window,
  including non-native containers. Rebuild that chain on reparenting; geometry/hide transitions
  retire accepted projections even when pane-local bounds stay unchanged.
- Each pane's Environment launcher opens one retained `QtEnvironmentSettingsPopup` immediately
  left of the display menu. Its collapsible Environment, Fog, Camera and Post Process controls
  edit document settings through the public query/scene APIs, including empty documents.
  Object selection does not own or interrupt these edits. Gate retained refresh on the document
  settings revision before copying values; runtime replacement abandons dialogs and numeric
  gestures without accessing the old owners. See `tests/qt_environment_settings_popup_tests.cpp`.
- Its single retained settings overlay stays in the pane that initiated the shared operation and
  queries that origin's settings without activating it. If that pane is hidden by the layout,
  temporarily host it in a visible pane and restore its anchor when the origin returns.
  It shows only actual settings controls and their actions, with the
  operation title in the disclosure header. No descriptions or empty shell are shown. Expansion
  starts expanded for each activation; an explicit collapse lasts for that activation. Scene-owned
  origin metadata pins the overlay's visual position and fences its controls. Visible panes project
  the same active operation and handles; each camera owns only its projected presentation.
  Focus changes neither settle nor acquire a tool; editing from another pane preserves the operation
  and its settings origin. Drafts retain their originating operation/session fence.
  Collapse settles field drafts without terminating the operation. See
  `tests/qt_viewport_settings_overlay_tests.cpp` and the settings-control tests.
  Each value uses one label-left, field-right row. Measure localized labels with the active font
  for a shared width, bounded so long labels wrap; size checkbox fields to their contents.
  Recompute measurements only when layout inputs change, never on unchanged refreshes. Composite
  fields keep their controls in the field column and actions align with fields. The overlay has
  no Reset Settings action or numeric limit labels.
- `main.cpp` selects Fusion; `qt_application_theme.*` supplies the dark palette and live system
  High Contrast fallback. Preserve native control behavior and accessible default, hover, selected,
  pressed, focused, inactive, and disabled states when styling.
  `qt_application_style.*` paints flat surfaces through the existing scale and local toolbar/tree
  Fusion proxies; native geometry, hit testing, text, and editor semantics stay with Qt.
  Accent uses Quader's logo purple; viewport/material selection and axis colors remain semantic.
  Property sections share `QtPanelSectionHeader` for a full-width native disclosure surface;
  `qt_settings_detail::QtSettingsSection` retains its body and settles focused drafts before collapse.
  Keep labels aligned beside native editors, with stacked components where a narrow panel would
  crowd numeric values. Collection panels retain their native tree/list interaction.
  Retained dialogs prewarm their hidden native owner after polish/layout. Prewarming stays unmapped
  and keeps the native owner across later presentations; application native-sibling policy keeps
  ordinary UI widgets non-native. See `tests/qt_retained_message_box_tests.cpp`.
- Reuse localization, settings descriptors, models, delegates, and signals. Call base event handlers
  on unhandled paths. Input follows [INPUT_KEYBINDINGS.md](../../INPUT_KEYBINDINGS.md): Qt adaptation,
  canonical keybindings, then semantic viewport frames. Do not add a parallel `QAction` shortcut map.
- `QtToolToolbar` retains activatable modeling tool launchers and the source actions used by
  viewport controls. Immediate modeling operations belong in the Q menu, not a second
  mode-specific toolbar. Consume canonical command availability and keep requirement-driven
  enabled states. Use existing authored icons and text buttons when no icon exists. Preserve
  native workspace placement and the Tools visibility command.
  Delete actions must never appear in a toolbar; keep deletion in its existing menu/input owners.
  Viewport display controls belong in each pane's strip; their retained action source is hidden
  and has no main-window toolbar or workspace visibility entry.
- `QtMarkingMenu` owns gesture presentation only: compact floating labels with transparent gaps,
  never an enclosing rectangular panel. Button widths follow their labels; a bright, fixed-color
  live line connects the cursor to the gesture origin, independent of hover state. Supplemental
  commands use the shared modeling context command policy and form a compact menu below
  the radial actions, without a More launcher. `QtViewportMarkingMenuPresenter` projects the shared
  modeling-operation catalog on opening, keeps compass slots stable even for disabled or absent
  entries so gestures remain memorable, and revalidates
  pane, session, revisions, runtime and command availability before dispatch. Its initiating
  chord comes from semantic viewport input; the popup consumes release and the surface abandons
  stale held input afterward. Do not add a private Q shortcut or copy modeling availability.
- The viewport RMB menu retains clicked-face material/texture transfers and curve Apply Solidify.
  Generic operations already reachable through the Q radial actions or supplemental list stay out
  of RMB. The shared Q command collector applies the explicit operand scope in
  `viewport_context_menu_policy` before native availability and queries canonical metadata only on
  popup opening. Activation eligibility is not relevance: component/control selections must not
  expose whole-object operations; clipboard requires supported mesh/instance targets.
  Its three face material/texture transfers
  retain the selected source/destinations separately from the clicked face and reuse their public
  modeling API routes; the nested menu's revision fence protects both targets.
- `QtStatusMessage` owns error contrast and the portable Qt system alert. Announce new failures once;
  unchanged frames, localization and runtime rebinding must not replay sounds. `UISystem` retains
  direct notifications separately from command status. The bottom bar displays only contextual
  input hints or errors/warnings, with errors and warnings taking priority. Filter both message
  sources centrally by severity: success, information, operation completion, tool activation and
  project-open confirmations must neither appear there nor suppress hints.
  Every bottom-bar error/warning stays visible for five seconds, then fades out over 200 ms using
  opacity only; contextual hints return automatically. A new diagnostic restarts the five seconds.
  Unchanged frames, locale refreshes and retained messages must not restart or resurrect it.
  Direct user actions publish failures through `editor.ui().notify`; command dispatch already owns
  its status. Tool result ports identify explicit attempts so hover/preview refreshes remain silent.
- Numeric fields use `QtSpinBox` or `QtDoubleSpinBox` and the shared
  [`QtNumericSpinBoxController`](NUMERIC_INPUTS.md). It owns hover arrows, horizontal scrubbing and
  text-entry transitions while the native spin box owns values, validation,
  accessibility and virtual stepping. The application polish filter also adapts
  Qt-created fields in dialogs and item editors. Keep gesture logic here rather
  than adding panel-specific drag handlers; tool-setting wrappers only adapt
  owner edit lifecycles. Other numeric control text uses
  `qt_format_number`. Show at most four decimal places without trailing zeroes, while retaining
  owner-defined precision and ranges. Unedited rounded text must not change the stored value.
  Face texture Scale U/V deliberately retain four decimal places and use additive 0.01 steps;
  Shift and Rotate retain compact formatting.
- Settings categories group existing descriptors by user purpose. Undo/autosave share
  `SessionPreferences`: stage changes, merge per field, persist, then publish SessionCommit.
  File > Recover > Auto Save opens the native file picker in the owner-provided autosave folder;
  use `scene_documents().recover_autosave_native_path()` and reject stale runtime bindings.
  File > Open Recent loads the opened project's five scene records on menu opening, never during
  frame refresh; its owner rejects foreign-project and stale rows before scene transitions.
  Edit > Undo History queries its ten newest retained states only on submenu opening and presents
  ordinary list actions, never checkboxes. User selection commands remain visible; internal
  selection belongs to its enclosing operation. Original appears only while its initial state is retained.
  Its snapshot fences history selection against edits, scene rebinding and document replacement;
  active drafts keep ordinary Undo/Redo ownership and prevent jumping through committed history.
  Give each group one purpose: language and interface scaling are separate groups; do not join
  unrelated controls under a combined heading merely because they share a page.
  Settings contains preferences without equivalent controls in other menus, panels, or toolbars.
  Workspace toggles for snapping, backface selection, texture locks, and display visibility belong
  in their menus/toolbars. Appearance values, picking tolerances, and gizmo size are separate
  preferences and remain in Settings when no other surface edits them.
  The Viewport page's Default Material image is a profile preference, available even
  when material authoring is excluded. Stage its PNG/JPG choice and preview locally;
  import through `DefaultMaterialTextureStore`, persist, then publish
  `DefaultMaterialCommit`. Cancel and failed persistence do not change live appearance.
  Keep the canonical scene material references untouched. See
  `tests/qt_settings_dialog_tests.cpp` and `tests/public_release_ui_tests.cpp`.

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
