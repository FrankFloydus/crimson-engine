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
| Scoped dialog Apply/Cancel routing | `src/ui/qt_apply_cancel_input_filter.*`, using `QuaderCommandsApi` lifecycle resolution |
| Event accumulation and host frame assembly | `src/ui/qt_viewport_surface.*`, `src/ui/qt_viewport_panel.*` |
| Relative pointer accumulation and Win32 backend | `src/ui/qt_relative_pointer_input.*`, `src/platform/raw_mouse_input.*` |
| Shared press record and release suppression | `src/viewport/viewport_pointer_gesture.hpp` |
| Semantic navigation | `src/viewport/viewport_input_router.*` (`ViewportInputRouter`) |
| Tool/selection/gizmo precedence | `src/editor/viewport/viewport_interaction_controller.*` |
| Active operation hint declarations | `src/editor/metadata/operation_input_hint_catalog.*`, exposed by `QuaderToolsApi` |
| Transform, array and curve hint phases | `src/editor/metadata/transform_operation_input_hints.*` |
| Receiving local-control hint source | `src/ui/qt_contextual_input_hints.*`, registered by each control owner |
| Bottom-bar keybinding presentation | `src/ui/qt_operation_input_hints.*` |

`QuaderCommandsApi::Impl::register_default_keybindings()` and
`register_viewport_default_keybindings()` are the compiled mapping entry points. Their scope metadata
comes from the target catalog and modeling/tool descriptors. Query `commands().keybinding_catalog()`
for current/default mappings, unassigned actions, source, diagnostics, and read-only `when` expressions.
Do not introduce a parallel map through private Qt shortcuts, widget key checks, parsing, or settings.

Orbit, snap-orbit, and Pan retain their compiled mouse gestures. The keybinding service locks
these targets; the public catalog marks them noneditable, and Settings omits their rows. Neither
assignment, unassignment, replacement nor a legacy file can override them. Their physical
button/modifier combinations are reserved across press, click, double-click and drag, including
any position in a multi-key sequence, when the action's context overlaps viewport navigation.
Fly Navigation and its controls remain editable: the reservation excludes active fly navigation,
where the same middle button can reset fly speed. Loading ignores only the legacy fixed-target
or reserved-gesture overrides and keeps unrelated custom bindings.

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

A chord is physical input, not an action. Windows event conversion uses native virtual-key
identity rather than the produced text. Letter
tokens retain the native named letter on AZERTY/QWERTZ and non-Latin layouts; digit-row, keypad,
function, editing and canonical punctuation tokens likewise do not depend on shifted glyphs.
Punctuation names describe the canonical native key slots, and `IntlBackslash` identifies the
separate ISO key. Shortcut recording and execution use the same converter, so capture an input
when the printed national key legend differs from its canonical token. Existing binding strings,
overrides and unassignments retain their identities. Text controls keep their native text input.
AltGr's explicit Qt `GroupSwitchModifier` has no canonical shortcut representation and must not
be dropped into an unmodified keyboard, mouse or wheel chord. Ordinary Ctrl+Alt stays Ctrl+Alt.
Held/consumed event tracking uses native scan identity (virtual-key or Qt identity fallback),
retaining the accepted press chord until release even if modifiers or the keyboard layout change.
All canonical press, shortcut-reservation, recorder and release paths use `qt_input_adapter.*`;
intrinsic Qt editing and navigation checks remain with their controls.
Rules select a target through app-owned boolean contexts;
`when` supports identifiers, `!`, `&&`, `||`, and parentheses. Important context families include
`viewportFocus`, `outlinerFocus`, `editorFocus`, `assetBrowserFocus`, `viewport.hovered`, `viewport.orthographic`, `textInputActive`, `editorTextFocus`,
`navigation.*`, `activeTool.*`, `modalTool.active`, `transformDrag.active`, `editorMode.*`, and `selectionMode.*`.
Read `viewport_keybinding_context()` and the registration context helpers before adding another key.

Ordinary editor panels supply `editorFocus`; the Outliner supplies `outlinerFocus` while hovered
or focused. Registered editor commands, including tool activation, plus selection-mode and grid-preset bindings
use the same compiled mappings and user overrides across these panels. A panel dispatches the public
command or typed owner intent immediately. Selection-mode changes and selection edits that leave the
current operation accept valid pending work through its existing owner before publishing the new
selection state. Compatible in-tool selection edits retain the owner's live-preview behavior, while
viewport pointer, navigation, and popup targets retain their semantic router. Explicit focused-control
keys such as item-view Ctrl+A, arrows, and activation retain local ownership; item-view type-ahead
search runs only when no editor command matches. Text/delegate editors retain editing input except
for the Asset Browser command and document Save commands, which settle a focused main-editor field
before serializing. Popups and
auxiliary tool windows retain input ownership. Idle viewport frames clear only viewport-owned
prefixes; they must preserve another editor panel's multi-key sequence.

The Asset Browser uses the canonical `ui.asset_browser` command (Ctrl+Space by default), including
from main-editor text fields and its search and rename editors. Canonical browser shortcuts are
reserved before Qt shortcut activation; foreign top-level windows retain input ownership.
A focused widget below `quaderAssetBrowserInput` supplies
`assetBrowserFocus`: Cut, Copy, Paste, Duplicate and Delete retain their canonical mappings and
user overrides, but dispatch to the browser's asset owner independently of scene selection modes,
tools and clipboard availability. Project/scene document lifecycle and command search remain available;
scene object, tool and selection shortcuts are excluded before sequence resolution.
Native text editing, item selection and navigation keep their ownership; browser toggling remains
available from those controls. Browser prefixes have a distinct owner and are probed without mutation
before Qt shortcut activation. See `tests/qt_command_event_filter_tests.cpp` and `tests/editor_api_tests.cpp`.

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

The marking-menu launcher's accepted keyboard chord has a tap/hold gesture: release before
opening activates Select from another tool, or cycles Select → Lasso Select → Paint Select →
Select when a selection tool is active. Every transition uses the ordinary tool lifecycle;
a 200 ms hold opens the menu without cycling on release. Capture the initiating pane and pointer
origin once; auto-repeat never restarts the hold.
Focus/context loss, runtime replacement, navigation or another input abandons a pending hold
without selecting or opening. Remapping `viewport.marking_menu` preserves both gestures;
separately assigned Select shortcuts keep their canonical command behavior.

`Z` defaults to the canonical `viewport.shading.toggle_wireframe_shaded` command. It toggles the
focused pane's active perspective/orthographic shading slot: Wireframe becomes Shaded, and every
other mode becomes Wireframe. The pane strip's Shading menu retains its existing commands. The
former `viewport.shading_popup` target and cursor-position Shading popup are retired.
The command requires viewport focus outside text editing and navigation; active tools' Z-axis
constraints retain their contextual precedence.

Escape defaults to the shared Clear Selection command when the active tool's descriptor permits
selection fallback, from the viewport or Outliner in every selection mode. The command context uses
that capability rather than a tool-name list. Active transform drags, previews, and modal tools retain
cancellation priority; a subsequent Escape clears selection. The existing selection owner handles
object/group history and component selection. Clear Selection remains remappable through Settings.
Tool IDs used by input contexts come from the same descriptor catalog as tool activation and presentation.
Primitive activation uses the dedicated Create Box, Cylinder and Sphere commands with
Shift+B, Shift+C and Shift+S defaults. Cone and Torus are deliberately unassigned where
their release profile exposes them. The old `tools.activate.create_primitive` target is a
presentation-hidden alias of Create Box in the same keybinding action group; preserve its
stored assignments, removals and sequences. Both alias and canonical command dispatch to the
shape-specific owner route, including when a different primitive is already active.
`B` invokes the immediate Bridge action in Edge/Face mode, using one straight span regardless of
remembered Bridge preview steps. The edge/face menus expose the Bridge preview;
its remembered settings stay independent. Canonical quick-Bridge aliases share the remappable binding.
Preview command renames migrate stored assignments and removals at the keybinding-file boundary,
preserving sequences and declaration-order precedence without retaining runtime command aliases.
Tool Settings Apply/Cancel uses the same canonical lifecycle bindings from the viewport
and from every settings control: Space applies, Escape cancels by default. Focus on a
button must not turn Space into a different action, including activating Cancel.
An active numeric text edit consumes Escape first, restoring its starting value
and exiting text entry without canceling the enclosing tool or dialog. Its matching
repeats and release stay consumed; the next Escape follows the canonical lifecycle binding.
Apply settles the focused numeric draft before committing; Cancel discards it before
focus loss can publish it. Keep user remaps, deliberate unassignments, sequence ownership,
and popup input ownership intact. Test physical key press/release through the production
viewport and settings adapters, not only the button's API call.
Explicit operation completion resolves through canonical Apply/Cancel commands and preserves their
remapping and deliberate unassignment. Implicit tool, selection, save, and scene transitions call
the same acceptance owners directly; they do not synthesize an Apply key or fall back to Cancel.
A meaningful draft that cannot be accepted rejects the transition without changing the draft,
selection, tool, or pane ownership. An untouched starter exits without creating geometry or history.
Repeated activation of the same tool in the same pane preserves its operation; pane focus alone
does not accept or cancel it. Undo accepts valid pending work before undoing it so Redo can restore
that result; native tool-local history, including primitive creation, retains its own semantics.
Preferences and the floating material usage filter resolve their Apply/Cancel controls
through the same canonical bindings. Free-text fields, shortcut capture, and nested
dialogs keep their input; the scoped dialog adapter owns its multi-key prefixes while
the global editor adapter yields. Preferences Apply keeps the dialog open, matching
its button, and the embedded material filter retains its immediate checkbox behavior.
The persistent Extrude tool reserves Escape even while idle: it exits the tool and
keeps accepted geometry and selected caps. E captures preselected faces in a pending
extrusion at the last accepted Amount (initially zero); when activated without faces, the first face selection starts
that pending operation. Its source selection stays locked while editing that operation; a selection
or tool transition accepts it before changing the source.
Escape or the settings Cancel button restores its source without recording
history. Releasing a viewport drag leaves the extrusion pending and enables Apply;
Apply accepts it once and retains Amount without starting another preview. A new pointer drag
starts from zero displacement. During an extrusion drag, Escape cancels that operation and keeps
Extrude active; a subsequent Escape exits the tool. Ctrl+E starts an extrusion in
Individual mode and Alt+E starts one
Along Normals; all three bindings are canonical and remappable.
The yellow disk stays visible during dragging. The disk, selected-face dragging and
Amount edit only the pending operation. After Apply, the next amount edit or drag
starts a new operation from the selected caps; accepted geometry is never adjusted.
Repeated E retains the current pending operation instead of accepting it implicitly.
Object/Edit Mode is a scene-owned context separate from selection kind. The remappable Tab
command toggles it from a focused viewport; panel/text-field Tab traversal and Ctrl+Tab scene
switching retain their owners. Vertex/Edge/Face selection commands enter Edit Mode through the
mode owner when needed, including with no selected mesh, then select the requested component kind.
Object selection returns to Object Mode through the same owner; direct Group selection retains
its Object Mode requirement.
`select.toggle_group_selection` is the canonical Object Mode toggle, default Shift+G.
It switches the existing selection owner between Object and Group: enabled uses the current
group picking and hierarchy promotion; disabled restores ordinary object selection. The toolbar
always shows Vertex/Edge/Face/Object buttons and an independent checkable Groups icon, with no
Object/Edit dropdown. The compiled selection defaults are 1/2/3/4 respectively. Exiting Edit Mode
restores Object selection, so Groups is unchecked. Explicit Object/Group semantic selection
targets remain remappable; Group has no compiled 5 default. Preserve stored user overrides.
When Object selection is empty, explicit Tab/API Edit entry may use the retained active mesh if it is eligible;
selected objects take precedence, and a retained group does not expand. No eligible target leaves
the mode unchanged without settling pending work or showing a disabled-command error.
Direct component choices still enter an empty Edit context when that fallback has no target.
Component hover and selection can target eligible scene meshes independently of prior object
selection; committed selection updates the mode participants, while hover changes presentation only.
The retained candidate survives empty space and viewport leave. Blank clicks and completed regions
clear immediate highlighting; gizmo ownership also clears the candidate, preserving selected components.
Insert Edge Loop and Knife activation require Edit Mode and enter Edge selection, including through
Shift+R and C respectively. Their surface acquisition and commit validation allow eligible peer meshes.
Insert Edge Loop reserves canonical Cancel while active, including idle and hover-only states.
Its preview and insertion move continuously by default. Hold Ctrl to snap the loop to the
current grid size, independently of the global snapping toggles; releasing Ctrl restores
continuous positioning. The contextual hint advertises Grid Snap and highlights the held gesture.
Escape exits to Select, discards transient loop previews, and keeps accepted loops and their history.
Cut point and line dragging is continuous by default. Hold Ctrl to snap to the current
grid step, independently of global snapping, and Shift to straighten the point or line
translation along the retained support plane. The line shows a hand cursor and preserves
the cursor's initial offset while dragging; neither endpoint is re-picked onto another surface.
Dragging follows the visible gizmo axis in every extrusion mode. When the axis points
directly into the view and has no visible direction, dragging upward increases Amount.
Extrusion dragging is unsnapped by default. Holding Ctrl snaps the total extrusion
amount to the current grid size, including an existing result's starting amount;
pressing Ctrl can therefore move an off-grid amount to its nearest grid multiple.
Shift reduces subsequent motion to one tenth. Vertex snapping requires its explicit
hold binding. The release event's modifiers and held snap binding determine the final preview value. Modified
face clicks cannot alter a pending extrusion's source; the disk accepts Ctrl and Shift when pressed.

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
- New compiled defaults yield to eligible stored user mappings, including a stored multi-chord
  prefix. Immediate global-command probes honor the winning viewport mapping before choosing an
  execution adapter; they cannot preempt a user assignment by filtering its owner out first.
- Persist only action groups whose effective mapping differs from defaults. Legacy target-level JSON
  remains loadable: latest assignment wins within a group, and stored `when` text is rebound to the
  current app-owned scope. Loading normalizes in memory without rewriting the legacy file.
  Retired implicit-material and `viewport.shading_popup` assignments/removals are discarded by
  exact target ID; other unknown targets remain errors, preserving strict validation of unrelated overrides.
- Retained menus/tooltips refresh on keybinding commits/revisions, outside the frame loop. They must
  not enumerate the complete catalog or repeat unchanged Qt setters every frame.
- Release-disabled targets remain registered and their overrides remain persisted, but are absent
  from public catalogs, conflict presentation and input eligibility. Resolve release availability
  before sequence prefixes or deferred pointer reservations; resetting available shortcuts keeps
  dormant overrides for future cumulative releases.

## Contextual input hints

The bottom bar describes the input owner receiving the current interaction. It is a presentation
of semantic state, never a second dispatcher or a command catalog. Project viewport phases through
`QuaderOperationInputContext` and the authoritative tool/session owners; do not reconstruct hit tests,
selection, geometry, drag state, or tool eligibility in the status widget.
Only errors and warnings may replace contextual hints. Success, informational and operation-complete
messages must not appear in the bottom bar or suppress its hints; the UI severity filter owns this
policy for command status and direct notifications alike.
All bottom-bar errors/warnings yield back to hints after five seconds plus a 200 ms opacity fade.
Only a new diagnostic restarts that interval; frame and localization refreshes do not.

- Idle perspective navigation shows only Orbit, Pan and Fly Navigation, using their current
  bindings. Fly navigation replaces these with movement, Fly Speed and Reset Fly Speed. Respect
  orthographic capabilities. Never add an Escape/End Navigation hint; navigation input behavior
  remains with its existing owner.
- Tool hints reflect the current phase and hovered or captured handle. Offer Apply only when the
  owner can accept that phase, and name cancellation for the action it actually cancels. Show
  modifier, axis and snap hints only where that interaction supports them; distinguish enabling
  from disabling snap and retain the actual operation once a drag has begun. Do not append idle
  navigation, Select, Focus or unrelated commands to every active tool.
- Numeric editors, local panels and dialogs publish their own hints through
  `qt_register_contextual_input_hints`. Capture and active editing take precedence over hover.
  An empty local snapshot still suppresses viewport hints; popup, modal, text-input and focus-loss
  ownership must not expose stale viewport instructions. Hint registration does not consume or
  remap input. Destroying a source invalidates its retained presentation safely.
- Remappable actions declare typed command IDs or canonical resolver targets, including related
  targets for movement/axis groups. Resolve badges from current bindings; disabled, unavailable or
  deliberately unassigned actions have no badge. Intrinsic control gestures remain with their
  control owner. Do not hard-code WASD or other defaults into the presenter.
- Gate hint construction, binding lookup, localization and retained Qt updates by source identity,
  semantic context/owner revision, keybinding revision and locale revision. Cheap observers must
  not allocate snapshots or scan every registered control. Unchanged frames do no presentation work.
- Keep the existing single status-bar row. At narrow widths retain higher-priority completion and
  cancellation before optional alternatives; do not wrap to another row.

Extend the existing owner catalog or local provider for new contexts. See
`tests/operation_input_hint_tests.cpp` for phase, remapping and unchanged-frame contracts.

## Mouse and held-key contracts

Press, Click, DoubleClick, Drag, and directional/generic Wheel are distinct mappings. The canonical
parser also accepts explicit press aliases and normalizes them to the bare button name. Qt conversion
normalizes the primary modifier, rejects unsupported keys, and ignores auto-repeat/pure modifiers for
capture. Use the shared parser/formatter for keyboard, mouse, and mixed sequences.

Keyboard nudges are the explicit repeatable command family. Arrow defaults move the selection;
Alt+arrows rotate it. Shift+arrows and Shift+Alt+arrows duplicate objects/groups or extrude
components before the step. Qt retains the original physical press chord and queues native repeat
edges with their event-time Shift state and position among true presses. Keep repeats out of
`pressed_input_chords`: SDK consumers,
ordinary commands and marking-menu holds still receive one initiating press.

The viewport panel retains a repeat token only after a changed, successful nudge. The command owner
revalidates its completed sequence, binding revision, pane, context and current availability without
advancing the sequence dispatcher. Prefixes and other commands never become repeatable. Release,
focus/text/navigation ownership changes, runtime rebinding and remapping retire accepted tokens.
For live Shift changes, resolve a copy of the complete accepted sequence with only its terminal
Shift modifier changed. Accept only the corresponding normal/duplicate-or-extrude action for
that same tool and direction. Missing, deliberately unassigned or conflicting variants skip the
repeat without dispatching another command or consuming a prefix; the original valid hold remains.
Process queued repeats before release cleanup, including a complete tap/repeat/release between frames;
the final held-key set cannot discard those already received events. A navigation press in the same
batch blocks later repeats immediately through its semantic action flags. Each accepted invocation
has one undo entry and preserves the selected tool. Pending edits are rejected, never settled by a nudge.
See `tests/qt_input_adapter_tests.cpp` and `tests/editor_api_tests.cpp` for these contracts.

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
The pane's remembered Box/Lasso/Paint choice also governs selection fallback while Move,
Rotate, Scale, or another selection-capable tool remains active. Resolve that choice through
the viewport workspace owner; do not infer Box from a non-selection active tool. Preserve the
active tool's gesture and transform-handle precedence at press. Once a selection gesture owns
the press, its matching release cannot become a transform or tool gesture. Lasso fallback
retains its partial-intersection setting independently of the executing tool's settings.

Ordinary component double-click dispatch is kind-specific: Edge selects the clicked loop,
Face uses the prior active selected face to resolve a unique quad strip or falls back to
connected faces, and Vertex selects connected vertices. Shift adds and Ctrl removes, with
Ctrl taking precedence; unmodified input replaces. Modified Edge double-click bounds a
valid loop using the pre-first-click selection and clicked edge when those seeds are separated;
an already contiguous seed chain expands to the full loop. Adjacent Face anchors select the
whole strip; separated anchors bound an interval. Closed intervals minimize element count,
with a stable tie rule, rather than searching arbitrary mesh paths. Committed selections update
the participating meshes through the ordinary selection publication owner.
Capture the anchor and selection before the first click publishes, then resolve one logical
double-click result against that context. Fence it by scene, Edit generation, selection kind,
gesture and intervening selection changes; cancellation must retain an already published
ordinary single click. An Alt double-click must not reapply the first click's traversal toggle.

In Edit Mode, Alt+LMB requests one loop traversal from the actual clicked component:
Vertex, Edge, or Face. Ctrl+Alt requests an edge ring in Vertex/Edge mode; Face mode
keeps the quad face band. Shift toggles the whole result according to the clicked
component's selectedness. For Vertex/Face direction, use an adjacent, still-selected
active component only on the same globally active object and in the same mode;
otherwise choose the pointer-nearest incident edge with a stable tie fallback.
This one-shot gesture overrides the remembered Box/Lasso/Paint method without changing
it. Navigation, mapped commands, active-tool gestures and transform handles retain their
press precedence. Preserve the physical press chord through release and gizmo handoff;
traversal misses must not clear selection or settle a pending operation.
Paint Select uses those ordered samples with their event-time modifiers. Its press owns the stroke;
unmodified input replaces once, then accumulates, Shift adds and Ctrl removes (including when both
are held). Releasing a modifier never clears the stroke again. Interpolate pointer segments through
the existing point-picking policy, stop at the matching release, and ignore later coalesced motion.
Painting can enroll eligible peer meshes. Explicit Paint activation bypasses transform handles;
Paint fallback honors the active tool's press precedence before capture. Both bypass click-only
traversal once a paint stroke begins; an initial Alt traversal press remains a one-shot
selection gesture. Scene, mode, tool, selection-kind and camera changes retire its capture. An interruption retains the
already published selection without sampling the interruption coordinates. Object selection edits
merge into one history entry per stroke; component selection keeps its existing history policy.
Paint samples publish selection immediately but defer the dependent tool's selection-completion
callback until release or interruption. An idle Extrude must not begin or settle a preview between
samples. Keep this completion boundary in the existing selection owner without staging a second
selection state.
Ctrl+Q activates Paint Select by default and remains remappable; existing user overrides retain precedence.
Focus/capture loss supplies `pointer_interaction_canceled`, even when Qt synthesizes a release.
That transport signal is not explicit Cancel: gesture owners retain or accept their last valid
sample on interruption without applying its pointer coordinates. Incomplete primitive footprints
remain pending for retry or explicit Cancel. Outside panel clicks do not cancel a modeling draft;
the requested tool/selection/document command owns any required acceptance.
Merely focusing another pane while the pointer remains held preserves the pending gesture.
`ViewportInteractionController::handle_tool_pointer_lifecycle()` preserves Curve-control prepass,
semantic conversion edge order, native tool lifecycle, then ordinary transform-gizmo precedence.
Qt submits the frame and presents tool cursor intent; it does not enumerate tools or reconstruct this
precedence. Interaction hit tests share the frame's frozen `SceneSurfaceQuerySnapshot`.

Held actions use `active_input_chords`, not remembered press edges. In particular,
`viewport.transform.vertex_snap` produces `transform_vertex_snap_down` while its keyboard chord is
physically held and the Move/Curve/Extrude/transform context applies. A press released before sampling is not
held; release, focus loss, or context loss clears it. This action accepts exactly one keyboard press
chord; invalid persisted sequences or mouse mappings fall back to its compiled default.
Mouse-release edges also retain their event-time held keyboard chords. The canonical
resolver derives the edge's vertex-snap state before retiring suppressed key releases;
Extrude retains that edge's final preview even when snap-key release coalesces into the same frame.

Some actions must reserve constituent presses before their Click/DoubleClick resolves. Preserve the
material-transfer, Curve-conversion, and orthographic Mesh-depth-selection tests when modifying that
path. Their reserved presses must not start a gizmo first. Ordinary orthographic selection and
deselection publish immediately on Click release, without waiting for a possible depth double-click.
Retain the pre-first-click selection and frozen modifiers for a recognized depth double-click;
its correction coalesces with the first click only while the gesture context and history head still
match. Context loss cancels an unreleased gesture but never reverts an already published single
click. Rebinding/unassigning removes the corresponding reservation and depth-double behavior.

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
continuous path; never divide it by 120 or merge it with angle accumulation. Relative grid
bindings resolve to semantic `SetGridPreset` shortcuts using the synchronized current preset.

Camera depth samples actual surfaces within the view's clip range, only at navigation gestures and
wheel events. Ordinary zoom prefers the view center, then the event's cursor surface if the center
is empty; cursor zoom uses the reverse priority. Each event retains its own surface hint.
Empty-space drags may use the center surface, while absent surfaces retain the current pivot.
Whole-scene bounds are not evidence of navigation depth. Wheel input can recover a severely stale
distance in either direction; an orbit press must not inflate its radius to a farther background hit.

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
Navigation suspends tool picking without clearing the last world-space preview or rolling back
its authored values. Retain that preview through the captured release frame, including Curve
placement ghosts and Knife/Loop hover feedback. A pointer subgesture may retire at its last valid
value when its screen-space basis changes; navigation alone must not Apply/Cancel the pending tool
operation. Explicit cancellation, tool changes and matching editing-button releases retain their
own lifecycle rules.

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
