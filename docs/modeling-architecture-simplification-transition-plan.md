<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling Architecture Simplification Transition Plan

Internal architecture plan generated from a 10-subagent simplification audit plus a local
repository pass on 2026-07-04.

This document plans a transition toward modeling code that is easier to navigate from user intent.
The target experience is simple: a maintainer who wants to understand "how the Cut tool works"
should be able to open an obvious Cut entry point, follow a short route through named owners, and
end at the polygon operation without decoding historical layers.

This is not a plan to flatten all modeling code into one folder. The existing ownership boundaries
are mostly correct and should be preserved:

- viewport tools own per-frame input and modal viewport interaction;
- interaction logic owns pure modeling tool state and math;
- modeling runtime owns document/session mutation, preview state, and history integration;
- mesh/polygon owns deterministic topology and geometry algorithms;
- public API and command surfaces express user/editor intent.

The transition is to make those boundaries readable, narrower, and better named.

## Executive Summary

The current architecture is overcomplicated in four visible ways:

1. User-facing features do not have honest front doors. `src/editor/tools/interactive/cut_tool.hpp`
   is almost empty, while real Cut behavior lives in shared drawn-plane lifecycle code, runtime
   commands, editor runtime helpers, session operations, and polygon plane cut code.
2. Several files and classes are too broad. `session.hpp`, `session.cpp`,
   `runtime_command.cpp`, `session_tool_runtime.cpp`, `modeling_session_service.cpp`, and
   `modeling_session_services.cpp` hide many concepts behind generic names.
3. Some abstractions preserve old structure rather than real behavior. Examples include pass-through
   command tool subclasses, duplicated operation identity catalogs, and wrappers around internal
   singleton-style tool logic.
4. Tests and CMake still encode accidental shape. Some tests verify inheritance hierarchies or
   source snippets instead of user workflows, and CMake files group unrelated owners under broad
   "modeling" labels.

The migration should proceed in small, behavior-preserving slices. Start with Cut as the pilot
feature, because it shows nearly every problem: duplicate naming, shared viewport lifecycle, runtime
command indirection, session commit, preview projection, and polygon algorithm routing.

## Target Reader Experience

For a modal tool such as Cut, a reader should be able to follow this route:

```text
src/editor/tools/interactive/cut_viewport_tool.hpp
  -> src/editor/tools/base/drawn_plane_viewport_tool.hpp
  -> src/editor/interactions/modeling_tools/cut_tool_logic.hpp
  -> src/editor/modeling/runtime/runtime_command_cut.cpp
  -> src/editor/modeling/runtime/session_cut_operations.cpp
  -> src/mesh/polygon/operations/plane_cut_operation.cpp
```

The route should answer these questions in order:

| Question | Owner |
| --- | --- |
| How does the viewport dispatch pointer/finalize/mode-cycle input? | `src/editor/tools/**` |
| What state does the Cut tool store and how do points/preview requests update? | `src/editor/interactions/modeling_tools/**` |
| How is viewport input converted into runtime commands? | `src/editor/modeling/services/runtime_tool_command_builder.*` |
| How are runtime commands executed against current session state? | `src/editor/modeling/runtime/runtime_command_cut.cpp` |
| How does finalizing a Cut write history and call the document operation? | `src/editor/modeling/runtime/session_cut_operations.cpp` |
| How does the mesh actually split or discard geometry? | `src/mesh/polygon/operations/plane_cut_operation.cpp` |

The same pattern should be repeated for Mirror, Knife, Box, Poly, Insert Edge Loop, transform tools,
selection, and operation previews.

## Non-Goals

- Do not resurrect the old external SDK boundary as part of this simplification plan.
- Do not move all tool code into vertical feature folders if doing so violates owner boundaries.
- Do not add command aliases or compatibility names to paper over unclear naming.
- Do not change behavior while moving files. Preserve ordering, history, preview invalidation,
  selection sync, runtime status changes, hover state, and scene sync.
- Do not widen `ModelingSessionService` or `EditorSession` while trying to simplify them.
- Do not convert tests from behavior checks into more source-shape scans.

## Naming Rules

Use names that describe the layer, not just the feature:

| Concept | Naming Rule | Example |
| --- | --- | --- |
| Viewport controller | `<feature>_viewport_tool` | `cut_viewport_tool.hpp` |
| Shared viewport controller | `<concept>_viewport_tool` | `drawn_plane_viewport_tool.cpp` |
| Tool state and pure interaction logic | `<feature>_tool_logic` | `cut_tool_logic.cpp` |
| Runtime command handlers | `runtime_command_<feature>` | `runtime_command_cut.cpp` |
| Session mutation group | `session_<feature>_operations` | `session_cut_operations.cpp` |
| Polygon operation | concrete algorithm name | `plane_cut_operation.cpp` |
| Command builder | builder, not service | `runtime_tool_command_builder.cpp` |
| Operation preview | qualify the kind | `operation_preview`, `modal_tool_preview`, `transform_preview` |
| Selection | qualify the domain | `editor_selection`, `scene_selection`, `runtime_selection`, `polygon_selection`, `selection_overlay` |

Prefer "viewport", "runtime", "session", "polygon", "projection", and "metadata" in file names
when the same feature spans multiple layers.

## Current Pain Map

| Area | Problem | Primary Files |
| --- | --- | --- |
| Cut tool discoverability | `cut_tool.hpp` is a descriptor wrapper; behavior is elsewhere. | `src/editor/tools/interactive/cut_tool.hpp`, `src/editor/tools/base/drawn_plane_tool.cpp`, `src/editor/interactions/cut_tool.cpp` |
| Shared drawn-plane naming | Viewport lifecycle and interaction geometry both use `drawn_plane_tool`. | `src/editor/tools/base/drawn_plane_tool.*`, `src/editor/interactions/drawn_plane_tool.*` |
| Runtime command placement | Header lives under modeling runtime, implementation under interactions. | `src/editor/modeling/runtime/runtime_command.hpp`, `src/editor/interactions/runtime_command.cpp` |
| Editor runtime placement | Header lives under modeling runtime, implementation under interactions. | `src/editor/modeling/runtime/editor_runtime.hpp`, `src/editor/interactions/session_tool_runtime.cpp` |
| Huge session header | `session.hpp` mixes records, helper declarations, overlay/picking types, policies, and `EditorSession`. | `src/editor/modeling/runtime/session.hpp` |
| Overgrown modeling facade | `ModelingSessionService::Impl` owns too much runtime, scene sync, identity, command, overlay, and settings flow. | `src/editor/modeling/services/modeling_session_service.cpp` |
| Service bucket file | Multiple services are implemented in one large `modeling_session_services.cpp`. | `src/editor/modeling/services/modeling_session_services.cpp` |
| Operation identity duplication | Editor operations, instant operations, and polygon operation keys overlap. | `operation_schema.hpp`, `modeling_types.hpp`, `poly_operation_descriptor.hpp` |
| Command tool boilerplate | Many tiny command tool subclasses encode route policy only. | `src/editor/commands/modeling/*.cpp`, `modeling_command_tool_catalog.*` |
| Viewport activation routing | Shortcuts, menu activation, router state, and API activation overlap. | `viewport_panel.cpp`, `viewport_tool_runtime.cpp`, `quader_tools_api.cpp` |
| CMake ownership | Mesh, editor modeling, metadata, QDR, benchmarks, and tests share broad modules. | `cmake/QuaderModeling.cmake`, `CMakeLists.txt` |
| Test readability | Some tests freeze implementation shape rather than user behavior. | `modeling_tool_catalog_tests.cpp`, `viewport_tool_catalog_tests.cpp`, `modeling_session_service_split_tests.cpp` |

## Target Source Shape

This is an incremental target, not a single mechanical move.

```text
src/editor/modeling/
  README.md
  operations/
    editor_operation_descriptor.*
    operation_command.*
    operation_registry.*
  runtime/
    editor_runtime.hpp
    editor_runtime.cpp
    editor_runtime_cut.cpp
    editor_runtime_mirror.cpp
    runtime_command.hpp
    runtime_command.cpp
    runtime_command_cut.cpp
    runtime_command_mirror.cpp
    session.hpp
    editor_session.hpp
    session_types.hpp
    selection_types.hpp
    picking_types.hpp
    selection_overlay_types.hpp
    session_cut_operations.cpp
    session_mirror_operations.cpp
    session_object_operations.cpp
    session_selection_operations.cpp
  services/
    modeling_session_service.*
    modeling_runtime_state.*
    modeling_command_executor.*
    runtime_tool_command_builder.*
    modeling_selection_service.*
    modeling_overlay_service.*
    modeling_mesh_sync_service.*

src/editor/interactions/
  modeling_tools/
    cut_tool_logic.*
    mirror_tool_logic.*
    drawn_plane_interaction_logic.*
    knife_tool_logic.*
    box_tool_logic.*
  transform/
    transform_drag_math.*
    transform_gizmo_math.*
  selection/
    selection_box.*
    selection_edit.*

src/editor/tools/
  base/
    drawn_plane_viewport_tool.*
    modal_viewport_tool.*
  interactive/
    cut_viewport_tool.*
    mirror_viewport_tool.*
    knife_viewport_tool.*
    box_viewport_tool.*
    select_viewport_tool.*

src/editor/projection/
  modeling_tools/
    cut_preview_overlay_projection.*
    drawn_plane_overlay_projection.*
```

The first phase may keep compatibility headers at old paths while includes migrate.

## Transition Phases

### Phase 0 - Baseline Docs, Indexes, And Guardrails

Goal: make the intended destination visible before moving code.

Changes:

- Add `docs/modeling-source-index.md`.
- Add `src/editor/modeling/README.md`.
- Add or update `src/editor/interactions/README.md` to define "interaction logic" as pure tool
  state/math, not viewport controllers.
- Expand `src/mesh/polygon/README.md` with operation-name mapping.
- Add feature index rows for Cut Tool, Mirror Tool, Knife Tool, Box Tool, Bevel Edges, Selection,
  Operation Preview, Modal Tool Preview, and Transform Preview.
- Add naming guidance to the modeling README:
  `Tool`, `Action`, `Operation`, `Preview`, and selection domain qualifiers.
- Add guardrails that permit planned compatibility headers but fail on new ambiguous files such as
  another `drawn_plane_tool.cpp` without a layer qualifier.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "docs|guardrail|oop|build"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- A maintainer can find Cut, Bevel, Selection, and Preview from user terms in docs.
- Internal docs are listed in `docs/public-docs-examples.txt`.
- Guardrails describe the destination without blocking the first compatibility slice.

### Phase 1 - Cut Tool Pilot

Goal: prove the feature-front-door pattern on one complex tool without behavior changes.

Changes:

- Rename `src/editor/tools/interactive/cut_tool.hpp` to
  `src/editor/tools/interactive/cut_viewport_tool.hpp`.
- Rename `CutTool` viewport class to `CutViewportTool`.
- Rename `src/editor/tools/base/drawn_plane_tool.*` to
  `src/editor/tools/base/drawn_plane_viewport_tool.*`.
- Rename `DrawnPlaneTool` viewport class to `DrawnPlaneViewportTool`.
- Move or alias `src/editor/interactions/cut_tool.*` to
  `src/editor/interactions/modeling_tools/cut_tool_logic.*`.
- Move or alias `src/editor/interactions/drawn_plane_tool.*` to
  `src/editor/interactions/modeling_tools/drawn_plane_interaction_logic.*`.
- Extract Cut handlers from `runtime_command.cpp` into `runtime_command_cut.cpp`.
- Extract `EditorSession::commit_cut_tool`, `EditorSession::plane_cut`, and related free helpers
  into `session_cut_operations.cpp`.
- Rename editor-session free `plane_cut` helper to `apply_plane_cut_to_objects`; leave
  `quader_poly::plane_cut` unchanged as the polygon owner.
- Centralize `ModelingCutMode <-> quader_editor_core::CutMode` conversion in one modeling-owned
  helper. Keep `QuaderCutMode` conversions at the API boundary.
- Add a short Cut route note in `cut_viewport_tool.hpp` or the modeling source index.

Compatibility strategy:

- Keep forwarding headers at old include paths for one phase if include churn is too large.
- Do not change public command IDs, action IDs, shortcut IDs, or metadata IDs.
- Do not rename polygon `PlaneCutRequest`, `PlaneCutMode`, or `plane_cut_operation.cpp`.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "Cut|PlaneCut|DrawnPlane|ToolCatalog|EditorApi"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- Opening the Cut viewport file gives a clear route to the behavior owner and polygon operation.
- Cut tests pass without behavior updates.
- No new source-shape-only tests are added except migration guardrails.

### Phase 2 - Runtime File Split

Goal: make the runtime readable by concept.

Changes:

- Split `session.hpp` while keeping `session.hpp` as an umbrella:
  - `session_types.hpp`
  - `selection_types.hpp`
  - `picking_types.hpp`
  - `selection_overlay_types.hpp`
  - `editor_session.hpp`
- Move `src/editor/interactions/runtime_command.cpp` to
  `src/editor/modeling/runtime/runtime_command.cpp`.
- Keep the dispatcher in `runtime_command.cpp`; split feature handlers into files:
  - `runtime_command_cut.cpp`
  - `runtime_command_mirror.cpp`
  - `runtime_command_box.cpp`
  - `runtime_command_selection.cpp`
  - `runtime_command_transform.cpp`
- Move `src/editor/interactions/session_tool_runtime.cpp` to
  `src/editor/modeling/runtime/editor_runtime.cpp`.
- Split editor runtime implementation by feature after the move:
  - `editor_runtime_modal_tools.cpp`
  - `editor_runtime_transform_drag.cpp`
  - `editor_runtime_box.cpp`
  - `editor_runtime_clipboard.cpp`
- Split `selection_policy.cpp` into rectangle selection, mode conversion/promotion, hit target
  selection, feedback/overlay lookup, and center planning files.
- Split `session_picking.cpp` into source-wire picking, hit collection, pick-cycle planning,
  hover update, and selection application.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "Runtime|Selection|Picking|Overlay|Command"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- `session.hpp` no longer defines every runtime DTO and `EditorSession` in one file.
- Runtime command implementation lives beside its header.
- Editor runtime implementation lives beside its header.
- Feature handlers can be opened by name.

### Phase 3 - ModelingSessionService Diet

Goal: make `ModelingSessionService` a compatibility facade, not a hidden modeling kernel.

Changes:

- Split `modeling_session_services.cpp` into implementation files that match existing service
  headers:
  - `modeling_selection_service.cpp`
  - `modeling_overlay_service.cpp`
  - `modeling_interaction_settings_service.cpp`
  - `modeling_tool_settings_service.cpp`
  - `modeling_operation_settings.cpp`
  - `modeling_context_action_service.cpp`
- Introduce `ModelingRuntimeState` or an equivalent narrow owner for:
  - `EditorSession`
  - `EditorRuntime`
  - `ModelingObjectRuntimeMap`
  - scene sync validity
  - app/runtime selection mode bridge
  - memento restore identity
- Make `ModelingCommandExecutor` the single post-mutation path for:
  - execute runtime command or operation;
  - sync runtime meshes to scene/assets;
  - project selection;
  - invalidate overlay/picking caches;
  - update dirty flags and command result metadata.
- Remove bespoke post-mutation sync blocks from mirror, flip, material, transform, and direct
  runtime operation helpers after they route through the executor.
- Shrink `ModelingTransformDragPort` into narrower collaborators:
  - sync/history
  - picking/cache
  - duplicate identity
  - transform preview
- Consolidate overlay frame and cache key production under modeling ownership so UI does not rebuild
  the same invalidation contract.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "ModelingSessionService|TransformDrag|Overlay|SelectionProjection|History"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- `ModelingSessionService::Impl` has fewer owner fields and less mutation logic.
- Most public methods are compatibility forwarding to focused owners.
- One executor owns post-mutation sync and invalidation.

### Phase 4 - Command Tool Simplification

Goal: remove descriptor-only command classes and make command routing explicit data.

Changes:

- Replace the current command tool subclass ladder with route-driven catalog specs:

```cpp
struct CommandToolSpec {
  CommandToolDescriptor descriptor;
  CommandToolRoute route;
  EditorOperationId operation_id;
  ModelingActionId action_id;
  CommandVariant variant;
};
```

- Keep a small runtime object such as `CatalogCommandTool` only if existing APIs require a
  `CommandTool` object.
- Remove pass-through files after their behavior is represented by catalog rows:
  - assignment/material wrappers if purely routed;
  - copy/paste wrappers if purely routed;
  - flip/mirror wrappers if purely routed;
  - simple selection wrappers if purely routed.
- Keep real behavior owners:
  - scene-aware delete;
  - box preview commit;
  - clipboard behavior if it owns custom data transfer;
  - repeat last action;
  - material assignment if it owns scene/asset-specific checks.
- Make `delete_selection` a single explicit route that chooses scene-aware object deletion or
  runtime component deletion.
- Reduce `modeling_command_tool_catalog.hpp` to:
  - `modeling_command_tools()`
  - `find_modeling_command_tool()`
- Update tests that assert subclass shape to assert route, descriptor, availability, and behavior.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "Command|CommandTool|ModelingToolCatalog|ProgrammaticApi"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- Descriptor-only command `.cpp` files are gone.
- Command routing is inspectable in one catalog.
- Behavior-owning custom commands remain explicit.

### Phase 5 - Operation Identity And Metadata Simplification

Goal: use one editor/runtime operation descriptor as the editor-facing source of truth.

Changes:

- Introduce or strengthen `EditorOperationDescriptor` with:
  - stable operation id;
  - label;
  - settings kind;
  - preview transaction kind;
  - execution hook or route;
  - option descriptors;
  - public command/action metadata when applicable;
  - availability policy.
- Derive these from the descriptor:
  - operation registry;
  - operation command schema;
  - operation settings extraction;
  - preview mapping;
  - host operation descriptors;
  - panel/settings metadata where possible.
- Narrow or retire `ModelingInstantOperationId`.
- Catalog-owned instant command tools should hold `EditorOperationId` directly for standard runtime
  operations.
- Keep polygon `PolyOperationKey` only as mesh-owned descriptor/test/doc metadata. Do not make editor
  commands depend on polygon registry identity.
- Move action variants out of `host_contract.cpp` into modeling action descriptors. For example,
  "Bridge" and "Interpolated Bridge" should be action variants over one operation owner, not
  separate host-owned sources of truth.
- Add helper boundaries:
  - apply a polygon function to selected objects under the current selection mode;
  - update `EditorSession` content/selection bookkeeping from a command result.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "Operation|Metadata|EditorMetadata|Preview|Instant"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- Operation label/settings/preview/action metadata is no longer independently handwritten in many
  places.
- Polygon operations remain simple deterministic functions.
- Editor operations remain the owner of editor-level routing, preview, history, and scene sync.

### Phase 6 - Viewport Intent Routing

Goal: make input, shortcut, and tool activation flow through one public/editor intent path.

Changes:

- Extract `ViewportInputResolver` from `viewport_panel.cpp`.
  - input frame in;
  - semantic actions, router shortcuts, command-consumed flag out.
- Rename `ViewportInputFrameBuilder` toward `ViewportPhysicalInputFrameBuilder` if that better
  matches responsibility.
- Change `ViewportInputRouter` so tool shortcuts produce activation requests instead of owning
  activation truth.
- Execute tool activation only through `editor.tools().activate(...)` or typed command execution.
- Sync router/modal state from `modeling.tool_status()` after activation.
- Extract `ViewportPaneInteractionCoordinator::run_frame` from `draw_pane_contents`.
- Split `ViewportInteractionFrame` into:
  - read-only `ViewportInteractionInput`;
  - returned `ViewportInteractionEffects`.
- Remove mutable `ViewportInputRouter&` from selection and gizmo interaction frame data.
- Consolidate point-light scene gizmo pick and visual projection into one owner that generates both
  overlay additions and pick candidates.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "ViewportInput|ViewportInteraction|ViewportTool|Keybinding|SelectionFallback"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- Menu, shortcut, and API tool activation share one owner path.
- `viewport_panel.cpp` is a coordinator of UI drawing, not the owner of input policy.
- Interaction code returns effects instead of mutating router state through a frame struct.

### Phase 7 - CMake And Target Ownership

Goal: make build files enforce the same source ownership that the code claims.

Changes:

- Split `cmake/QuaderModeling.cmake` by owner:
  - `cmake/QuaderMesh.cmake`
  - `cmake/QuaderEditorModeling.cmake`
  - `cmake/QuaderEditorTools.cmake`
  - `cmake/QuaderQdr.cmake`
  - `cmake/QuaderModelingDomainTests.cmake`
- Rename targets where practical while keeping compatibility aliases during migration:
  - `quader_mesh_geometry` aliasing current `quader_geometry`;
  - `quader_mesh_polygon` aliasing current `quader_modeling_polygon`;
  - `quader_editor_modeling_runtime`;
  - `quader_editor_modeling_services`;
  - `quader_editor_modeling_commands`.
- Replace the polygon source glob with explicit source lists.
- Add source-prefix target guardrails:
  - `src/mesh/**` only in mesh targets;
  - `src/io/qdr/**` only in QDR targets;
  - `src/editor/tools/**` only in tool/viewport targets;
  - no concrete `*_tool.cpp` under `src/editor/interactions`.
- Make modeling-domain test categories use category-specific include dirs and link dependencies.

Verification:

- `cmake --preset vs2026`
- `ctest --preset vs2026-debug --output-on-failure -R "BuildGuardrail|Dependency|Oop"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- CMake target names communicate ownership.
- Source-prefix guardrails catch new drift.
- Polygon tests cannot accidentally include editor runtime headers.

### Phase 8 - Test Suite Readability

Goal: tests should explain behavior first and protect architecture second.

Changes:

- Keep these as strong owner specs:
  - polygon plane cut tests;
  - runtime commit cut tests;
  - cut tool state tests;
  - editor API cut setting tests.
- Split broad cut command tests into workflow-sized cases:
  - first click captures targets and starts line;
  - second click completes plane and builds preview;
  - dragging endpoint or line moves the cut plane;
  - changing keep mode invalidates preview;
  - command click sequence builds preview without history;
  - finalize commits one history entry;
  - missing selection rejects without mutation.
- Move source-scan tests that pin implementation shape into guardrail files.
- Replace inheritance hierarchy assertions with route/behavior assertions.
- Fix `BehaviorScenario::activate` so public/editor intent tests can activate tools.
- Add `tests/modeling_cut_workflow_tests.cpp` after `BehaviorScenario` can drive tool activation and
  narrow cut events.
- Consolidate duplicate event helper setup, especially drawn-plane tool events, into
  `tests/support/modeling_test_events.*`.

Verification:

- `ctest --preset vs2026-debug --output-on-failure -R "Cut|Workflow|BehaviorScenario|ModelingToolCatalog|ViewportToolCatalog"`
- `cmake --build --preset vs2026-debug --target quader`

Done criteria:

- "How does Cut work?" can be answered from tests without reading one giant aggregate command test.
- Guardrails still protect boundaries, but workflow tests own behavior.

### Phase 9 - Repeat The Pattern Across Modeling Features

Goal: apply the Cut pilot to the rest of modeling without creating new architecture.

Feature order:

1. Mirror, because it shares drawn-plane lifecycle with Cut.
2. Box, because it has modal tool state plus preview/commit behavior.
3. Knife, because it is complex and currently has large interaction files.
4. Insert Edge Loop, because it mixes hover, drag, and runtime selection.
5. Poly, because it has modal state and commit semantics.
6. Move/Rotate/Scale/Extend/Extrude transform tools, because transform drag ownership needs to be
   narrowed.
7. Select tool and selection overlays, because selection is central and behavior-sensitive.
8. Operation previews, because they currently overlap with modal tool previews.

For each feature:

- Add or update source-index row.
- Create/rename viewport front door.
- Move pure state/math to a feature-named interaction logic file.
- Extract runtime command handlers.
- Extract session operation groups.
- Keep polygon operation files under `src/mesh/polygon/operations`.
- Add or split behavior tests by user workflow.
- Delete compatibility headers only after includes are migrated.

Verification:

- Run feature-specific tests first.
- Run affected guardrails.
- Build `quader`.
- Run full `ctest --preset vs2026-debug` after every two or three feature migrations or before
  larger merge points.

Done criteria:

- Every modal modeling tool has a plain-language front door.
- `src/editor/interactions` no longer contains concrete viewport controllers.
- Runtime command files and session operation files are navigable by feature.

### Phase 10 - Compatibility Cleanup

Goal: remove transitional scaffolding after all call sites have moved.

Changes:

- Delete old forwarding headers and compatibility aliases.
- Remove old target aliases only after downstream project files and tests are migrated.
- Remove dead-looking runtime commands only after confirming they are unused or replacing them with
  intentional public routes. Candidate: `UpdateCutPreview`.
- Remove unused action tool wrappers if public tool activation is the intended owner.
- Remove duplicated conversion helpers.
- Remove source-scan tests that only protected migration compatibility.
- Tighten guardrails:
  - no new broad `session_*` grab bags;
  - no `Service` suffix for static builders;
  - no pass-through command tool subclasses;
  - no layer-ambiguous file names such as `drawn_plane_tool.cpp`.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug`
- `ctest --preset vs2026-debug`
- Manual smoke only for GPU/window behavior if touched.

Done criteria:

- No compatibility headers remain for renamed modeling files.
- Guardrails enforce the new source shape.
- The code map and modeling source index match actual files.

## First Implementation Slice

Start with Phase 0 and the lowest-risk part of Phase 1:

1. Add `docs/modeling-source-index.md`.
2. Add `src/editor/modeling/README.md`.
3. Add Cut source-index row and route note.
4. Centralize Cut mode conversion.
5. Rename viewport-only Cut and drawn-plane classes/files with forwarding headers.
6. Update includes and CMake.
7. Run focused Cut/tool catalog tests.
8. Build `quader`.

This slice is valuable even before deeper refactors because it makes the existing route explicit and
sets the naming standard for the remaining transition.

## Risk Register

| Risk | Mitigation |
| --- | --- |
| Header split causes broad include churn. | Keep umbrella headers first, then migrate includes by package. |
| Behavior changes during file moves. | Move code mechanically first; only rename symbols after tests pass. |
| Guardrails block transitional state. | Add temporary allowlists with removal dates or phase labels. |
| Cut pilot overfits to one feature. | Apply the same pattern immediately to Mirror after Cut. |
| Tests keep enforcing old implementation shape. | Convert tests from inheritance/source scans to route and behavior assertions before deleting old files. |
| CMake target split breaks broad include assumptions. | Split targets after file moves, then reduce include dirs in small steps. |
| Public command IDs drift. | Do not rename public command/action/shortcut IDs in this migration. |
| UI activation semantics change accidentally. | Add behavior tests around menu, shortcut, command, and API activation before changing router ownership. |

## Verification Matrix

Use the repo standard timeout of at least 10 minutes for build and test commands.

| Change Type | Required Verification |
| --- | --- |
| Docs/index only | `cmake --build --preset vs2026-debug --target quader` |
| File rename only | Focused package tests plus `quader` build |
| Runtime command split | Runtime command tests, feature tests, `quader` build |
| Session operation split | Operation tests, history tests, feature tests, `quader` build |
| Service/facade split | Modeling session service tests, selection projection tests, overlay tests, `quader` build |
| Command catalog simplification | Command API tests, command tool catalog tests, programmatic API guardrails, `quader` build |
| Operation descriptor simplification | Operation tests, editor metadata tests, preview tests, `quader` build |
| Viewport routing changes | Viewport interaction tests, keybinding tests, tool catalog tests, `quader` build |
| CMake target split | `cmake --preset vs2026`, dependency/build guardrails, full or broad `ctest`, `quader` build |

## Final Destination Checklist

- [ ] `Cut` has a readable route from viewport front door to polygon operation.
- [ ] `Mirror`, `Knife`, `Box`, `Poly`, `InsertEdgeLoop`, selection, and transforms follow the
      same route pattern.
- [ ] `session.hpp` is an umbrella, not the owner of every runtime type.
- [ ] Runtime command implementation lives under `src/editor/modeling/runtime`.
- [ ] Editor runtime implementation lives under `src/editor/modeling/runtime`.
- [ ] `src/editor/interactions` contains interaction logic/math, not viewport controller classes.
- [ ] `ModelingSessionService` is a compatibility facade over focused owners.
- [ ] Command tool routing is data-driven for descriptor-only commands.
- [ ] Editor operation metadata has one editor-facing source of truth.
- [ ] Polygon operations remain deterministic free-function owners under `src/mesh/polygon`.
- [ ] Viewport activation flows through one public/editor intent path.
- [ ] CMake target names and source lists reflect real owners.
- [ ] Tests read as user workflows where behavior matters.
- [ ] Guardrails protect the new shape without pinning accidental implementation details.
