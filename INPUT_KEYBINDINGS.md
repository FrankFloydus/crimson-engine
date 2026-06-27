<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Input, Keybindings, And Contexts

This guide is the source-of-truth map for Quader input, shortcut, and future user keybinding work.
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
- `src/ui/controllers/viewport_host.cpp` translates platform events into a `ViewportInputFrame`;
  `src/viewport/ViewportInputRouter.*` consumes semantic navigation input and resolved viewport
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
- mouse buttons such as `LeftMouse`, `MiddleMouse`, `RightMouse`, `Mouse4`, `Mouse5`;
- drag gestures such as `MiddleMouseDrag` and `RightMouseDrag`;
- wheel gestures such as `Wheel`, `WheelUp`, and `WheelDown`.

For active mouse drags, keep the drag-start chord stable. Do not recompute drag modifiers every
frame. Example: `RightMouseDrag` fly navigation must continue if the user presses Shift after the
drag has started.

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
semantic navigation input, not raw SDL keys or hardcoded button combinations, except for legacy
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

Future UI for user customization should be built on these API concepts:

- list commands/actions from the command API and keybinding service;
- display default shortcut, current effective shortcut, source, and `when` context;
- capture input into `QuaderInputChord` using the canonical parser/formatter;
- show conflicts for the same shortcut and overlapping contexts;
- support reset one command, remove one shortcut, and reset all user overrides;
- write only user overrides/removals to `keybindings.json`;
- never duplicate key parsing, formatting, conflict detection, or command id canonicalization in UI.

The UI should not store private UI-only shortcut state. It should call public/editor API methods that
route to `QuaderKeybindingService`.

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
- UI/source guardrails keep input routing through `ViewportHost` and `ViewportInputRouter`.

Manual smoke checks are still useful for real window, mouse capture, and GPU behavior, but they are
not a replacement for these synthetic tests.
