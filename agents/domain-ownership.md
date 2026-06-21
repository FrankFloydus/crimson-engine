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

Future public viewport API should delegate to a viewport state/workspace service rather than
directly mutating `ViewportPanel`.

## UI

UI panels and controllers own presentation and routing only. If they express editor intent, prefer
public API calls. If they perform per-frame hover, drawing, or input assembly, use the narrowest
runtime owner.

## Render

Filament resource ownership and render extraction belong in `src/render/**`. `SceneRenderBridge` is
a render facade/composition root, not the owner of new render algorithms.
