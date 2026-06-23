<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Viewport Package Guide

Read `../../agents/viewport-tools.md` before changing viewport behavior.

## Viewport Rules

- `ViewportWorkspaceService` owns viewport workspace state: layout, focused pane, pane view, shading,
  grid preset, render size/visibility bookkeeping, and pane camera controllers.
- Keep camera, input routing, grid placement, viewport layout, and workspace state here.
- UI panels build context and delegate; they should not become long-lived owners of viewport state.
- Render code owns draw resources; viewport code owns state and interaction-facing policy.
- Expose user/editor viewport intent through `QuaderViewportApi`.
- Add synthetic tests for camera, layout, pane, shading, and input-routing state when possible.
