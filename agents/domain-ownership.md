# Domain Ownership

Use these boundaries when deciding where behavior belongs.

## SDK And Editor-Core

Portable modeling, selection, picking, overlay payloads, box creation, navigation, and gizmo
semantics belong in SDK/editor-core first when they should be portable across Quader hosts.

The native app should call SDK behavior through app services instead of duplicating SDK semantics in
UI, render, or viewport code.

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

User-triggered document mutations belong in `src/document/commands/**` as `IUndoableCommand`
implementations and are routed by `DocumentCommandService` or `EditorCommandService`.

Public scene intent should route through `QuaderSceneApi`. Prefer `QuaderSceneBatch` when a UI,
test, or automation workflow needs several scene edits to land as one undoable operation.

Scene-only transform previews belong in `SceneTransformPreviewTransaction`.

## Modeling

User-triggered modeling operations belong in `src/modeling/tools/**` and execute through
`ModelingCommandDispatcher`, `ModelingPreviewCommandService`, `ModelingCommandExecutionService`, or
`EditorCommandService`.

Modeling session state and SDK synchronization belong in focused services under `src/modeling/**`.
Keep `ModelingSessionService` as a facade over owners, not as a dumping ground.

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
