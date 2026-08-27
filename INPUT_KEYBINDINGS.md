<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Input, Keybindings, And Contexts

This guide is the source-of-truth map for Quader input, shortcuts, and user keybinding work.
Read it before changing keyboard input, mouse input, viewport navigation, command shortcuts,
rebinding UI, or keybinding persistence.

## Architecture

Quader input is split into four layers:

- `src/editor/input/quader_input_binding.hpp` owns canonical physical input values: keyboard keys,
  mouse buttons, drag gestures, wheel gestures, modifiers, shortcut parsing, and shortcut
  formatting.
- `src/editor/input/quader_keybinding_service.hpp` owns keybinding rules: target id, shortcut
  sequence, `when` expression, source, weight, removals, conflict lookup, pending chord state, and
  user JSON persistence.
- `src/api/QuaderCommandsApi.*` owns command ids, default keybindings, canonical command aliases,
  viewport shortcut/action mapping, keybinding storage path selection, and public API access.
- `src/ui/qt_input_adapter.*` translates Qt key, mouse, wheel, modifier, focus, and text-capture
  events into canonical Quader input; `src/ui/qt_relative_pointer_input.*` accumulates captured
  relative motion from the platform backend or portable Qt fallback;
  `src/platform/raw_mouse_input.*` owns Win32 raw mouse registration and message decoding;
  `src/ui/qt_viewport_panel.*` assembles a `ViewportInputFrame`; and
  `src/viewport/viewport_input_router.*` consumes semantic navigation input and resolved viewport
  shortcuts.

Do not bypass these owners. If a missing capability is needed, add it to the owner first and then
route callers through that owner.

## Core Model

A physical input chord is not a command. A keybinding rule maps physical input to a target only when
its context matches:

```json
{ "command": "tools.activate.move", "key": "W", "when": "viewportFocus && !navigation.active && !textInputActive" }
{ "command": "viewport.navigation.fly_forward", "key": "W", "when": "viewportFocus && navigation.flyActive" }
```

The same key can and should mean different things in different contexts. Do not fix collisions by
adding ad hoc `if (key == W)` branches in UI or viewport code. Add or refine context keys and
`when` clauses.

Current supported `when` syntax is boolean-oriented: identifiers, `!`, `&&`, `||`, and parentheses.
Use simple context keys until a richer expression parser is needed.

## Context Keys

Context keys describe current editor state. They are the gate that lets one shortcut work differently
in viewport navigation, text editing, modal tools, selection modes, and panels.

Existing important keys include:

- `viewportFocus`
- `viewport.hovered`
- `textInputActive`
- `editorTextFocus`
- `navigation.active`
- `navigation.flyActive`
- `navigation.orbitActive`
- `navigation.panActive`
- `modalTool.active`
- `activeTool.ownsViewport`
- `activeTool.<tool-id>`
- `selectionMode.<mode-id>`
- `canUndo`
- `canRedo`

Stable editor modes such as `selectionMode.*`, navigation capture, operation previews, and active
modal tools belong in keybinding `when` expressions. Transient command availability such as undo
history, selected-element counts, required active elements, or clipboard contents does not. The
command dispatcher checks that availability after context matching and falls through to the next
eligible binding when a higher-precedence target is currently disabled.

Future context keys should be added by the system that owns the state. For example, viewport
navigation state belongs in `ViewportInputRouter` / `ViewportHost`, panel focus belongs in the UI
shell, and selection state belongs in editor/modeling selection owners.

When adding a context key:

- define the owner of the state;
- set it once in the input frame or API context builder;
- use it in default keybinding `when` clauses;
- add regression tests showing same-key/different-context behavior.

## Mouse Input Is First Class

Quader is a modeling app, so mouse input is part of the keybinding system. Do not treat keyboard
shortcuts as the only customizable input.

Canonical input supports:

- keyboard keys and modifiers;
- mouse-button presses such as `LeftMouse`, `MiddleMouse`, `RightMouse`, `Mouse4`, and `Mouse5`;
- stationary clicks such as `MiddleMouseClick` and double-clicks such as
  `MiddleMouseDoubleClick`;
- drag gestures such as `MiddleMouseDrag` and `RightMouseDrag`;
- wheel gestures such as `Wheel`, `WheelUp`, and `WheelDown`.

Press, Click, and Drag are distinct physical mappings. The viewport emits a Click on button release
only when pointer movement stayed below Qt's drag threshold; it continues to emit the existing Drag
edge on press so navigation begins without latency. Settings capture records a quick stationary
release as Click, thresholded movement as Drag, a native double-click as DoubleClick, and a
press-and-hold beyond Qt's double-click interval as Press. A quick click remains pending during
the double-click interval: acceptance stays disabled until it resolves, and a later unrelated
input flushes the click first so multi-chord ordering stays stable.

For active mouse drags, keep the drag-start chord stable. Do not recompute drag modifiers every
frame. Example: `RightMouseDrag` fly navigation must continue if the user presses Shift after the
drag has started.

Captured right-mouse fly navigation uses unbounded relative motion on Windows. The native-event
boundary classifies each raw packet before its corresponding legacy Qt move: relative packets add
raw deltas and suppress the following duplicate Qt move, while absolute-device packets accept the
translated Qt delta. Genuine raw and Qt fallback motion are retained together and drained exactly
once by the next captured viewport input frame. Failed raw registration also uses the Qt fallback,
but keeps capture-loss monitoring active. Cursor-warp events must be discarded before fallback
accumulation. Capture enable/disable is verified against the actual native postcondition; focus loss
or external capture theft releases fly navigation, clears pending motion, and restores the
pre-capture cursor position. External capture theft cancels the drag chord without synthesizing an
RMB click. Never expose Win32 messages or duplicate relative-pointer state below the UI/platform
boundary.

## Viewport Navigation Rules

Viewport navigation has semantic actions:

- `viewport.navigation.orbit`
- `viewport.navigation.orbit_snap`
- `viewport.navigation.pan`
- `viewport.navigation.fly`
- `viewport.navigation.zoom`
- `viewport.navigation.zoom_to_cursor`
- `viewport.navigation.reset_fly_speed`

These map to `ViewportInputActionKind` and `ViewportNavigationInput`. The router should react to
semantic navigation input, not raw Qt keys or hardcoded button combinations, except for legacy
fallback tests where the keybinding resolver is explicitly inactive.

Default tool, command, selection-mode, and grid shortcuts should normally require
`!navigation.active`. Navigation bindings themselves must remain active while navigation is active
so held drags and releases can be resolved.

## Adding Or Changing A Binding

Use this checklist:

1. Choose a stable target id.
   Commands use `QuaderCommand` ids where possible. Viewport actions use `viewport.*`. Tool
   activations use `tools.activate.<tool-id>`.
2. Register or expose the command/action in `QuaderCommandsApi`.
3. Add the default keybinding with a precise `when` clause.
4. If this is viewport navigation, map the target id to `ViewportInputActionKind`.
5. If an existing context is not enough, add the narrowest new context key at the owning layer.
6. Add tests for parsing, dispatch, context separation, and runtime behavior.

## User Custom Keybind System

Current user overrides are stored at:

```text
%LOCALAPPDATA%\Quader\keybindings.json
```

That file should contain only user overrides/removals. Defaults are compiled into the app. Deleting
the file resets all custom keybindings to defaults.

Settings -> Keybindings is a live editor built on these API concepts:

- User-facing rows have a stable `action_id + scope_id` key and may represent several resolver
  targets. The scope is app-owned semantic state such as `viewport.selection.vertex`,
  `viewport.selection.edge`, `viewport.navigation.fly`, or `viewport.tool.modal`; an edit applies
  to every alias represented by that action row.
- A user-facing action/scope has zero or one physical mapping. One mapping may still be an ordered
  multi-chord sequence such as `Ctrl+K Ctrl+C`.
- The public catalog exposes the singular current/default mapping, source (`Default` or `User`),
  customized/unassigned state, localized scope metadata, target diagnostics, and the exact
  read-only `when` expression.
- Capture produces complete keyboard, mouse, drag, double-click, and wheel sequences through the
  canonical Qt input adapter and formatter. Pure modifiers and key auto-repeat are ignored.
- Assignment analysis distinguishes compatible cross-context reuse, exact conflicts, and prefix
  conflicts. `Reject` and `Replace` are the public policies. Replace unassigns each colliding
  action before assigning the proposed mapping; compatible reuse needs no confirmation.
- Unassign intentionally leaves an action without a mapping. Reset Action restores its singular
  compiled default, and Reset All persists an empty override set.
- Keybinding edits save and activate immediately. Settings Apply/Cancel still governs only staged
  interface and viewport settings.
- Each edit builds one complete candidate user-rule set, saves it before activation, and emits a
  `KeybindingsCommit` event after success so retained menus and tooltips refresh off the frame loop.
- Candidate JSON uses the shared atomic text writer. A late flush or replacement failure must
  preserve the previous file and leave the live resolver/catalog unchanged.
- Persistence writes only action groups whose effective mappings differ from defaults while the
  existing target-level JSON assignment/removal schema remains loadable. Legacy groups containing
  several assignments are normalized in memory with the latest entry winning, and stored `when`
  text is rebound to the current app-owned scope predicate rather than becoming user-editable
  context state.

The coarse display category (`Global`, `Viewport`, navigation, preview, or tool) is not action
identity. Modeling scopes are derived from the existing host-action and instant-operation
availability metadata. Different semantic actions may reuse the same mapping when their supported
mode sets are mutually exclusive; for example, `M` may map to Merge Vertices in
`viewport.selection.vertex` and Merge Edges in `viewport.selection.edge`. Conceptually shared
operations aggregate their supported modes into one scope and one row; Radial Align spans the
component modes instead of becoming three unrelated bindings.

The UI should not store private UI-only shortcut state. It should call public/editor API methods that
route to `QuaderKeybindingService`, and must never duplicate parsing, formatting, conflict
detection, or target canonicalization.

Keybinding labels are complete action names, not reused menu leaves. Keep labels unique in each
locale and include the subject, operation, axis, or mode needed to recognize the command (for
example, `Align X Center`, `Activate Move Tool`, and `Bridge Selected Edges`).

Current compiled defaults added with the scoped editor are:

- `T` toggles transform tools between global and local orientation while the
  viewport is focused and no navigation, modal tool, transform drag, or
  operation preview owns the interaction. `Shift+T` remains Apply Active
  Material.
- `H` hides selected visible objects/groups and `Alt+H` unhides hidden objects while an object/group
  selection scope is active. Quader does not yet own persistent component-hide state, so these do
  not pretend to hide individual vertices, edges, or faces.
- `F1`, `F2`, `F3`, and `F4` select Top, Side, Front, and Perspective views.
- `F5`, `F6`, and `F7` select Wireframe, Shaded, and Rendered presentation.
- `Ctrl+1`, `Ctrl+2`, `Ctrl+3`, and `Ctrl+4` select Single, Two Vertical, Two Horizontal, and Four
  Quad viewport layouts.
- `Alt+WheelUp` and `Alt+WheelDown` increase and decrease the grid preset;
  `Alt+MiddleMouseClick` restores the default 32-unit preset.

The three relative/reset grid gestures dispatch as semantic `SetGridPreset` viewport shortcuts
using the synchronized current preset. Do not route them only as immediate commands: actual wheel
and mouse input is consumed by the viewport semantic router.

On Windows, Qt rewrites an `Alt`-modified vertical wheel into a horizontal-only `angleDelta` before
the event reaches Quader. `src/ui/qt_input_adapter.*` must reverse that platform representation
before canonical wheel-direction and step classification. Production-path regression coverage must
send the Windows-shaped X delta through `QtViewportSurface` and `QtViewportPanel`; an idealized Y
delta alone does not cover the native failure.

Grid preset rows no longer own any `F1`-`F10` shortcut. Redo has one compiled mapping,
`Ctrl+Shift+Z`.

Texture Lock (`viewport.toggle_texture_lock`) and Texture Lock Scale
(`viewport.toggle_texture_lock_scale`) are separate bindable viewport commands. Both are
deliberately unassigned by default; the toolbar buttons do not install private Qt shortcuts.

## Testing Requirements

Input changes need fast synthetic coverage. Prefer headless tests over manual clicking whenever
possible.

Cover at least one relevant case from each group:

- parser/formatter accepts the intended keyboard, mouse, drag, or wheel chord;
- user override persists and reloads;
- conflict lookup reports expected conflicts;
- same key resolves differently in different contexts;
- active navigation continues while modifier keys are pressed after drag start;
- tool shortcuts do not fire while `navigation.active`;
- UI/source guardrails keep Qt conversion in `src/ui/qt_input_adapter.*` and routing through
  `ViewportInputRouter`.

Manual smoke checks are still useful for real window, mouse capture, and GPU behavior, but they are
not a replacement for these synthetic tests.
