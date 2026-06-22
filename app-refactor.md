<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader App Refactor Plan

Status: proposed architecture plan  
Scope: native Windows Quader app  
Decision date: 2026-06-22  

## Executive Summary

The current app has too many places that are allowed to interpret and mutate the same editor intent. The group delete and undo/redo bugs are not isolated mistakes. They are symptoms of a larger architectural problem:

- Scene hierarchy and group state live in the app document.
- Polygon object state and component selection live in the former SDK editor session.
- Selection is mirrored both ways between scene and modeling state.
- Undo/redo is split between app history and SDK history.
- UI surfaces call different mutation routes for the same user action.
- `ModelingSessionService` is trying to be a facade, bridge, session host, command coordinator, selection owner, overlay owner, transform preview owner, and history adapter at once.

The fix is not to add more local patches around delete. The fix is to remove the separate `sdk/` world as a separate authority, absorb the useful SDK code into the app source tree, and create one app-owned editor architecture with:

- one app-facing editor document,
- one public command path,
- one public history stack,
- one selection authority,
- one projection path from editor data to render/cache data,
- one set of behavior tests that drive the public API first.

The `sdk/` folder should be removed as a top-level product boundary for this native app. Its code should be migrated into `src/` as normal app modules with clear ownership. We should not throw away the good geometry, polygon document, picking, operation, and tool-runtime code. We should stop treating that code as an external subsystem with its own editor truth.

## Agent Pass Synthesis

Six focused agents inspected the current codebase. Their findings converged on the same root cause even when they disagreed about the final packaging.

### Ownership And Source Of Truth

The app currently has two mutable truths for editor objects:

- `SceneDocument` owns entities, hierarchy, groups, transforms, visibility, locks, active scene entity, selected scene entities, mesh/material/light components, and tree order.
- The former SDK `EditorSession` owns polygon objects, object selection, component selection, tool state, previews, and SDK history.
- `ModelingSessionService` bridges both directions and decides when one side overwrites the other.

Groups already exist as scene/document concepts. They are not polygon topology. Pushing groups into the old polygon `EditorSession` would duplicate `SceneDocument` inside the modeling session.

### Undo/Redo

The app has two public histories pretending to be one:

- `UndoRedoManager` stores app/document commands.
- Former SDK `EditorSession` stores session snapshots.
- `EditorCommandService` currently exposes undo/redo by trying app history first and then falling back to modeling history.
- Some modeling commands are wrapped through `ModelingHistoryCommand`, which assumes the top SDK history entry still belongs to the app command that wrapped it.

This creates order bugs. For example:

1. Create a box through modeling.
2. Rename the scene entity through the app document.
3. Undo twice.
4. Redo twice.

Chronologically, the box must be recreated before the rename can be replayed. With split histories, app redo and SDK redo can race or replay in the wrong order.

### Selection And Input

Selection currently has several meanings:

- scene entity selection,
- modeling object selection,
- component selection,
- group mode selection,
- viewport UI mode.

The same visible user action can go through different routes:

- outliner selection calls scene selection and then manual modeling sync,
- viewport fallback selection calls document commands,
- viewport mesh picking calls modeling `pickSelect`,
- context menu actions call `editor.modeling().executeAction`,
- shortcuts mostly call `editor.commands().execute`.

That is why one delete surface can behave differently than another. The UI is not just presenting intent. It is deciding which subsystem owns the intent.

### Service Boundaries

`ModelingSessionService` is the largest architectural smell. Its implementation owns or coordinates:

- former SDK session/runtime,
- scene document mutation,
- asset updates,
- mesh sync,
- object registry,
- selection sync,
- hover,
- picking,
- overlays,
- modal tools,
- transform previews,
- group duplication,
- history bridging,
- command execution choreography.

Several helper services exist, but the dependency direction is still not clean. Some helper headers include the facade header they are supposed to help split.

### Public API And Tests

`QuaderEditorApi` is already the right whole-editor front door. The new public API and scenario DSL are the right way to test behavior. The problem is that not all app behavior routes through it yet, and some tests currently protect the workaround paths instead of the desired architecture.

The future test harness should make invariants automatic after every public mutation:

- document state coherent,
- modeling state coherent,
- render/asset projection coherent,
- selection coherent,
- history coherent,
- revisions coherent.

### SDK Folder Packaging

One agent recommended keeping `sdk/` as a submodule. That would make sense for a stable multi-host SDK product. The user's decision is different and accepted here: for the native app, the top-level `sdk/` folder is now a liability because it encourages a second authority and a mental model that says "the app cannot touch this." The code can be kept, but the folder boundary should go.

The plan below absorbs the former SDK code into `src/`, then re-splits it by actual app ownership.

## Hard Architectural Decisions

### Decision 1: Remove `sdk/` As A Separate Top-Level Codebase

The native app should no longer build against `sdk/packages/*` as an external-like subsystem.

Planned outcome:

- delete the top-level `sdk/` folder after migration,
- move needed source code into `src/`,
- move tests into app test directories,
- remove `QUADER_MODELING_SDK_DIR` and SDK override concepts from the app build,
- remove `add_subdirectory(sdk)` style build boundaries,
- replace `quader_editor_core`, `quader_poly`, `quader_io`, and `quader_host_contract` include paths with app-owned include paths.

This does not mean "throw away all SDK code." It means "the app owns this code now."

### Decision 2: One Public Editor History

The native app gets one public undo/redo stack: `EditorHistoryService`, built from or replacing `UndoRedoManager`.

Planned outcome:

- no public fallback to former SDK undo/redo,
- no `canUndo = appCanUndo || modelingCanUndo`,
- no UI/public API path calling modeling undo/redo directly,
- every user-visible mutation becomes one editor transaction,
- preview transactions are separate from committed history,
- former SDK history code is either removed, made private to low-level tests, or converted into snapshot/diff helpers.

### Decision 3: One Selection Authority

Selection is an editor-domain concern, not a scene-only concern and not a modeling-session-only concern.

Planned outcome:

- `EditorSelectionService` owns mode, active target, selected targets, and selection transitions,
- UI can provide input facts, but cannot decide selection authority,
- scene entity selection and polygon component selection become projections of editor selection,
- group selection is first-class,
- selection sync methods become private transitional implementation details, then disappear.

### Decision 4: One Public Command Doorway

All UI, script, public API, and shortcut mutation must route through `QuaderEditorApi` and command services.

Planned outcome:

- keyboard Delete, Canc, popup X, context menu Delete, outliner Delete, command palette Delete, and API Delete all execute the same command ID,
- context menus expose command IDs instead of raw modeling action callbacks,
- old direct action routes become command handlers,
- command descriptors become the single source for labels, availability, routing, history labels, shortcut binding, menu visibility, and scripting IDs.

### Decision 5: Editor Document Owns Scene-Level Truth

Groups, hierarchy, transforms, visibility, locks, scene selection, stable object identity, material bindings, and entity lifetime belong to the app editor document.

Planned outcome:

- `ProjectDocument` evolves into or owns `EditorDocument`,
- `SceneDocument` is either renamed to `SceneGraphDocument` or remains as the scene-graph component of `EditorDocument`,
- former SDK object IDs become internal runtime IDs, not public object identity,
- stable app IDs survive undo/redo, duplication, import/export, and projection rebuilds.

### Decision 6: Polygon Modeling Becomes A Domain Module, Not An Editor Authority

The old SDK polygon document and operations are still valuable. They should become app-owned modeling domain code.

Planned outcome:

- polygon topology, mesh compilation, UVs, materials slots, component selection, and modeling operations live under `src/mesh/polygon` or `src/modeling/core`,
- they do not own scene hierarchy,
- they do not own public history,
- they do not create or destroy scene entities directly,
- operations return typed deltas/results that editor transactions apply.

### Decision 7: Projection Is One-Way

Rendering, mesh assets, overlays, and picking caches should be projections from editor document/modeling state, not competing state owners.

Planned outcome:

- `EditorDocument` plus polygon documents are the write model,
- render scene, mesh assets, picking caches, and overlays are derived read models,
- projection services invalidate and rebuild caches after committed transactions,
- no projection service should decide user selection or history.

## What "Scene vs SDK" Means In This Codebase

The phrase has become too vague. Here is the exact current split.

### Scene Side

Scene side currently means app-native document state:

- `src/scene/SceneDocument.*`
- `src/scene/Components.h`
- `src/document/ProjectDocument.*`
- `src/document/ProjectSession.*`
- `src/document/DocumentCommandService.*`
- `src/document/commands/**`
- scene entity IDs,
- hierarchy/group nodes,
- transforms,
- visible/locked flags,
- mesh/material/light components,
- selection components,
- active entity,
- tree order.

This side is needed for the native app. It is the correct home for group hierarchy and scene objects.

### SDK Side

SDK side currently means the old `sdk/packages/*` code:

- polygon topology document,
- polygon operations,
- editor session,
- component selection,
- object selection,
- picking,
- transform previews,
- overlay payloads,
- viewport navigation math,
- gizmo math,
- SDK history snapshots,
- QDR IO,
- host contract descriptors.

This code is not useless. The problem is that it is packaged and treated as a second editor authority.

### The Actual Bug Class

The dangerous overlap is not "we have a scene and a polygon document." That is normal. The dangerous overlap is this:

- scene and modeling both have selected object concepts,
- scene and modeling both participate in object lifetime,
- scene and modeling both participate in undo/redo,
- UI can call either side directly,
- sync direction depends on caller order,
- deleting a group must guess whether scene selection, SDK selection, or group mode is authoritative.

The refactor removes that overlap.

## Target Directory Layout

The old app-owned modeling domain should be moved into app source modules. A proposed final tree:

```text
src/
  app/
    App.*
    AppCompositionRoot.*

  api/
    QuaderEditorApi.*
    QuaderApi.*
    QuaderCommandIds.*
    QuaderCommandsApi.*
    QuaderSelectionApi.*
    QuaderSceneApi.*
    QuaderModelingApi.*
    QuaderHistoryApi.*
    QuaderScript.*

  assets/
    AssetRegistry.*
    MeshAsset.*
    MaterialDocument.*
    TextureAsset.*
    BakeService.*

  core/
    CommandResult.*
    IUndoableCommand.*
    Logger.*
    Types.*
    LocalizationService.*

  editor/
    document/
      EditorDocument.*
      EditorDocumentSnapshot.*
      EditorDocumentDelta.*
      EditorObjectId.*
      EditorRevisionClock.*
      EditorInvariantCheck.*

    commands/
      EditorCommand.*
      EditorCommandRegistry.*
      EditorCommandDescriptor.*
      DeleteSelectionCommand.*
      DuplicateSelectionCommand.*
      GroupSelectionCommand.*
      UngroupSelectionCommand.*
      TransformSelectionCommand.*
      CreatePrimitiveCommand.*
      AssignMaterialCommand.*
      CompositeEditorCommand.*

    history/
      EditorHistoryService.*
      EditorTransaction.*
      EditorTransactionBuilder.*
      EditorHistorySnapshot.*

    selection/
      EditorSelectionService.*
      EditorSelectionState.*
      EditorSelectionTarget.*
      EditorSelectionPolicy.*
      ViewportPickSelectionCommand.*

    projection/
      EditorProjectionService.*
      MeshProjectionService.*
      SelectionOverlayProjection.*
      PickingCacheProjection.*

    tools/
      ToolRuntime.*
      ModalToolSession.*
      TransformInteractionService.*
      PreviewTransactionService.*

  geometry/
    Geometry.*
    Ray.*
    Plane.*
    Aabb.*
    Intersection.*

  io/
    qdr/
      QdrDocumentReader.*
      QdrDocumentWriter.*
      QdrFormat.*

  modeling/
    polygon/
      PolygonDocument.*
      PolygonDocumentBuilder.*
      PolygonMeshCompiler.*
      PolygonSelection.*
      PolygonOperation.*
      PolygonOperationRegistry.*
      operations/
        DeleteSelectionOperation.*
        BridgeEdgesOperation.*
        BevelEdgesOperation.*
        BevelVerticesOperation.*
        AssignMaterialSlotOperation.*
        ...

    ModelingWorkspace.*
    ModelingWorkspaceSnapshot.*
    ModelingWorkspaceDelta.*
    ModelingObjectRuntimeMap.*
    ModelingOperationExecutor.*
    ModelingPickingService.*
    ModelingOverlayService.*
    ModelingTransformPreview.*

  platform/
    PlatformWindow.*
    FatalErrorHandler.*

  render/
    SceneRenderBridge.*
    SceneRenderSyncService.*
    ViewportOverlayFrame.*
    ...

  scene/
    SceneDocument.*
    SceneBounds.*
    Components.*
    TransformPivot.*

  ui/
    OutlinerPanel.*
    ViewportPanel.*
    ViewportSelectionController.*
    ViewportContextMenuPresenter.*
    TransformGizmoController.*
    ...

  viewport/
    ViewportInputRouter.*
    ViewportWorkspaceService.*
    ViewportCameraController.*
    ViewportNavigation.*
    ViewportGizmoMath.*
```

The exact names can change during implementation, but the ownership lines should not.

## Former SDK Package Migration Map

### `sdk/packages/foundation`

Move to:

- `src/core` for generic app primitives,
- `src/mesh/geometry` for math/geometric primitives,
- `src/mesh/polygon` only for topology-specific primitives.

Rules:

- no public "SDK foundation" target,
- no dependency from app UI to low-level modeling internals,
- no hidden global state.

### `sdk/packages/geometry`

Move to:

- `src/mesh/geometry`

Planned names:

- `quader::geometry`
- `Ray`
- `Plane`
- `Aabb`
- `Intersection`
- `Projection`
- geometry predicates

Rules:

- keep it low-level and reusable,
- do not include UI, render, scene document, or command headers,
- dependency direction is inward only,
- external libraries such as GTE stay behind implementation boundaries,
- no GTE types in public app APIs.

### `sdk/packages/document`

Move to:

- `src/mesh/polygon`

Planned names:

- `PolygonDocument`
- `PolygonObject`
- `PolygonMeshCompiler`
- `PolygonOperation`
- `PolygonOperationRegistry`
- `PolygonSelection`
- `PolygonDocumentBuilder`

Rules:

- this module owns polygon topology only,
- it can own vertex/edge/face/loop/poly IDs,
- it can own polygon material slots, UVs, and mesh compilation,
- it cannot own scene groups,
- it cannot own scene entity IDs,
- it cannot push app undo entries,
- operations return results/deltas for editor transactions.

### `sdk/packages/editor-core`

Split across several app modules:

- polygon session/object operation code -> `src/modeling`,
- selection policy -> `src/editor/selection` plus `src/modeling` for component internals,
- session history -> `src/editor/history` only if converted to app transaction snapshots; otherwise delete or keep as test-only transition code,
- transform preview -> `src/editor/commands` or `src/modeling`,
- transform gizmo math -> `src/viewport` or `src/editor/commands`,
- viewport navigation math -> `src/viewport`,
- overlay policy -> `src/editor/projection`.

Rules:

- do not move the whole `EditorSession` as-is and rename it,
- split it by responsibility,
- delete public SDK history semantics,
- keep only one app-visible state owner.

### `sdk/packages/host-contract`

Move to:

- `src/editor/commands`
- `src/api`
- `src/editor/commands`

Planned owners:

- `EditorCommandRegistry` owns command descriptors,
- `ToolRegistry` owns tool descriptors,
- `QuaderCommandsApi` exposes command availability and execution,
- UI reads descriptors, but does not own command semantics.

Rules:

- descriptors are data,
- command handlers own behavior,
- no duplicate metadata in context menus, action catalogs, shortcut routers, and scripting.

### `sdk/packages/io`

Move to:

- `src/io/qdr`

Rules:

- IO reads and writes `EditorDocument`, not an SDK session,
- old polygon-only QDR IO can survive as an internal codec if useful,
- import/export must preserve stable editor IDs, group hierarchy, material bindings, and polygon topology,
- load/save must not recreate stale runtime IDs as public identity.

### `sdk/packages/diagnostics`

Move to:

- `src/diagnostics` if the app still needs it,
- or merge small profiling helpers into `src/core`.

Rules:

- profiling helpers must not become a global dependency magnet,
- no diagnostics dependency from pure polygon code unless it is header-light and optional.

### SDK Tests

Move to:

- `tests/modeling_polygon_tests.cpp`
- `tests/modeling_operations_tests.cpp`
- `tests/modeling_workspace_tests.cpp`
- `tests/editor_selection_tests.cpp`
- `tests/editor_history_tests.cpp`
- `tests/io_qdr_tests.cpp`

Rules:

- old SDK unit tests become normal app unit tests,
- behavior tests use public API and scenario DSL,
- low-level tests can include low-level modules directly.

## Target Runtime Architecture

### EditorDocument

`EditorDocument` becomes the app-facing document model. It should own:

- `SceneDocument` or its replacement scene graph,
- stable editor object records,
- links from scene entities to polygon documents,
- asset references,
- selected targets,
- active target,
- selection mode,
- revision counters,
- document metadata,
- dirty state,
- load/save identity.

Proposed core types:

```cpp
struct EditorObjectId {
    uint64_t value = 0;
};

enum class EditorNodeKind {
    Empty,
    Group,
    Mesh,
    Light,
    Camera
};

struct EditorNode {
    EntityId entity;
    EditorObjectId stableObjectId;
    EditorNodeKind kind;
    std::optional<PolygonDocumentId> polygonDocument;
};

struct EditorDocument {
    SceneDocument scene;
    AssetRegistry assets;
    EditorObjectStore objects;
    EditorSelectionState selection;
    EditorRevisionClock revisions;
};
```

The app can keep `SceneDocument` internally, but public/editor code should stop treating it as the only document. `SceneDocument` is the scene graph part of `EditorDocument`.

### Polygon Modeling

Polygon modeling becomes a pure or mostly-pure domain module.

It owns:

- polygon objects,
- topology,
- vertices/edges/faces,
- face loops,
- UVs,
- material slots,
- mesh compilation,
- polygon operations,
- component-level operation validation.

It does not own:

- scene entity hierarchy,
- groups,
- editor public selection,
- app history,
- render assets,
- UI state.

Modeling operations should move toward this shape:

```cpp
struct PolygonOperationRequest {
    PolygonDocumentId document;
    PolygonSelection selection;
    PolygonOperationParameters parameters;
};

struct PolygonOperationResult {
    bool changed = false;
    PolygonDocumentDelta delta;
    PolygonSelection newSelection;
    std::string message;
};
```

The editor transaction applies the result and records undo/redo.

### ModelingWorkspace

`ModelingWorkspace` replaces the old `EditorSession` as an internal runtime workspace.

It owns:

- active polygon object runtime map,
- temporary tool state,
- component picking acceleration,
- hover state,
- transform previews,
- overlay extraction,
- modal operation state.

It does not own:

- public history,
- scene entity creation/destruction,
- group selection,
- permanent object identity.

It should be rebuildable from `EditorDocument` plus caches. If it cannot be rebuilt, it is owning too much persistent state.

### EditorSelectionService

`EditorSelectionService` owns all user-facing selection.

Selection targets should be typed:

```cpp
enum class EditorSelectionTargetKind {
    SceneEntity,
    Group,
    MeshObject,
    Vertex,
    Edge,
    Face,
    Light,
    Camera
};

struct EditorSelectionTarget {
    EditorSelectionTargetKind kind;
    EntityId entity;
    EditorObjectId object;
    PolygonDocumentId polygonDocument;
    uint64_t componentId;
};
```

Selection service responsibilities:

- set selection mode,
- select targets,
- clear selection,
- select all,
- invert selection,
- convert selection between object and component modes,
- pick viewport targets,
- paint-pick viewport targets,
- preserve/replace selection on right-click according to explicit policy,
- produce scene selection projection,
- produce polygon selection projection,
- update active target.

UI responsibilities:

- collect ray, mouse button, modifiers, viewport size, clicked panel,
- call selection API/command,
- display the returned state.

UI must not:

- call `syncObjectSelectionFromScene`,
- call `syncSceneSelectionFromModeling`,
- call `DocumentCommandService::selectEntities`,
- call modeling `pickSelect`,
- switch modeling mode as a workaround for group selection.

### EditorHistoryService

`EditorHistoryService` replaces public `UndoRedoManager` usage. It can wrap the existing `UndoRedoManager` internally during migration, but app code should depend on the new concept.

Responsibilities:

- execute transactions,
- undo,
- redo,
- expose labels,
- expose canUndo/canRedo,
- clear redo after any new changed transaction,
- group multiple low-level edits into one user-visible step,
- capture editor document, polygon document, asset, selection, and projection invalidation changes as one unit.

Transaction shape:

```cpp
struct EditorTransaction {
    std::string label;
    EditorDocumentDelta documentDelta;
    std::vector<PolygonDocumentDelta> polygonDeltas;
    AssetDelta assetDelta;
    EditorSelectionDelta selectionDelta;
    ToolStateDelta toolStateDelta;
    ProjectionInvalidation invalidation;
};
```

Undo/redo restores exact transaction deltas. It does not ask a modeling session to undo "whatever is on top."

### EditorCommandRegistry

Commands become the single public mutation vocabulary.

Responsibilities:

- register command ID,
- label,
- category,
- shortcut,
- availability predicate,
- handler,
- history label,
- whether the command is undoable,
- UI visibility,
- scripting exposure,
- command palette metadata.

Surfaces that must use it:

- keyboard shortcuts,
- Canc/Delete,
- popup X,
- viewport context menu,
- outliner menu,
- main menu,
- toolbar,
- command palette,
- scripts,
- public API convenience methods.

The delete command must have one implementation. Any path that deletes a group, mesh, child, component selection, or scene selection must end in that same command handler, with typed request data.

### Projection Services

Projection services convert editor state into derived state:

- mesh assets,
- renderable instances,
- picking acceleration,
- overlays,
- bounds,
- outliner display rows.

Proposed services:

- `MeshProjectionService`
- `RenderSceneProjectionService`
- `PickingCacheProjectionService`
- `SelectionOverlayProjectionService`
- `OutlinerProjectionService`

Rules:

- projection is invalidated by transactions,
- projection may cache aggressively,
- projection never owns user intent,
- projection never commits history,
- projection never decides selection authority.

## Specific Current Hotspots To Replace

### `src/modeling/ModelingSessionService.*`

Current role:

- god facade and god implementation.

Planned split:

- `src/modeling/ModelingWorkspace.*`
- `src/modeling/ModelingOperationExecutor.*`
- `src/modeling/ModelingPickingService.*`
- `src/modeling/ModelingOverlayService.*`
- `src/modeling/ModelingTransformPreview.*`
- `src/editor/projection/MeshProjectionService.*`
- `src/editor/selection/EditorSelectionService.*`
- `src/editor/commands/TransformInteractionService.*`
- `src/editor/commands/DeleteSelectionCommand.*`
- `src/editor/commands/DuplicateSelectionCommand.*`

Migration rule:

- first extract behavior without changing public behavior,
- then replace public callers,
- then delete facade methods.

Methods to make private or remove:

- `syncObjectSelectionFromScene`
- `syncSceneSelectionFromModeling`
- `duplicateSelectedHierarchyForGroupDrag`
- public `undo`
- public `redo`
- public `canUndo`
- public `canRedo`
- direct mutation helpers that bypass app history.

### `src/editor/commands/EditorCommandService.h`

Current role:

- app undo/redo wrapper with modeling fallback.

Planned replacement:

- `EditorCommandService` routes commands only,
- `EditorHistoryService` owns history,
- no modeling undo/redo fallback.

Required deletion:

- `undoRedo_.canUndo() || modeling_->canUndo()`
- app undo followed by modeling fallback,
- app redo followed by modeling fallback.

### `src/editor/ModelingHistoryCommand.h`

Current role:

- wraps SDK top-of-stack undo/redo.

Planned replacement:

- delete or replace with exact `EditorTransaction` capture.

Reason:

- an app command must not undo "whatever SDK history entry is currently on top."

### `src/editor/ModelingCommandExecutionService.*`

Current role:

- routes modeling action execution,
- special-cases scene-aware delete,
- wraps some actions in app history.

Planned replacement:

- command handlers live in `src/editor/commands`,
- modeling operations are internal implementation details,
- scene-aware delete becomes normal `DeleteSelectionCommand`,
- every mutating command returns one `EditorTransaction`.

### `src/api/QuaderApiMutationRunner.*`

Current role:

- reliability hinge for public API mutations.

Planned role:

- still important,
- but it should execute editor commands/transactions, not raw modeling service calls,
- every public mutation gets consistent status, revision, history, and event behavior.

### `src/api/QuaderModelingApi.*`

Current role:

- public convenience API that can call modeling service directly.

Planned role:

- convenience facade over editor commands and typed modeling requests,
- no direct raw mutation without history,
- `executeAction` becomes deprecated in favor of command IDs or routes through command registry.

### `src/api/QuaderSelectionApi.*`

Current role:

- mixed public selection API that can call modeling selection directly.

Planned role:

- facade over `EditorSelectionService`,
- all selection changes return coherent editor selection state,
- picking selection calls editor selection command, not modeling service.

### `src/api/QuaderSceneApi.*`

Current role:

- scene selection and scene batch construction can bypass selection authority.

Planned role:

- scene object operations delegate to editor document commands,
- `select` delegates to `EditorSelectionService`,
- `QuaderSceneBatch` delegates transaction construction to editor/document command builders.

### `src/ui/ViewportPanel.*`

Current role:

- rendering panel,
- input routing,
- context menu,
- overlay composition,
- selection workaround logic,
- tool activation,
- direct modeling calls.

Planned split:

- panel owns layout and UI composition,
- selection goes through `EditorSelectionService`,
- commands go through `QuaderCommandsApi`,
- overlay composition moves to projection service,
- direct document/modeling mutation leaves the panel.

### `src/ui/ViewportSelectionController.*`

Current role:

- parallel route for group fallback selection and modeling picking.

Planned replacement:

- becomes a thin adapter that sends pick facts to `EditorSelectionService`,
- no direct `DocumentCommandService`,
- no direct `modeling.pickSelect`.

### `src/ui/ViewportContextMenuPresenter.*`

Current role:

- calls `editor.modeling().executeAction`.

Planned replacement:

- exposes command IDs,
- checks command availability through registry,
- executes `editor.commands().execute(commandId)`.

### `src/ui/OutlinerPanel.*`

Current role:

- manually changes modeling selection mode,
- calls scene selection,
- calls modeling sync,
- restores group mode,
- has delete-specific routing.

Planned replacement:

- outliner emits selection or command intent,
- selection goes through `EditorSelectionService`,
- delete/duplicate/group/ungroup go through command registry,
- no direct modeling mode changes.

### `src/ui/TransformGizmoController.*`

Current role:

- hit testing,
- transform drag state,
- modeling preview,
- scene preview,
- group duplicate,
- history commit logic.

Planned split:

- UI hit testing and draw remain,
- transform planning moves to `TransformInteractionService`,
- preview state moves to `PreviewTransactionService`,
- commit produces one `EditorTransaction`,
- Shift duplicate group drag becomes `DuplicateSelectionCommand` plus transform transaction.

### `src/editor/tools/**`

Final role:

- editor-owned viewport tool contracts, catalog rows, and state machines,
- tools emit typed command/tool requests,
- preview tools own temporary interaction state only,
- committed tool results go through `EditorHistoryService`,
- `ViewportToolContext` does not contain raw `UndoRedoManager`.

## Command Routing Plan

### Command IDs

Typed command IDs remain the C++ surface:

- `QuaderCommand::HistoryUndo`
- `QuaderCommand::HistoryRedo`
- `QuaderCommand::DeleteSelection`
- `QuaderCommand::DuplicateSelection`
- `QuaderCommand::GroupSelection`
- `QuaderCommand::UngroupSelection`
- `QuaderCommand::SetSelectionModeObject`
- `QuaderCommand::SetSelectionModeGroup`
- `QuaderCommand::SetSelectionModeVertex`
- `QuaderCommand::SetSelectionModeEdge`
- `QuaderCommand::SetSelectionModeFace`
- modeling operation command IDs.

Stable strings remain the script/command-palette representation.

### Delete Command Contract

`DeleteSelectionCommand` must handle:

- selected group root,
- selected nested group,
- selected group children,
- selected mesh entity,
- selected light/entity,
- selected component selection,
- stale modeling selection with valid scene selection,
- scene selection empty but component selection active,
- mixed entity selection,
- locked/hidden policy,
- undo restore,
- redo delete again,
- active selection restoration policy.

All delete surfaces call this command:

- keyboard Delete,
- keyboard Canc,
- popup X,
- viewport context menu Delete,
- outliner Delete,
- main menu Delete,
- command palette Delete,
- API delete.

### Command Availability

Availability must be computed once.

Delete availability examples:

- enabled if editor selection has deletable targets,
- disabled if all targets are locked,
- enabled for component selection when polygon operation supports delete,
- disabled during modal tool states that reject delete,
- context menu reads same result as keyboard.

### Action Catalog Cleanup

Current scattered catalogs should collapse:

- `ModelingActionCatalog`
- `ModelingCommandToolCatalog`
- `instantOperations()`
- command descriptors
- context menu visible actions
- script action strings

Target:

- one `EditorCommandRegistry`,
- one modeling operation registry for low-level operation implementations,
- command registry maps user command to low-level operation where needed.

## History Plan

### Phase A: Characterize Current Mixed History

Add tests before changing behavior:

- `createBox -> rename -> undo -> undo -> redo -> redo`
- `createBox -> group -> delete group -> undo -> redo`
- `createBox -> set material -> undo -> redo`
- `createBox -> transform -> undo all -> redo all`
- `component delete -> undo -> redo`
- `group duplicate drag -> undo -> redo`
- new document edit after modeling undo clears public redo.

These tests should currently expose the split-history problem. If some pass due to recent patches, keep them as regression coverage.

### Phase B: Introduce EditorHistoryService

Add:

- `src/editor/history/EditorHistoryService.h`
- `src/editor/history/EditorHistoryService.cpp`
- `src/editor/history/EditorTransaction.h`
- `src/editor/history/EditorTransactionBuilder.h`

Initially it can wrap `UndoRedoManager`, but callers move to `EditorHistoryService`.

### Phase C: Wrap Modeling Mutations

Every public modeling mutator must become an editor transaction:

- create box,
- create mesh,
- commit two-stage box,
- transform preview commit,
- transform gizmo commit,
- move selection,
- transform selection,
- assign material slot,
- delete component selection,
- duplicate selected hierarchy,
- paste/import mesh,
- any operation that creates/destroys scene entities.

### Phase D: Remove Public SDK History

Remove:

- public modeling `undo`,
- public modeling `redo`,
- public modeling `canUndo`,
- public modeling `canRedo`,
- `EditorCommandService` modeling fallback,
- `QuaderQueryApi` app-or-modeling history query,
- `BehaviorExpect` app-or-modeling history expectation,
- tests that expect split history.

### Phase E: Exact Transaction Restore

Undo/redo should restore:

- scene graph deltas,
- polygon document deltas,
- object registry deltas,
- mesh asset deltas,
- material assignment deltas,
- selection deltas,
- active target,
- relevant tool state,
- projection invalidation.

It should not restore by replaying current commands against current mutable state unless the command is explicitly designed for that.

## Selection Plan

### Phase A: Define EditorSelectionState

Add:

- selected targets,
- active target,
- mode,
- last selection source,
- component selection payload,
- group selection payload,
- selection revision.

### Phase B: Centralize Selection Entry Points

Move public selection to:

- `editor.selection().setMode(...)`
- `editor.selection().select(...)`
- `editor.selection().pick(...)`
- command IDs for common selection operations.

Deprecate or reroute:

- `editor.scene().select(...)`,
- `editor.modeling().setSelectionMode(...)`,
- `editor.selection().selectObjects(...)` if it means SDK-only object selection,
- direct `modeling.pickSelect(...)`.

### Phase C: Make Scene And Polygon Selection Projections

Scene selection projection:

- selected scene entities for outliner/render highlights.

Polygon selection projection:

- selected polygon object/component IDs for modeling operations.

Projection direction:

- editor selection -> scene/polygon projections.

The old bidirectional sync methods should disappear.

### Phase D: Guard UI

Add tests or source guardrails forbidding UI from calling:

- `context.editor.scene().select`,
- `documentCommands.selectEntities`,
- `context.modeling.syncObjectSelectionFromScene`,
- `context.modeling.syncSceneSelectionFromModeling`,
- `modeling.pickSelect`,
- `modeling.setSelectionMode`,
- `editor.modeling().executeAction` for user-visible commands.

Allow exceptions only in low-level transition tests and clearly named adapter code.

## Editor Document And Identity Plan

### Stable IDs

Introduce stable app-level IDs:

- `EditorObjectId`
- `EditorNodeId` if distinct from scene `EntityId`
- `PolygonDocumentId`

Scene `EntityId` can remain, but it should not be the only durable identity for modeling objects.

### Object Registry

Current `ModelingObjectRegistry` maps SDK object IDs to scene entities. Replace it with an app-owned map:

- stable editor object ID -> scene entity,
- stable editor object ID -> polygon document,
- stable editor object ID -> runtime workspace object ID,
- runtime IDs are rebuildable.

### Group Semantics

Groups are editor document nodes.

Required behavior:

- group can contain mesh, light, empty, nested group,
- group delete deletes children according to explicit command policy,
- undo restores full hierarchy,
- redo deletes the same hierarchy,
- transform group applies hierarchy transform or child transforms according to explicit rules,
- group selection does not require component/object selection hacks.

### Material Semantics

Material assignment should be owned by editor document/asset commands:

- scene entity material binding,
- polygon face/material slot binding,
- material assets,
- projection from slot to mesh asset/render material.

Undo/redo must restore material assets and bindings together.

## Modeling Operation Plan

### Low-Level Operation Interface

All polygon operations should move toward:

- explicit request,
- explicit validation,
- explicit result,
- explicit delta,
- no direct scene mutation,
- no public history mutation,
- no UI dependency.

### Operation Categories

Keep and migrate:

- primitive creation,
- delete selection,
- extrude,
- inset,
- bevel,
- bridge,
- connect,
- collapse,
- dissolve,
- detach,
- triangulate,
- flip normals,
- UV operations,
- assign material slot.

### Editor-Level Modeling Commands

Editor commands wrap low-level operations:

- resolve selected targets,
- validate mode and availability,
- call polygon operation,
- apply polygon delta,
- apply scene/entity changes if needed,
- update selection,
- invalidate projection,
- emit one history transaction.

## Projection And Cache Plan

### Mesh Projection

Current `ModelingMeshSyncService::syncSceneObjectsFromModeling` creates/destroys scene entities and mesh assets from SDK objects. That is too powerful for a projection service.

Target:

- editor commands create/destroy scene entities,
- polygon operations update polygon documents,
- `MeshProjectionService` updates mesh assets for changed polygon documents,
- projection never creates/deletes editor objects except during controlled import/migration.

### Picking Cache

Picking cache should be derived from:

- scene transforms,
- polygon mesh data,
- visibility/lock state,
- selection mode.

Picking result should return typed candidate facts. `EditorSelectionService` decides what to select.

### Overlay Projection

Overlay projection should derive:

- selected group bounds,
- selected mesh wire,
- selected vertex/edge/face overlays,
- hover overlay,
- transform gizmo handles,
- modal tool preview overlays.

UI should draw overlay frames. It should not assemble modeling semantics.

## UI Simplification Plan

### Outliner

Outliner should:

- render projected hierarchy rows,
- send `SelectTargets` requests,
- send command IDs for delete/duplicate/group/ungroup/rename,
- display command availability.

Outliner should not:

- switch modeling modes,
- call scene selection directly,
- call modeling sync directly.

### Viewport

Viewport should:

- collect pointer/ray/modifier facts,
- call selection service/commands,
- render projected overlays,
- route shortcuts through command registry,
- host modal tool UI state.

Viewport should not:

- directly mutate scene selection,
- directly mutate modeling selection,
- directly commit history,
- compose semantic overlays in panel code.

### Context Menus And Popup X

Context menus should:

- resolve target under cursor,
- optionally preserve existing selection according to explicit policy,
- show command descriptors,
- execute command IDs.

Popup X must execute the same delete command as keyboard Delete/Canc.

### Transform Gizmo

Transform gizmo should:

- own hit/draw interaction only,
- ask `TransformInteractionService` for drag plans,
- use preview transactions while dragging,
- commit one editor transaction,
- cancel preview without history.

Shift duplicate should:

- execute duplicate selection as part of the transform transaction,
- preserve hierarchy and materials,
- update selection to duplicated roots according to explicit policy,
- undo/redo as one history item.

## Build System Plan

### Mechanical Migration

1. Create new `src/mesh/polygon`, `src/modeling`, `src/mesh/geometry`, `src/io/qdr`, and `src/editor/*` directories.
2. Move former SDK sources into these directories.
3. Update includes mechanically.
4. Keep temporary namespace aliases if necessary for staged compilation.
5. Remove SDK package CMake files from the app build.
6. Add migrated sources directly to app CMake targets or app-owned libraries.
7. Remove `QUADER_MODELING_SDK_DIR`.
8. Remove SDK test preset or repurpose it into normal app test targets.
9. Remove `sdk/QUADER_NATIVE_SOURCE.txt` after migration history is recorded in git.

### CMake Target Shape

Proposed internal targets:

- `quader_core`
- `quader_geometry`
- `quader_modeling_polygon`
- `quader_modeling_workspace`
- `quader_editor_document`
- `quader_editor_commands`
- `quader_editor_history`
- `quader_editor_selection`
- `quader_editor_projection`
- `quader_io`
- `quader_app`
- `quader_tests`

This gives clear compile boundaries without pretending there is an external SDK.

### Third-Party Dependencies

Third-party dependencies should not be dumped into `src/modeling`.

Use one of:

- existing root-level dependency management,
- `third_party/`,
- CMake FetchContent only if allowed and pinned,
- vendored minimal source only with license approval.

Rules:

- verify license before moving or adding dependencies,
- avoid GPL, AGPL, LGPL with unclear obligations, SSPL, BUSL, copyleft/source-available dependencies unless explicitly approved,
- hide third-party types behind app-owned APIs.

## Namespace Plan

The old namespaces should not survive as public app architecture:

- `quader_editor_core`
- `quader_poly`
- `quader_io`
- `quader_host_contract`

Proposed app namespaces:

- `quader::core`
- `quader::geometry`
- `quader::polygon`
- `quader::modeling`
- `quader::editor`
- `quader::scene`
- `quader::assets`
- `quader::render`
- `quader::ui`
- `quader::io`

During migration, namespace aliases can be used temporarily to reduce patch size. The final code should not expose old SDK names in app-facing headers.

## Public API Plan

### Keep

Keep `QuaderEditorApi` as the main user/editor front door.

Keep:

- `editor.commands()`
- `editor.selection()`
- `editor.scene()`
- `editor.modeling()`
- `editor.object(handle)`
- `editor.mainViewport()`
- `editor.history()`

### Change Semantics

`editor.modeling()` should not mean "talk directly to a second editor session." It should mean "send modeling intent to the app editor command/modeling domain."

`editor.scene().select` should either:

- delegate to `editor.selection()`,
- or be restricted to document-internal tests.

`editor.modeling().executeAction` should become:

- deprecated public compatibility route through command registry,
- then removed after scripts/tests migrate to command IDs.

### Public API Testing

The public API should be the default behavior test surface:

```cpp
editor.modeling().createBox(...);
editor.object(object).rename("Wall").select();
editor.commands().execute(QuaderCommand::DeleteSelection);
editor.commands().execute(QuaderCommand::HistoryUndo);
```

Public tests should not reach into `SceneDocument`, `ModelingWorkspace`, or `AssetRegistry` except through invariant probes.

## Test Strategy

### Test Principle

The refactor needs more tests before and during the move, not after. The first safety net should be public behavior coverage for the exact bug class.

### Scenario Harness

Promote `BehaviorScenario` to the main public behavior DSL:

- owns `ProjectSession + QuaderEditorApi`,
- exposes `editor()` and `api()`,
- hides raw session access behind `InvariantProbe`,
- runs invariant checks after every mutation.

Add invariant helpers:

- `sceneGraphCoherent()`
- `selectionCoherent()`
- `modelingWorkspaceCoherent()`
- `polygonDocumentsCoherent()`
- `assetsCoherent()`
- `historyCoherent()`
- `projectionCoherent()`
- `revisionsCoherent()`

### Must-Have Group Tests

Add behavior tests for:

- create group from selected meshes,
- group empty selection no-op/failure policy,
- group single mesh policy,
- nested group create,
- ungroup simple group,
- ungroup nested group,
- delete selected group with Delete,
- delete selected group with Canc,
- delete selected group with popup X,
- delete selected group with context menu,
- delete selected group from outliner,
- delete selected child inside group,
- delete mesh while group selected stale modeling object exists,
- delete component selection while group mode is active should follow explicit policy,
- undo group create,
- redo group create,
- undo group delete,
- redo group delete,
- undo child delete restores hierarchy,
- duplicate group preserves hierarchy,
- duplicate group preserves transforms,
- duplicate group preserves materials,
- shift-drag duplicate group is one undo item,
- group transform undo/redo restores world transforms,
- group material assignments preserve child material policy.

### Must-Have History Tests

Add behavior tests for:

- mixed scene/modeling chronology,
- redo clearing after new document edit,
- redo clearing after new modeling edit,
- undo after failed command keeps stacks intact,
- redo after failed command keeps stacks intact,
- repeated undo/redo does not change IDs unexpectedly,
- history labels match command descriptors,
- canUndo/canRedo reflect only public editor history,
- no hidden modeling redo is visible after app redo stack is empty,
- preview cancel creates no history item,
- preview commit creates exactly one history item.

### Must-Have Selection Tests

Add behavior tests for:

- viewport click mesh in object mode,
- viewport click group in group mode,
- outliner click group,
- outliner click mesh while in component mode,
- context menu right-click selected target preserves selection,
- context menu right-click unselected target explicit policy,
- click empty space explicit policy,
- switch object -> vertex -> group -> object,
- select all in group mode,
- select all in object mode,
- invert selection in object mode,
- stale runtime modeling selection cannot delete wrong scene target,
- selected target always points to existing editor object or component.

### Must-Have Command Routing Tests

Add tests for:

- every command descriptor has unique ID,
- every typed `QuaderCommand::*` has descriptor or explicit dynamic backing,
- every shortcut routes to command registry,
- context menu uses command registry,
- outliner actions use command registry,
- popup X uses DeleteSelection command,
- command API and UI routes produce identical state for delete/group/ungroup/duplicate.

### Property Tests

Add deterministic generated sequence tests:

- random create box,
- random select entity/group/component,
- random group/ungroup,
- random delete,
- random duplicate,
- random transform,
- random material assign,
- random undo/redo,
- random save/load if cheap.

After every step:

- run invariants,
- check no selected target references missing state,
- check no mesh entity references missing polygon/modeling data,
- check no polygon document lacks a scene owner unless intentionally detached,
- check history canUndo/canRedo consistency.

Use fixed seeds and print seed on failure.

### Static Guardrail Tests

Add or update source guardrails:

- UI cannot include `core/UndoRedoManager.h` except transitional allowlist,
- UI cannot include `document/DocumentCommandService.h` except transitional allowlist,
- UI cannot call `modeling.setSelectionMode`,
- UI cannot call `modeling.pickSelect`,
- UI cannot call `modeling.syncObjectSelectionFromScene`,
- UI cannot call `modeling.syncSceneSelectionFromModeling`,
- UI cannot call `editor.modeling().executeAction` for command-backed actions,
- API cannot expose old SDK session types,
- public behavior tests cannot include low-level document/modeling headers except invariant probes,
- no `canUndo() || modeling.canUndo()` pattern,
- no `sdk/` includes after migration,
- no source file under `src/` includes `quader_editor_core/`, `quader_poly/`, `quader_io/`, or `quader_host_contract/` after final migration.

## Migration Phases

### Phase 0: Freeze The Contract And Add Characterization Tests

Goal:

- make the bug class impossible to hide during refactor.

Tasks:

- add public behavior tests for group delete surfaces,
- add history chronology tests,
- add selection coherence tests,
- add static guardrails for direct UI mutation paths,
- record current failing or fixed behavior explicitly,
- define transition allowlists.

Exit criteria:

- current test suite passes or expected failing characterization tests are marked as planned failures only if the test framework supports that cleanly,
- every critical bug path has a named test,
- no new refactor work starts without tests for delete/history/selection.

### Phase 1: Mechanical SDK Absorption

Goal:

- remove the physical and build-system separation without changing behavior.

Tasks:

- create target directories under `src/`,
- move former SDK package files into the proposed app-owned modules,
- update CMake,
- update include paths,
- keep temporary namespaces/aliases if needed,
- move old SDK tests into app test target,
- remove SDK test preset once coverage is integrated,
- remove top-level `sdk/` folder.

Exit criteria:

- app builds,
- all old SDK tests run as app tests,
- no top-level `sdk/` folder remains,
- no app build path references `QUADER_MODELING_SDK_DIR`.

### Phase 2: Introduce EditorDocument And Stable Identity

Goal:

- create one app-facing document owner.

Tasks:

- add `EditorDocument`,
- add stable `EditorObjectId`,
- add object store,
- wrap `SceneDocument`,
- add polygon document ownership records,
- move `ModelingObjectRegistry` into editor document/runtime mapping,
- update creation/deletion code to use stable IDs,
- add invariant checks for object/entity/polygon links.

Exit criteria:

- every mesh entity has a stable editor object ID,
- every polygon document has a stable owner,
- runtime workspace IDs are internal,
- undo/redo tests preserve stable IDs or remap through explicit deltas.

### Phase 3: Replace Public History

Goal:

- one public history.

Tasks:

- add `EditorHistoryService`,
- route `QuaderHistoryApi` and `EditorCommandService` through it,
- wrap modeling mutations as editor transactions,
- remove modeling undo/redo fallback,
- replace `ModelingHistoryCommand`,
- remove split-history query patterns,
- update tests.

Exit criteria:

- no public call can reach modeling undo/redo,
- `canUndo/canRedo` is app history only,
- mixed scene/modeling chronology tests pass,
- every public mutator creates zero or one explicit history item according to command contract.

### Phase 4: Replace Selection Sync With Selection Service

Goal:

- one selection authority.

Tasks:

- add `EditorSelectionService`,
- add typed selection targets,
- route scene selection API through it,
- route modeling selection API through it,
- route viewport picking through it,
- route outliner selection through it,
- make old sync methods private,
- remove UI selection workarounds.

Exit criteria:

- UI cannot call selection sync,
- UI cannot directly select scene entities,
- group mode does not require switching modeling mode internally from UI,
- stale runtime modeling selection cannot delete wrong scene targets.

### Phase 5: Command Registry Unification

Goal:

- one command vocabulary.

Tasks:

- create or finish `EditorCommandRegistry`,
- migrate modeling actions to command handlers,
- migrate context menu to command IDs,
- migrate popup X to DeleteSelection command,
- migrate outliner actions to command IDs,
- migrate shortcuts to typed command IDs,
- consolidate command/action metadata.

Exit criteria:

- command routing tests pass,
- delete command surface parity tests pass,
- no UI direct `executeAction` for command-backed operations,
- command palette/menu/shortcut/API all agree on availability.

### Phase 6: Split The God Classes

Goal:

- shrink `ModelingSessionService`, `ViewportPanel`, and `TransformGizmoController`.

Tasks:

- extract modeling workspace,
- extract operation executor,
- extract picking service,
- extract overlay projection,
- extract transform interaction service,
- extract modal tool session service,
- extract mesh projection service,
- extract hierarchy duplicate command,
- move scene batch construction into document/editor command builders.

Exit criteria:

- `ModelingSessionService` is deleted or reduced to a temporary compatibility facade,
- `ViewportPanel` no longer owns semantic overlay or mutation logic,
- `TransformGizmoController` no longer owns history commits or group duplication,
- dependency guardrails pass.

### Phase 7: Projection Cleanup

Goal:

- derived state becomes visibly derived.

Tasks:

- replace sync calls with projection invalidation,
- remove projection services that create/delete scene entities during normal updates,
- make mesh assets derive from polygon documents,
- make overlays derive from selection/workspace state,
- make picking caches rebuildable.

Exit criteria:

- no bidirectional scene/modeling sync,
- projection rebuild after undo/redo produces identical render/cache state,
- projection services do not mutate editor history or selection.

### Phase 8: IO And Persistence

Goal:

- save/load the new editor document correctly.

Tasks:

- move QDR IO into `src/io/qdr`,
- update format to persist editor document, hierarchy, stable IDs, assets, polygon docs, material bindings, selection if desired,
- add migration loader for old files if needed,
- add save/load roundtrip tests for groups and modeling objects.

Exit criteria:

- group hierarchy survives save/load,
- materials survive save/load,
- polygon topology survives save/load,
- stable IDs survive save/load where expected,
- undo stack policy after load is explicit.

### Phase 9: Delete Transitional Shims

Goal:

- remove old architecture completely.

Tasks:

- delete old namespace aliases,
- delete old action compatibility routes,
- delete old selection sync public methods,
- delete old SDK history code or isolate test-only internals,
- delete transition allowlists in guardrail tests,
- delete stale comments that mention SDK as separate app dependency.

Exit criteria:

- no `sdk/` path in repo,
- no old SDK include paths,
- no public old SDK namespaces,
- no split history,
- no bidirectional selection sync,
- no UI direct mutation paths.

## Risk Register

### Risk: Moving Everything At Once Breaks Everything

Mitigation:

- split into phases,
- do mechanical move separately from semantic refactor,
- keep tests passing at each phase,
- use temporary namespace aliases only inside a phase,
- delete aliases only after behavior migration.

### Risk: Polygon Operations Depend On Session History

Mitigation:

- first wrap current snapshots in editor transactions,
- then replace session history with deltas,
- keep low-level tests for operation behavior.

### Risk: Selection Behavior Changes Accidentally

Mitigation:

- public behavior tests for every selection mode,
- source guardrails against direct UI sync/mutation,
- typed selection target state,
- explicit right-click/empty-click policies.

### Risk: Material And Mesh Assets Drift During Undo/Redo

Mitigation:

- include asset deltas in editor transactions,
- projection invalidation after every transaction,
- tests for texture/material restoration after group delete undo.

### Risk: Identity Drift

Mitigation:

- introduce stable editor IDs before deleting bridge code,
- runtime IDs become private,
- transaction deltas restore stable IDs,
- invariant tests catch missing links.

### Risk: Render Or Picking Performance Regresses

Mitigation:

- projection services can still cache aggressively,
- caches are invalidated by document revisions,
- hot geometry stays in low-level modules,
- avoid moving per-frame math into UI glue.

### Risk: Unity/Godot Parity Gets Harder

Mitigation:

- the native app can still expose a clean app-core library later if needed,
- keep domain modules clean and host-independent where reasonable,
- document behavior contracts in tests and command descriptors,
- do not mix UI/platform concerns into polygon/geometry modules.

## Immediate Kill List

These patterns should be removed during the refactor:

- public app history OR modeling history fallback,
- `ModelingHistoryCommand` undoing the top SDK stack entry,
- UI calling `modeling.setSelectionMode` to preserve group mode,
- UI calling `syncObjectSelectionFromScene`,
- UI calling `syncSceneSelectionFromModeling`,
- UI calling `modeling.pickSelect`,
- context menu calling `editor.modeling().executeAction`,
- outliner directly mutating scene selection,
- popup X having its own delete path,
- `QuaderQueryApi` reporting history by OR-ing app and modeling stacks,
- public API headers exposing former SDK session types,
- bridge/projection code creating scene entities as a side effect of routine sync,
- duplicated command metadata across action catalogs, context menus, and command descriptors.

## Success Criteria

The refactor is done when all of these are true:

- the top-level `sdk/` folder is gone,
- former SDK code lives under `src/` in app-owned modules,
- there is one public editor history,
- there is one selection authority,
- there is one command registry for user-visible actions,
- Delete/Canc/popup X/context menu/outliner/API all call the same delete command,
- groups are first-class editor document nodes,
- polygon modeling does not own scene hierarchy,
- UI code does not directly mutate scene/modeling state for user commands,
- render/picking/overlay state is derived projection state,
- public API scenario tests cover group, selection, delete, transform, material, undo, redo, and save/load behavior,
- static guardrails prevent reintroducing split authority.

## Verification Commands

During implementation, run the normal app build/test lane after each phase:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
ctest --preset vs2026-debug --output-on-failure
```

After SDK absorption, there should no longer be a separate SDK test preset required for native app correctness. The former SDK tests should run under the app test target.

For focused iteration:

```powershell
cmake --build --preset vs2026-debug --target quader_tests
build\vs2026\Debug\quader_tests.exe --gtest_filter=*Group*:*History*:*Selection*
```

Before declaring the refactor complete:

```powershell
cmake --build --preset vs2026-debug --target quader
ctest --preset vs2026-debug --output-on-failure
```

## Final Position

The old `sdk/` folder should not survive as a separate root in the native app. The useful code inside it should be absorbed into `src/`, split by real ownership, and made subordinate to the native editor architecture.

The core architecture should be:

```text
UI / scripts / public API
        |
        v
QuaderEditorApi
        |
        v
EditorCommandRegistry + EditorSelectionService
        |
        v
EditorHistoryService executes EditorTransactions
        |
        v
EditorDocument owns scene hierarchy, groups, stable object identity, assets, selection
        |
        v
Polygon modeling modules apply topology changes as transaction deltas
        |
        v
Projection services rebuild mesh assets, picking caches, overlays, render state
```

That is the architecture that ends the current scene-vs-SDK ambiguity. There is still a polygon modeling domain, but it is not a second editor. There is still geometry/tool/runtime code, but it is not a parallel app. The app has one brain, and the former SDK code becomes part of its body.
