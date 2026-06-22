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
- `ViewportPanel` builds context and delegates; it does not own modal tool implementation,
  selection-picking policy, transform-gizmo behavior, or box/cut/knife/mirror state machines.
- `ViewportWorkspaceService` owns viewport workspace state: layout, focused pane, pane view,
  pane shading, grid preset, render size/visibility bookkeeping, and pane camera controllers.
  `ViewportPanel` should consume that service for UI/render orchestration instead of introducing
  new long-lived layout, camera, shading, or grid fields.
- `QuaderViewportApi` should expose user/editor viewport intent through the workspace service.
  Runtime hooks are for host/render actions that cannot be represented as core viewport state,
  such as future screenshot/readback integrations.
