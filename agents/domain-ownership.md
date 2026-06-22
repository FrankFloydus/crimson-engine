<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Domain Ownership

Use these boundaries when deciding where behavior belongs.

## Former SDK Code And Editor Domains

Former SDK geometry, polygon, editor-core, host-contract, and QDR IO code is now native-app code.
Keep it under app-owned modules such as `src/mesh/geometry`, `src/mesh/polygon`,
`src/editor/modeling`, `src/editor/interactions`, `src/editor/tools`, `src/editor/commands`, `src/editor/metadata`,
`src/editor/selection`, `src/editor/history`, `src/editor/projection`, `src/viewport`, and
`src/io/qdr`; do not recreate a separate SDK product boundary.

Portable behavior still matters, but the native app should implement it behind app services instead
of routing user/editor intent through a separate SDK authority. UI, render, and viewport code should
call public/editor services rather than duplicating polygon semantics.

## Scene, Project, And Assets

Scene graph and asset truth belongs in:

- `SceneDocument`;
- `ProjectDocument`;
- `ProjectSession`;
- `AssetRegistry`;
- typed asset stores;
- document commands.

UI must not directly create, destroy, parent, select, rename, transform, or mutate document entities
when a command/service owner exists.

## Document Commands

User-triggered document mutations belong in `src/editor/commands/document/**` as `IUndoableCommand`
implementations and are routed by `DocumentCommandService` or `EditorCommandService`.

Public scene intent should route through `QuaderSceneApi`. Prefer `QuaderSceneBatch` when a UI,
test, or automation workflow needs several scene edits to land as one undoable operation.

Scene-only transform previews belong in `SceneTransformPreviewTransaction`.

## Modeling

User-triggered modeling operations belong in `src/editor/commands/**` and execute through
`ModelingCommandDispatcher`, `ModelingPreviewCommandService`, `ModelingCommandExecutionService`, or
`EditorCommandService`.

Pure polygon and geometry algorithms belong under `src/mesh/**`. Editor-facing mesh workflow state
belongs under `src/editor/modeling/**`, viewport tool state machines belong under
`src/editor/tools/**`, and former SDK interaction math belongs under `src/editor/interactions/**`.
`ModelingSessionService` may exist only as a thin adapter over those
owners; it must not own public history, command semantics, bidirectional selection sync, or scene
identity.

## Viewport

Viewport pointer, modal, transform, camera, and drawing interactions belong in `src/viewport/**` and
viewport controllers/services. `ViewportPanel` builds context and delegates.

Viewport layout, focused pane, pane view, pane shading, grid preset, pane render size/visibility,
and camera controllers belong in `ViewportWorkspaceService`. `QuaderViewportApi` delegates core
viewport state to that service. `ViewportPanel` may own ImGui presentation and renderer lifetimes,
but it must not become the long-lived owner of viewport workspace state.

## UI

UI panels and controllers own presentation and routing only. If they express editor intent, prefer
public API calls. If they perform per-frame hover, drawing, or input assembly, use the narrowest
runtime owner.

For command-shaped UI intent, prefer `editor.commands().execute(QuaderCommand::...)` in C++.
Use raw string command IDs only where the command palette, future scripting layer, serialized data,
or dynamic catalog lookup requires strings.

## Render

Filament resource ownership and render extraction belong in `src/render/**`. `SceneRenderBridge` is
a render facade/composition root, not the owner of new render algorithms.
