<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Input and keybinding contracts

Read this guide for keyboard, mouse, navigation, shortcut, capture, or rebinding work. It maps
current owners and regression-sensitive contracts; the compiled catalog and tests define individual
bindings. Do not copy that catalog into documentation. Follow root `AGENTS.md` for verification.

## Find the owner

| Responsibility | Source |
| --- | --- |
| Physical chord types, parsing, formatting | `src/editor/input/quader_input_binding.hpp`, `src/api/quader_input_types.hpp` |
| Rules, precedence, context matching, pending sequences, persistence | `src/editor/input/quader_keybinding_service.hpp` (`QuaderKeybindingService`) |
| Action identity, scopes, alias groups, catalog metadata | `src/editor/input/quader_keybinding_target_catalog.*` |
| Public command/keybinding API and compiled registration | `src/api/quader_commands_api.*`; IDs in `src/api/quader_command_ids.hpp` |
| Canonical input to semantic viewport actions | `src/editor/input/viewport_keybinding_resolver.*` |
| Qt event conversion | `src/ui/qt_input_adapter.*` |
| Event accumulation and host frame assembly | `src/ui/qt_viewport_surface.*`, `src/ui/qt_viewport_panel.*` |
| Relative pointer accumulation and Win32 backend | `src/ui/qt_relative_pointer_input.*`, `src/platform/raw_mouse_input.*` |
| Shared press record and release suppression | `src/viewport/viewport_pointer_gesture.hpp` |
| Semantic navigation | `src/viewport/viewport_input_router.*` (`ViewportInputRouter`) |
| Tool/selection/gizmo precedence | `src/editor/viewport/viewport_interaction_controller.*` |
| Active operation hint declarations | `src/editor/metadata/operation_input_hint_catalog.*`, exposed by `QuaderToolsApi` |
| Bottom-bar keybinding presentation | `src/ui/qt_operation_input_hints.*` |

`QuaderCommandsApi::Impl::register_default_keybindings()` and
`register_viewport_default_keybindings()` are the compiled mapping entry points. Their scope metadata
comes from the target catalog and modeling/tool descriptors. Query `commands().keybinding_catalog()`
for current/default mappings, unassigned actions, source, diagnostics, and read-only `when` expressions.
Do not introduce a parallel map through private Qt shortcuts, widget key checks, parsing, or settings.

## Command and default-binding lifecycle

Treat a command addition, rename, removal, or requested default change as one change across:

1. Canonical command ID, registration, dispatch, and public API.
2. Compiled default or an explicit, tested, documented **unassigned** decision.
3. Semantic scope, `when` expression, availability, and conflict behavior.
4. Settings catalog, complete localized labels, menus, and tooltips.
5. User override persistence and compatibility.
6. Command behavior contract and focused input tests.

A removed or renamed command must leave no stale default, alias, catalog row, localization entry,
menu shortcut, or user-override target. Default changes preserve existing user overrides and deliberate
unassignments unless migration is explicitly requested. Record intentional unassigned defaults in the
owning registration/tests; do not maintain a second list here. Root `AGENTS.md` defines the visible
command behavior record and baseline requirements.

## Context and dispatch

A chord is physical input, not an action. Rules select a target through app-owned boolean contexts;
`when` supports identifiers, `!`, `&&`, `||`, and parentheses. Important context families include
`viewportFocus`, `outlinerFocus`, `editorFocus`, `viewport.hovered`, `viewport.orthographic`, `textInputActive`, `editorTextFocus`,
`navigation.*`, `activeTool.*`, `modalTool.active`, `transformDrag.active`, and `selectionMode.*`.
Read `viewport_keybinding_context()` and the registration context helpers before adding another key.

Ordinary editor panels supply `editorFocus`; the Outliner supplies `outlinerFocus` while hovered
or focused. Registered editor commands, including tool activation, plus selection-mode and grid-preset bindings
use the same compiled mappings and user overrides across these panels. A panel dispatches the public
command or typed owner intent immediately. Selection-mode changes cancel active previews through the
selection API before publishing the new mode, while
viewport pointer, navigation, and popup targets retain their semantic router. Explicit focused-control
keys such as item-view Ctrl+A, arrows, and activation retain local ownership; item-view type-ahead
search runs only when no editor command matches. Text/delegate editors retain editing input except
for document Save commands, which settle a focused main-editor field before serializing. Popups and
auxiliary tool windows retain input ownership. Idle viewport frames clear only viewport-owned
prefixes; they must preserve another editor panel's multi-key sequence.

Stable state such as selection mode, text focus, preview, or navigation belongs in context predicates.
Transient command availability such as selection counts, clipboard contents, or undo history is checked
after context matching, with fallthrough to the next eligible binding. Add state at its owning layer
and expose it once through the frame/context builder; do not patch collisions with raw-key branches.

Commands, viewport shortcuts, and semantic actions share the **stateful sequence dispatcher**. It owns
prefix state, precedence, context matching, and availability fallthrough. Pressed and wheel events must
reach it before semantic application. Stateless lookup is only for held/released probes and deferred
mouse ownership; it must not intercept a sequence's next chord. A pending prefix expires after one
second without continuation; each accepted intermediate chord refreshes that deadline. A chord at or
after expiry starts fresh. Focus loss or text input clears pending viewport input.

Tool, selection-mode, grid, and ordinary viewport command bindings normally require
`!navigation.active`; navigation rules stay eligible during navigation to resolve held/released input.
For example, `W` activates Move when idle and means Fly Forward during fly navigation. The router
consumes semantic `ViewportInputActionKind`/`ViewportNavigationInput`, not Qt keys or physical chords.

Cursor-position viewport launchers are semantic non-command targets resolved through that same
dispatcher. The viewport adapter captures the initiating pane and pointer position, while the Qt
presenter uses stack-local popup menus and revalidates the runtime, scene session, revisions, pane,
and selected command after the nested popup loop returns. Menu actions do not own private shortcuts.

Escape defaults to the shared Clear Selection command when the active tool's descriptor permits
selection fallback, from the viewport or Outliner in every selection mode. The command context uses
that capability rather than a tool-name list. Active transform drags, previews, and modal tools retain
cancellation priority; a subsequent Escape clears selection. The existing selection owner handles
object/group history and component selection. Clear Selection remains remappable through Settings.
Tool IDs used by input contexts come from the same descriptor catalog as tool activation and presentation.

## Settings and persistence

- The row key is `action_id + scope_id`, not its coarse display category. A row can represent several
  resolver targets; editing it updates all aliases. Modeling scopes come from owner availability
  metadata. Mutually exclusive semantic scopes can reuse a mapping.
- Each row has zero or one mapping, which may be a multi-chord sequence. Labels are complete,
  localized action names, not ambiguous menu leaves.
- Assignment analysis distinguishes compatible reuse, exact conflicts, and prefix conflicts.
  `Reject` rejects collisions; `Replace` unassigns colliding actions before assigning the candidate.
- Unassign removes the mapping. Reset Action restores its compiled default; Reset All persists an
  empty override set. Edits save and activate immediately, independently of Settings Apply/Cancel.
- `%LOCALAPPDATA%\Quader\keybindings.json` stores only overrides/removals. Defaults stay compiled;
  deleting the override file restores them. The API selects the path and supplies the shared atomic
  text writer to `QuaderKeybindingService`.
- Validate one complete candidate, persist it, then activate it and emit `KeybindingsCommit`.
  Flush/replacement failures preserve the previous file and live resolver/catalog.
- Persist only action groups whose effective mapping differs from defaults. Legacy target-level JSON
  remains loadable: latest assignment wins within a group, and stored `when` text is rebound to the
  current app-owned scope. Loading normalizes in memory without rewriting the legacy file.
  Retired implicit-material assignments and removals are discarded by exact target ID; other
  unknown targets remain errors, preserving strict validation of unrelated overrides.
- Retained menus/tooltips refresh on keybinding commits/revisions, outside the frame loop. They must
  not enumerate the complete catalog or repeat unchanged Qt setters every frame.
- Operation hints declare typed commands and gesture labels at the tool owner. The shared status
  widget resolves current bindings/localization only when their revisions or operation state change;
  disabled or unassigned commands have no badge. Extend the owner catalog for new operations, not
  a second shortcut map in the widget. Primitive hints show keyboard commands only, occupying the
  existing single status-bar row with compact badges; narrow widths must not create another row.
  See `tests/operation_input_hint_tests.cpp`.

## Mouse and held-key contracts

Press, Click, DoubleClick, Drag, and directional/generic Wheel are distinct mappings. The canonical
parser also accepts explicit press aliases and normalizes them to the bare button name. Qt conversion
normalizes the primary modifier, rejects unsupported keys, and ignores auto-repeat/pure modifiers for
capture. Use the shared parser/formatter for keyboard, mouse, and mixed sequences.

The viewport emits Click on release below the drag threshold and emits the navigation Drag edge on
press for immediate response. Settings capture resolves a stationary quick release as Click, movement
as Drag, a native double as DoubleClick, and a hold beyond the double-click interval as Press. Pending
click capture cannot be accepted early; later unrelated input flushes it before sequence continuation.

Freeze the originating button, sequence, position, modifiers, threshold, and deadline in
`ViewportPointerGesture`; keep domain identities beside it. A later modifier change cannot transfer
ownership or alter a deferred action. Match releases to the accepted press; an older/unrelated release
cannot commit a newer gesture. Cancellation reserves the originating release until the button is up,
so it cannot become a selection click, context menu, navigation start, or another tool's input.

Keep drag-start chords immutable: pressing Shift after RMB fly navigation starts must not end it.
An RMB gesture that moves the fly camera, changes fly speed, or exceeds its frozen drag threshold
retires its context-menu click until release. Stopping movement or returning to the press position
does not restore that click; a fresh stationary RMB gesture can open the menu.
Freehand selection consumes ordered, gesture-scoped pointer samples, including press and release;
a closed path collected between frames must not collapse to the final cursor position. Clear the
sample stream on capture/focus loss, runtime rebinding, and coordinate-system changes.
Focus/capture loss supplies `pointer_interaction_canceled`, even when Qt synthesizes a release.
`ViewportInteractionController::handle_tool_pointer_lifecycle()` preserves Curve-control prepass,
semantic conversion edge order, native tool lifecycle, then ordinary transform-gizmo precedence.
Qt submits the frame and presents tool cursor intent; it does not enumerate tools or reconstruct this
precedence. Interaction hit tests share the frame's frozen `SceneSurfaceQuerySnapshot`.

Held actions use `active_input_chords`, not remembered press edges. In particular,
`viewport.transform.vertex_snap` produces `transform_vertex_snap_down` while its keyboard chord is
physically held and the Move/Curve/transform context applies. A press released before sampling is not
held; release, focus loss, or context loss clears it. This action accepts exactly one keyboard press
chord; invalid persisted sequences or mouse mappings fall back to its compiled default.

Some actions must reserve constituent presses before their Click/DoubleClick resolves. Preserve the
material-transfer, Curve-conversion, and orthographic Mesh-depth-selection tests when modifying that
path. Their reserved gestures must not mutate selection or start a gizmo first. Delayed selection
freezes the ray and first-click modifiers, cancels on context loss, and commits a lone click once at
its deadline. Rebinding/unassigning must remove the corresponding reservation/timing behavior.

Face material copies share texture continuation's source-preserving pointer ownership. A selected
source must be exactly one face; sampling into the selection can target faces across meshes.
Copying a material retains target UVs and texture settings. Face-only copying applies on the first
Click release, without waiting for the double-click interval. A subsequent whole-mesh double click
applies the material to the rest of the mesh through its own normal history operation. Selection,
scene, or binding changes before release cancel the reserved gesture.

## Windows wheel and relative pointer input

Qt on Windows represents Alt+vertical-wheel as horizontal-only `angleDelta`. The adapter restores the
canonical vertical direction before classification. Test the Windows-shaped X delta through both
`QtViewportSurface` and `QtViewportPanel`; an idealized Y delta alone misses this failure.

Accumulate angle input per viewport in native 120-unit detents. Preserve incomplete motion, but drop
the old remainder on axis change or direction reversal. Emit every completed detent as an ordered
event with its event-time local cursor position. Opposite events in one frame cannot collapse to a net
scalar: zoom limits are evaluated in order. An accepted wheel event survives a later hover leave.
Camera zoom and Fly speed consume that same ordered stream. `pixelDelta` follows its own calibrated
continuous path; never divide it by 120 or merge it with angle accumulation. Relative/reset grid
bindings resolve to semantic `SetGridPreset` shortcuts using the synchronized current preset.

Captured fly navigation uses unbounded relative motion. Classify each Win32 raw packet before its
legacy Qt move: relative packets add raw deltas and suppress duplicates; absolute-device packets use
translated Qt motion. Preserve genuine raw and fallback deltas together and drain them once per frame.
Failed raw registration falls back to Qt while retaining capture-loss monitoring. Ignore cursor-warps
before fallback accumulation. Verify capture enable/disable against actual native postconditions.
Focus loss or capture theft ends fly navigation, clears motion, restores the pre-capture cursor, and
cancels the RMB chord without synthesizing a click. Win32 messages stay at the UI/platform boundary.
The navigation release frame retains picking suppression while its sample still contains the
captured pointer position. Hover and selection resume from the restored real cursor on the next
frame; the navigation crosshair must never publish a scene hover candidate.

## Verification map

Use synthetic tests for parser/formatter behavior, persistence/reload and failed writes, exact/prefix
conflicts, context reuse, sequence expiry, navigation suppression, immutable press ownership,
cancellation, preview lifecycle, and undo/redo as applicable. Relevant owners:

| Behavior | Tests |
| --- | --- |
| Catalog, defaults, dispatch, overrides, scopes | `tests/editor_api_tests.cpp` (`EditorApiTests`) |
| Settings capture, conflicts, live persistence | `tests/qt_settings_dialog_tests.cpp` |
| Qt translation, actual event shapes, held keys, surface/panel routing | `tests/qt_input_adapter_tests.cpp` |
| Raw/fallback motion, warps, capture postconditions | `tests/qt_relative_pointer_input_tests.cpp` |
| Semantic routing, deferred gestures, tool/gizmo precedence | `tests/viewport_interaction_controller_tests.cpp`, `tests/curve_tool_service_tests.cpp` |
| Boundary and UI-policy protection | `tests/programmatic_api_guardrail_tests.cpp`, `tests/ui_layout_guardrail_tests.cpp` |

Choose one risk-selected verification path under root [AGENTS.md](AGENTS.md), including the relevant
changed suites once. Use real-window smoke checks for native capture, window, or GPU behavior that
headless tests cannot establish.
