<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Standalone Editor Extraction Plan

Internal architecture plan generated from a 10-agent audit on 2026-07-03.

This plan has two linked goals:

- reduce source file count where the repo is genuinely over-filed;
- extract reusable, app-owned libraries for navigation, modeling, and transform gizmos so Quader's
  modeling suite can later be embedded in Unity, Godot, or another host without reimplementing the
  same editor mechanics.

This is not a plan to restore the old SDK boundary. The reusable libraries stay Quader-owned and
native-app-compatible. Unity and Godot should be adapters over stable Quader contracts, not owners
of Quader internals.

## Summary Verdict

The codebase is complex, but it is not uniformly over-engineered. There are two different problems:

- Some modules are too wide, especially `ModelingSessionService` and viewport/render/UI
  composition paths.
- Some behavior is over-filed, especially descriptor-only viewport tool wrappers and tiny modeling
  command wrapper classes.

The strongest reusable seams already exist:

- `quader_geometry` and `quader_modeling_polygon` are mostly clean modeling core targets.
- Most of `quader_modeling_runtime` is plausible reusable editor/modeling runtime code.
- `ViewportCameraController`, viewport input frame types, and `ViewportWorkspaceService` are close
  to headless navigation/workspace code.
- `TransformGizmoFrame`, gizmo frame building, gizmo picking, and move/rotate/scale drag math are
  close to reusable gizmo core code.
- `ViewportOverlayFrame` is already data-only, but it lives under `src/render`, which forces
  editor/modeling code to depend on render package naming.

The plan should not start by moving hundreds of files. Start by adding guardrails and neutral
contracts, then extract narrow libraries while preserving current behavior, tests, history, cache
invalidation, and overlay invariants.

## Existing Plans To Reuse

| Document | Use In This Plan | Do Not Copy Wholesale |
| --- | --- | --- |
| `docs/architecture_migration_plan.md` | Owner rules, CMake target split direction, one mutation path, render snapshots, verification map. | Mesh ID reform, QDR determinism, and ADR templates are broader backlog. |
| `docs/ui_renderer_decoupling_plan.md` | UI/render/platform adapters, snapshots, fake/null render backend, target split scaffolding. | The full Qt/custom-renderer replacement scope is larger than this extraction. |
| `docs/overlay-handling.md` | Canonical overlay behavior and regression map. | Do not flatten overlay behavior into this plan; keep that doc authoritative. |
| `docs/absorbed_modeling/README.md` | Confirms former SDK code is now native app-owned. | Do not reintroduce a copied external SDK authority. |
| `docs/api-overview.md` | Current public API baseline and standalone editor entry points. | Do not expose the C++ public API as a binary plugin ABI. |

## Current Baseline

File shape from the audit:

- `rg --files` sees about 1,503 tracked files, including `src` 669, `thirdparty` 573, `tests` 180,
  and `resources` 35.
- `src/editor` is the largest package at 270 files.
- `src` has 442 `.cpp` files and 378 `.hpp` files in the scanned repo areas.
- About 238 source C++ files are 50 lines or shorter, and about 61 are 20 lines or shorter.
- The largest source file is `src/editor/modeling/services/modeling_session_service.cpp`, roughly
  166 KB.

The important takeaway is that file reduction has to attack both ends:

- remove or merge tiny wrapper files that do not own behavior;
- split or narrow wide owner facades only after tests and dependency direction are strong enough.

## Target Library Shape

The end state should be a small set of Quader-owned libraries plus thin engine adapters.

```text
quader_foundation
  math types, stable ids, diagnostics shims, hashing/signature helpers

quader_modeling_core
  geometry, polygon document, polygon operations, operation descriptors

quader_modeling_runtime_core
  editor modeling session/runtime, runtime operation commands, selection, picking,
  previews, runtime tool state, compiled mesh output

quader_viewport_navigation
  camera state/controller, projection/ray helpers, view-cube primitives/picking,
  navigation input router, workspace/pane state

quader_transform_gizmo_core
  gizmo visual spec, frame generation, picking, drag math, event state machine,
  primitive output

quader_overlay_contracts
  overlay frame/camera/layer/style/source records and backend-neutral overlay mesh plans

quader_editor_adapter
  Quader app adapters over scene, assets, history, commands, selection, render snapshots,
  public API, and native editor behavior

quader_embed
  embeddable facade for engine plugins, backed by Quader public APIs and snapshots

unity_quader_adapter / godot_quader_adapter
  thin engine-specific wrappers over quader_embed
```

`quader_embed` is the integration seam for Unity and Godot. It should wrap `QuaderApi`,
`QuaderEditorApi`, command APIs, metadata catalogs, event streams, render/export snapshots, and
project IO behind ABI-stable handles and DTOs.

Do not expose raw `QuaderEditorApi` or `QuaderApi` as the Unity/Godot plugin ABI. They are useful
internal C++ APIs, but their headers use C++ standard-library types and ownership conventions that
are not binary-stable for Unity C# P/Invoke or cross-toolchain engine plugins.

## Product Boundaries

### Modeling Core

Reusable:

- `src/mesh/geometry/**`
- `src/mesh/polygon/document.hpp`
- `src/mesh/polygon/poly_operation*.hpp`
- `src/mesh/polygon/operations/**`
- operation descriptors and registries
- compiled mesh output records
- optional QDR document/runtime IO targets

Keep app-side:

- `ModelingSessionService`
- scene and asset synchronization
- document command services
- editor history integration
- native selection service integration
- public `QuaderModelingApi` implementation
- command-palette and shortcut command wrappers
- full `host_contract` metadata catalog

First extraction steps:

1. Define exportable CMake target names before moving code:
   `quader_modeling_core_geometry`, `quader_modeling_core_polygon`,
   `quader_modeling_core_runtime`, optional `quader_modeling_core_qdr`, and optional
   `quader_modeling_core_metadata`.
2. Add dependency guardrails forbidding modeling core includes of `scene/`, `assets/`,
   `editor/commands`, `editor/history`, `render/`, `ui/`, `platform/`, and `api/`.
3. Move scene-bound runtime map files out of `src/editor/modeling/runtime` or quarantine them in an
   app adapter package.
4. Create a neutral facade over runtime sessions, operation commands, operation catalogs, overlays,
   selections, previews, and compiled meshes.
5. Split modeling-only metadata from the full editor host metadata catalog.

### Viewport Navigation

Reusable:

- camera state and `ViewportCameraController`;
- pure orbit, pan, zoom, fly, view-axis, focus-bounds, projection, and ray helpers;
- view-cube primitive generation and picking;
- navigation-only semantic input frame/result types;
- `ViewportWorkspaceService` after API enum coupling is removed or aliased through neutral types.

Keep app-side:

- ImGui drawing and `NavigationGizmo::draw`;
- SDL relative mouse capture and cursor warping;
- raw input collection in `ViewportPanel`;
- command execution and shortcut catalog ownership;
- tool runtime construction;
- settings persistence and UI presentation;
- scene/document bounds lookup.

First extraction steps:

1. Move pure navigation functions currently under `editor/interactions` and
   `editor/modeling/runtime` into a viewport navigation core package.
2. Split `ViewportInputRouter` into a pure `ViewportNavigationRouter` plus a Quader-specific wrapper
   for tool, grid, selection-mode, command, and shortcut behavior.
3. Consolidate camera conversion helpers currently duplicated between viewport, UI navigation gizmo,
   and viewport camera adapter code.
4. Move `host_viewport_contract_adapter.cpp` out of viewport core; it is app metadata/command
   composition, not reusable navigation logic.
5. Add Unity/Godot camera basis adapters with explicit forward/up/right, FOV, orthographic size,
   near/far, and coordinate-system conversion rules.

### Transform Gizmo Core

Reusable:

- `TransformGizmoFrameInput`;
- frame generation for axes, planes, rings, trackball, and rotate indicators;
- gizmo picking for move, rotate, scale, planes, center handles, and rings;
- Godot-style visual dimensions as a single `TransformGizmoVisualSpec`;
- ray/axis drag math for move, rotate, and scale;
- pending click versus drag-threshold state machine;
- event output such as `BeginDrag`, `UpdateDrag`, `Commit`, `Cancel`, and `DeferredSelectionClick`;
- backend-neutral primitive emission.

Keep app-side:

- `TransformGizmoInteraction` as the Quader adapter over `QuaderEditorApi`;
- `ModelingSessionService`, `TransformDragController`, and `SceneTransformDragService`;
- public API request construction;
- object/component/light transform history;
- snapping policy and scene selection fallback;
- `ViewportPanel` event collection;
- Filament proxy/cache submission.

First extraction steps:

1. Centralize Godot-style gizmo constants used by runtime geometry and overlay conversion.
2. Add pure tests for `pick_transform_gizmo`, `pick_move_gizmo_axis`,
   `pick_scale_gizmo_axis`, and `pick_rotate_gizmo_axis`.
3. Convert the current interaction state machine to call a neutral core and translate emitted
   events into `QuaderEditorApi` requests.
4. Move the gizmo overlay appender out of Filament-owned render code or rename it as a neutral
   overlay builder.
5. Keep overlay rendering as editor/display-space primitives. Do not turn gizmos into ordinary lit
   scene meshes.

### Overlay Contracts

Reusable:

- overlay frame, camera, layer, style role, source, element ref records;
- overlay mesh/build-plan primitives;
- overlay signatures and cache keys that do not own backend resources;
- transform gizmo, selection marquee, box preview, and view-cube primitive output.

Keep backend-specific:

- Filament buffers, entities, material instances, layer masks, and proxy lifetimes;
- `OverlayProxyFactory`;
- render pass setup and backend-specific depth/blend/cull policy;
- selected-object and scene-wire persistent proxy resource updates.

First extraction steps:

1. Move `ViewportOverlayFrame` and `ViewportOverlayCamera` out from under `src/render` into
   `quader_overlay_contracts`.
2. Keep modeling extraction semantic; project it into neutral overlay snapshots through a dedicated
   projection/snapshot builder.
3. Split selected-object and scene-wire overlays into neutral extraction records plus Filament
   adapter resources.
4. Preserve `docs/overlay-handling.md` as the canonical behavior matrix.

## Unity And Godot Integration Boundary

The desired future plugin shape is:

```text
Unity C# or Godot script/editor UI
  -> engine adapter
  -> C ABI or GDExtension facade
  -> quader_embed
  -> Quader public APIs and reusable core libraries
  -> engine-owned mesh/material/viewport/render sync
```

Unity should use a C ABI at the binary boundary. Godot can use a C++ GDExtension wrapper when built
with a compatible toolchain, but the wrapper should still bind `quader_embed`, not raw Quader
internals.

Required `quader_embed` features:

- session lifecycle: create, destroy, tick, version, allocator/free, structured errors;
- operation/action/tool catalogs as ABI-safe arrays or JSON snapshots with schema versions;
- document/modeling operations through stable handles and transactions;
- normalized engine input facade for pointer, key, modifier, wheel, drag, viewport size, and frame
  delta;
- selection, hover, transform, and history event stream;
- render/export snapshots for meshes, materials, lights, overlays, and dirty resource diffs;
- UTF-8 path and optional memory/buffer project IO;
- engine export profiles for Unity/Godot units, materials, and light intensity conversion.

Engine adapters must own:

- engine object/node creation and destruction;
- engine mesh upload and material mapping;
- render texture or viewport/subviewport presentation;
- engine undo integration where needed;
- cursor capture and pointer lock;
- engine-specific coordinate and unit conversion.

## File Count Reduction Track

This track can start before the full standalone extraction, but it must preserve current owners.

### High-Value File Reductions

1. Remove descriptor-only viewport tool wrapper files.

   Current issue: many `src/editor/tools/interactive/*_tool.hpp` files are constructor-only wrappers
   around descriptor-owned behavior, and they duplicate names with real interaction owners under
   `src/editor/interactions`.

   Target: instantiate `ViewportTool`, `ModalViewportTool`, `TwoStageBoxTool`, `DrawnPlaneTool`,
   `PointSequenceTool`, and `DragCommitTool` directly from descriptors in
   `viewport_tool_catalog.cpp`.

   Expected reduction: roughly 12 to 14 files.

2. Replace tiny modeling command wrapper classes with catalog-local adapters.

   Current issue: service-backed tools such as assign material, copy, paste, repeat, flip, clear,
   and invert are spread across small `.cpp` and `.hpp` files even though behavior routes to existing
   owners.

   Target: add a catalog-local table or lambda adapter in `modeling_command_tool_catalog.cpp`. Keep
   real custom behavior where it exists, especially delete selection and scene-aware routes.

   Expected reduction: roughly 20 files, plus simpler catalog registration.

3. Remove low-risk pass-through files.

   Candidates:

   - empty or near-empty API implementation files;
   - alias-only command headers;
   - constructor-only `.cpp` files that add no behavior.

   Expected reduction: small, but low risk.

4. Consolidate polygon operation families only where behavior is naturally grouped.

   Current issue: `src/mesh/polygon/operations` has many files. Some are large and should stay
   separate. Some small family variants can be grouped inside the polygon owner.

   Good candidates:

   - simple bridge variants;
   - element wrapper operations such as extrude/inset families;
   - small surface create/modify helpers.

   Do not merge large owners such as bevel edges, plane cut, slice quads, or knife operations only
   to reduce count.

5. Parameterize small modeling-domain operation tests where readability stays good.

   Do not merge guardrail tests that protect owner boundaries. Test file count is secondary to
   behavioral clarity.

### What Not To Do For File Count

- Do not collapse `DocumentCommandService`; it owns real document mutation and validation behavior.
- Do not turn `SceneRenderBridge` into a large algorithm owner; it is a composition root.
- Do not split `host_contract.cpp` further just because it is large; it is a source of truth.
- Do not merge CMake targets just to reduce target count if that weakens dependency direction.
- Do not move overlay, selection, or transform behavior without parity tests.

## Over-Wide Service Reduction Track

`ModelingSessionService` should remain as a compatibility facade while ownership is extracted in
narrow slices.

Candidate extractions:

- selection mode and selection projection owner;
- overlay dirty/result propagation instead of manual invalidation from many mutation paths;
- transform drag/preview owner;
- modal preview state owner;
- runtime tool command factory;
- modeling clipboard helper;
- context action execution helper;
- cache signature and conversion helper.

Several current "services" are stateless wrappers. Prefer free functions or internal namespaces near
the owner when there is no durable state. Keep separate services only when they own real state,
lifetime, cache invalidation, or policy.

Render overlay caches have duplicated resource lifecycle mechanics. Extract lifecycle plumbing only;
do not merge selected-object semantics with scene-wire semantics.

## Dependency Guardrails Needed First

Add source-policy guardrails before moving files:

- portable modeling core must not include `scene/`, `assets/`, `editor/commands`, `editor/history`,
  `render/`, `ui/`, `platform/`, or `api/`;
- portable navigation core must not include `QuaderEditorApi`, `ProjectSession`,
  `ModelingSessionService`, `render/`, `ui/`, `platform/`, SDL, ImGui, or Filament;
- portable gizmo core must not include `QuaderEditorApi`, scene/assets/history services, render
  backend headers, UI headers, SDL, ImGui, or Filament;
- overlay contracts must not include Filament, ImGui, SDL, `SceneDocument`, `AssetRegistry`, or UI
  packages;
- app adapters may depend downward on the reusable libraries, never the reverse.

Also add a target/include matrix with temporary allowlist entries tied to migration phases. Burn the
allowlist down as each extraction lands.

## Test Gates

Current useful tests:

- modeling runtime tool tests under `tests/modeling_domain/tool/`;
- modeling runtime operation tests under `tests/modeling_domain/operation/`;
- `ViewportCameraTests.*`;
- `ViewportInputRouterTests.*`;
- `ViewportInteractionControllerTests.*`;
- `ViewportTransformGizmoOverlayBuilderTests.*`;
- `ProgrammaticBehaviorScenarioTests.*`;
- `ModelingSessionServiceTests.*`;
- `DocumentCommandTests.SceneTransform*`;
- `BuildGuardrailTests.*`;
- `OopArchitectureGuardrailTests.*`;
- `ProgrammaticApiGuardrailTests.*`;

Add before extraction:

- direct tests for `pick_view_cube`, `viewport_view_axis`, `viewport_step_view_axis`, and
  view-cube hit behavior;
- direct gizmo pick-priority tests for move, scale, rotate, planes, rings, and center handles;
- pure drag-delta tests for move axis-ray, rotate angle, scale factor, and snap-disabled begin;
- headless navigation gizmo planner tests;
- core primitive-output tests for gizmo and view cube;
- ABI layout/version tests once `quader_embed` exists;
- Unity/Godot adapter smoke tests once plugin wrappers exist.

Initial verification set for extraction slices:

```powershell
cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests quader_tests quader_guardrail_tests
build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_filter=QdrModelingRuntimeToolViewportNavigationTests.*:QdrModelingRuntimeToolViewportProjectionTests.*:QdrModelingRuntimeToolTransformGizmoTests.*:QdrModelingRuntimeToolMoveTests.*:QdrModelingRuntimeToolRotateTests.*:QdrModelingRuntimeToolScaleTests.*:QdrModelingRuntimeToolSelectTests.*:QdrModelingRuntimeOperationTransformPreviewTests.*:QdrModelingRuntimeOperationTransformTests.*:QdrModelingRuntimeOperationTranslateTests.*
build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportCameraTests.*:ViewportInputRouterTests.*:ViewportInteractionControllerTests.*:ViewportTransformGizmoOverlayBuilderTests.*:ProgrammaticBehaviorScenarioTests.*:ModelingSessionServiceTests.*Transform*:DocumentCommandTests.SceneTransform*
build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=BuildGuardrailTests.*:OopArchitectureGuardrailTests.*:ProgrammaticApiGuardrailTests.*
cmake --build --preset vs2026-debug --target quader
```

Use narrower affected checks for each small slice, then broaden when package boundaries change.

## Migration Phases

### Phase 0 - Lock The Baseline

- Add this plan to `docs/public-docs-examples.txt` as an internal doc.
- Add source-policy guardrails for portable core include boundaries.
- Add missing pure gizmo and view-cube tests.
- Record current file counts and small-file counts in a guardrail or script output.
- Keep existing app behavior untouched.

### Phase 1 - Low-Risk File Reduction

- Remove descriptor-only viewport tool wrapper files.
- Replace tiny modeling command wrappers with catalog-local adapters.
- Remove empty/pass-through API and alias files.
- Update static-assert tests that currently require concrete wrapper classes.
- Run guardrails, catalog tests, viewport tool catalog tests, and the final app build.

### Phase 2 - Neutral Contracts

- Create `quader_overlay_contracts`.
- Move `ViewportOverlayFrame`, `ViewportOverlayCamera`, overlay layer/style/source records, and
  primitive records into the neutral package.
- Add a shared signature/hash helper for projection and cache signatures.
- Centralize `TransformGizmoVisualSpec`.
- Add guardrails that these contracts remain backend-neutral.

### Phase 3 - Modeling Core Export Surface

- Define exportable modeling core targets.
- Quarantine scene-bound runtime map files.
- Add a modeling-core facade over runtime sessions, operation commands, selections, overlays,
  previews, and compiled mesh output.
- Split modeling-only metadata from full host metadata.
- Keep `ModelingSessionService` as the Quader app compatibility adapter.

### Phase 4 - Viewport Navigation Core

- Move pure camera/navigation/projection functions into the navigation target.
- Split `ViewportNavigationRouter` from Quader tool/command input routing.
- Move view-cube primitive generation and picking into the core.
- Keep ImGui navigation gizmo drawing as an adapter.
- Add Unity/Godot basis conversion tests.

### Phase 5 - Transform Gizmo Core

- Move gizmo frame generation, picking, visual spec, drag math, and pending click/drag state into
  the gizmo core.
- Make the core emit events and primitives instead of calling `QuaderEditorApi`.
- Update `TransformGizmoInteraction` to translate core events to Quader public API requests.
- Move gizmo overlay appending to neutral overlay primitives.

### Phase 6 - Embedding Facade

- Add `quader_embed` handles, lifecycle, versioning, error buffers, allocator/free functions, and
  schema-versioned DTOs.
- Add command, modeling, selection, transform, overlay, and render/export snapshot APIs.
- Add an event stream for scene/assets/selection/history/tool/overlay changes.
- Add UTF-8 path and memory/buffer project IO surfaces.

### Phase 7 - Unity And Godot Adapters

- Unity: implement a C ABI wrapper and C# P/Invoke layer over `quader_embed`.
- Godot: implement GDExtension wrapper over `quader_embed`, with optional C ABI path for prebuilt
  distribution.
- Map engine input to neutral navigation/gizmo input.
- Map Quader scene/modeling output to engine nodes, meshes, materials, lights, and overlays.
- Keep engine undo/render/material ownership in the engine adapter.

## Risks

- Overlay regressions are easy. Preserve `docs/overlay-handling.md` invariants and tests.
- Gizmo pending-click behavior must keep ordinary selection clicks working when a handle is under the
  cursor.
- Move-axis drags must keep using axis-ray distance, not pixel fallback, except for synthetic tests
  with static rays.
- Snapping must not activate on the press/begin frame for plain clicks.
- Naive render/export snapshots can copy too much mesh data. Use revision-keyed immutable payloads.
- Engine camera conventions differ. Unity and Godot adapters need explicit coordinate tests.
- Removing wrapper classes can break tests that assert class inheritance rather than behavior.
- A C ABI must own memory, errors, and versioning explicitly from day one.

## Recommended First PRs

1. Add pure gizmo pick tests and view-cube tests.
2. Add include-boundary guardrails for future portable packages.
3. Remove viewport tool wrapper headers by direct descriptor catalog instantiation.
4. Replace tiny modeling command wrapper classes with catalog-local adapters.
5. Move neutral overlay contracts out of `src/render`.
6. Centralize gizmo visual spec constants.
7. Split `ViewportNavigationRouter` from `ViewportInputRouter`.
8. Add the first `quader_embed` lifecycle and catalog snapshot prototype.

This order gives an immediate file-count reduction while protecting the reusable-library extraction
with tests and dependency guardrails.
