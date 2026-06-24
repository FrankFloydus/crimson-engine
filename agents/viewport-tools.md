<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Viewport Tools

`Tool` and `ViewportTool` are the base viewport contracts. New viewport tools communicate through
`ViewportToolEvent`, `ViewportToolContext`, and `ViewportToolResult`.

For viewport keyboard, mouse, shortcut, user rebinding, or context work, also read
`../INPUT_KEYBINDINGS.md` before editing code.

## Tool Families

- Selection/activation tools derive from `ViewportTool` or `InteractiveViewportTool` when they only
  need activation/fallback behavior.
- Transform tools use `TransformViewportTool`.
- Box-like draw/height workflows use `TwoStageBoxTool`.
- Plane-captured point workflows use `DrawnPlaneTool`.
- Click-sequence tools use `PointSequenceTool`.
- Drag/commit tools use `DragCommitTool`.
- Use `ModalViewportTool` only when no narrower base fits.

## Rules

- Put concrete viewport tools under `src/editor/tools/interactive`.
- Descriptor-only wrappers may remain header-only.
- If a tool overrides event handling or owns a state machine, put that behavior in a `.cpp`.
- Register viewport tools in the viewport catalog implementation, not inline in headers.
- Update `tests/viewport_tool_catalog_tests.cpp` when ids, shortcuts, ownership, or runtime behavior
  changes.
- The UI shell builds presentation input and delegates; it does not own modal tool implementation,
  selection-picking policy, transform-gizmo behavior, or box/cut/knife/mirror state machines.
- `ViewportWorkspaceService` owns viewport workspace state: layout, focused pane, pane view,
  pane shading, grid preset, render size/visibility bookkeeping, and pane camera controllers.
  `UISystem` should consume that service for UI/render orchestration instead of introducing
  new long-lived layout, camera, shading, or grid fields.
- `QuaderViewportApi` should expose user/editor viewport intent through the workspace service.
  Runtime hooks are for host/render actions that cannot be represented as core viewport state,
  such as future screenshot/readback integrations.
- Viewport input should flow through canonical keybinding owners: platform events become
  `ViewportInputFrame` in `ViewportHost`, keybindings resolve through `QuaderCommandsApi` and
  `QuaderKeybindingService`, and `ViewportInputRouter` consumes semantic actions such as orbit,
  pan, fly, zoom, and reset fly speed. Do not add hardcoded key/button collisions in viewport tools.
- Use keybinding `when` contexts for same-key behavior. For example, tool hotkeys can use
  `!navigation.active`, while navigation-specific bindings remain active during fly/orbit/pan.
- Active mouse drags must keep the chord captured at drag start. Do not recompute drag modifiers
  every frame; pressing Shift during `RightMouseDrag` fly navigation must not cancel or reclassify
  the active drag.
