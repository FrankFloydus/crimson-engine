<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Core Modeling SDK Extraction Plan

Internal architecture plan generated from a 10-subagent audit plus a local repository pass on
2026-07-04.

The goal is to encapsulate Quader's core modeling code into a neat, portable modeling SDK toolkit
that can be reused by other engines without pulling in the standalone Windows editor, renderer, UI,
project document, command palette, or platform stack.

This is not a plan to restore the old copied SDK boundary. Current repository policy explicitly says
former SDK code was absorbed into the native app. The first migration step must update that policy
deliberately for a new app-owned portable core boundary while keeping the old SDK names, macros,
presets, and copied-package assumptions banned.

## Executive Decision

Build the SDK in three layers:

1. `quader_modeling_sdk_polygon`: pure geometry, polygon document, topology, mesh compilation,
   polygon operations, document picking, and portable QDR document IO.
2. `quader_modeling_sdk_runtime`: headless modeling session, runtime operation execution, preview
   transactions, picking policy, pure interaction math, tool state, semantic overlay payloads, and
   descriptor metadata.
3. Engine/native adapters: Quader native app, Unity, Godot, Unreal, or other hosts own scene
   entities, material assets, GPU upload, input routing, undo integration, viewport presentation,
   and file/package policy.

Do not export `QuaderEditorApi`, `QuaderApi`, `ModelingSessionService`, `SceneDocument`,
`ProjectDocument`, `AssetRegistry`, `SceneRenderBridge`, `ViewportWorkspaceService`, UI panels,
SDL, ImGui, Filament, or native project save/load as SDK core.

## Audit Findings

The subagent audit covered 10 independent slices:

| Area | Finding |
| --- | --- |
| Build targets | `quader_geometry`, `quader_modeling_polygon`, `quader_modeling_runtime`, `quader_qdr_document_io`, and `quader_editor_metadata` already form useful lower targets. Top-level configure still drags in app/render dependencies too early. |
| Pure mesh core | `src/mesh/**` is already mostly SDK-shaped and has no hard UI/render/viewport/platform dependency. |
| Runtime modeling | The extractable runtime is below `ModelingSessionService`: session state, operation execution, picking, previews, tool state, and semantic overlays. |
| Public API | `QuaderEditorApi` and `QuaderApi` are native app facades, not cross-engine SDK surfaces. |
| Scene/assets | `quader_poly::Document` and value mesh payloads are portable; scene identity, asset IDs, revisions, and undo/history stay app-owned. |
| QDR IO | QDR document DTO/codecs are portable; native project save/load must be split away from SDK document/interchange IO. |
| Testing | Existing modeling-domain tests are strong. Missing pieces are standalone SDK consumer tests, target dependency guardrails, header hygiene checks, and parity transcripts. |
| Dependencies | Public SDK headers should avoid GLM, EnTT, nlohmann/json, SDL, ImGui, Filament, Win32, and STL ownership across binary boundaries. GTE/OpenMesh/json may be private implementation dependencies. |
| Metadata/catalogs | Portable catalogs should be descriptor-only. Native command bridge, keybindings, command IDs, and delete/duplicate routing stay app-owned. |
| Viewport/render | Pure projection, ray, gizmo, selection-box, picking, and semantic overlay math are portable. Viewport controllers, cache invalidation, render policy, and GPU resources stay native. |

## Current Owner Map

Portable or nearly portable today:

- `src/mesh/geometry`: geometry primitives, predicates, projection, intersections, distances,
  triangulation, and broad-phase helpers.
- `src/mesh/polygon`: polygon document model, component IDs, topology, selection, validation,
  mesh compilation, UVs, picking, overlay preview primitives, and deterministic operations.
- `src/io/qdr/qdr_document.*`, `qdr_polygon_document_codec.*`, `qdr_root.*`,
  `qdr_diagnostic.hpp`: portable document DTO and codec layer.
- `src/editor/modeling/runtime`: runtime session state and behavior, excluding misplaced
  scene-identity glue.
- `src/editor/modeling/operations`: runtime operation execution over `EditorSession`.
- `src/editor/interactions`: pure tool math and low-level interaction algorithms where they avoid
  host input/render ownership.
- `src/editor/metadata`: descriptor-only host metadata catalogs.

Native app adapter territory:

- `src/editor/modeling/services`: especially `ModelingSessionService`,
  `ModelingMeshSyncService`, `SceneModelingSyncService`, `ModelingTransformDragService`,
  selection/overlay services, hierarchy duplication, and app history integration.
- `src/editor/projection`: scene-to-runtime projection, picking cache projection, and overlay
  projection into native render frames.
- `src/editor/commands`: `CommandTool` execution, public command registration, shortcut routing,
  command palette behavior, delete-selection split routing, and native undo semantics.
- `src/api`: public native app automation/editor API.
- `src/scene`, `src/assets`, `src/render`, `src/ui`, `src/platform`, `src/app`, `src/viewport`:
  native standalone editor domains.
- `src/io/qdr/qdr_document_reader.*`, `qdr_document_writer.*`, `qdr_project_codecs.*`,
  `qdr_project_write_snapshot.*`: native project persistence.

## Target Folder Structure

Use a new app-owned SDK source tree after the policy and guardrails are updated. Prefer
`src/sdk/core/modeling` for source ownership inside this repo, with install/package artifacts
generated from it. Avoid reviving old top-level copied SDK package names.

```text
src/sdk/core/modeling/
  AGENTS.md
  include/quader/modeling/
    version.hpp
    result.hpp
    ids.hpp
    math/
      vec2.hpp
      vec3.hpp
      transform.hpp
      ray.hpp
      bounds.hpp
    geometry/
      geometry.hpp
    polygon/
      document.hpp
      document_builder.hpp
      element_handles.hpp
      selection.hpp
      topology.hpp
      validation.hpp
      mesh_compilation.hpp
      picking.hpp
      overlay_preview.hpp
      operations.hpp
      operation_descriptor.hpp
      operation_registry.hpp
    runtime/
      session.hpp
      session_state.hpp
      runtime_command.hpp
      operation_command.hpp
      operation_schema.hpp
      operation_registry.hpp
      selection_policy.hpp
      picking_policy.hpp
      preview_transaction.hpp
      semantic_overlay.hpp
      tools/
        tool_ids.hpp
        viewport_projection.hpp
        viewport_navigation.hpp
        drag_math.hpp
        transform_gizmo.hpp
        selection_box.hpp
        snapping.hpp
    metadata/
      host_contract.hpp
      operation_catalog.hpp
      action_catalog.hpp
      tool_catalog.hpp
      shortcut_catalog.hpp
      overlay_catalog.hpp
      material_catalog.hpp
    io/
      qdr_document.hpp
      qdr_diagnostic.hpp
      qdr_reader.hpp
      qdr_writer.hpp
    host/
      host_types.hpp
      modeling_host.hpp
      mesh_payload.hpp
      material_payload.hpp
    c_api/
      quader_modeling_c.h
  src/
    geometry/
    polygon/
      model/
      selection/
      topology/
      mesh/
      picking/
      operations/
      internal/
    runtime/
      session/
      operations/
      tools/
      overlays/
      internal/
    metadata/
    io/
      qdr/
    host/
    c_api/
  cmake/
    QuaderModelingSdkTargets.cmake
    QuaderModelingSdkConfig.cmake.in
  examples/
    cpp_create_box/
    cpp_run_operation/
    cpp_qdr_roundtrip/
    c_api_smoke/
  tests/
    consumer/
    parity/
    header_hygiene/
```

If the team wants a top-level distributable SDK folder later, generate it from this tree:

```text
sdk-dist/quader-modeling/
  include/
  lib/
  cmake/
  examples/
  licenses/
  README.md
```

Do not make `sdk-dist` the source-of-truth tree.

## Public Surface Shape

Start with a source-level C++ SDK, then add a narrow C ABI for engine/plugin boundaries.

C++ SDK surface:

- Own math/value types: `Vec2`, `Vec3`, `Ray`, `Transform`, `Bounds`.
- `PolygonDocument`, `DocumentBuilder`, element handles, selection records, validation results.
- Mesh compile output as value DTOs: vertices, indices, primitive spans, authored polygon payload.
- Operation descriptors and execution results.
- `ModelingSession` for headless runtime editing over polygon documents.
- Runtime commands for selection, transform, modal tools, and operation execution.
- Semantic overlay payloads: triangles, lines, points, layer IDs, style roles, object/component refs.
- QDR document read/write for portable documents.
- Metadata descriptors for operations, actions, tools, shortcuts, overlays, preferences, material
  recipes, and UI placement hints.

C ABI surface, later:

- Opaque handles: `qdr_modeling_session`, `qdr_document`, `qdr_mesh_payload`.
- Plain C structs for vectors, rays, bounds, commands, result/status, mesh buffers, and diagnostics.
- Caller-owned allocation callbacks or explicit `destroy` functions.
- No STL, exceptions, GLM, EnTT, nlohmann/json, SDL, ImGui, Filament, or Win32 types.

## Explicit Non-Goals

- Do not move `QuaderEditorApi` or `QuaderApi` into SDK core.
- Do not expose native project save/load as portable SDK IO.
- Do not expose `SceneDocument`, `ProjectDocument`, `AssetRegistry`, `ModelingSessionService`, or
  `ProjectSession`.
- Do not expose native command IDs, keybinding persistence, command palette registration, or UI menu
  ownership as SDK core.
- Do not expose Filament overlay resources, materials, render passes, or viewport render policy.
- Do not make runtime object IDs durable cross-engine identity.
- Do not copy source files into a parallel SDK tree without moving ownership and guardrails.

## Phase 0 - Policy And Baseline

Purpose: make the extraction intentional and measurable before moving code.

Concrete tasks:

1. Update project guidance to allow a new app-owned portable modeling SDK boundary while still
   banning the old copied SDK model.
2. Keep existing bans for old names such as `QUADER_MODELING_SDK_DIR`, SDK-specific presets, and
   legacy copied package folders.
3. Add an architecture note that `src/sdk/core/modeling` is the new source boundary only after
   guardrails are in place.
4. Freeze baseline behavior with current tests:
   - `quader_modeling_domain_tests`
   - `quader_tests`
   - `quader_guardrail_tests`
   - `ctest --preset vs2026-debug --output-on-failure`
5. Add a migration checklist to `docs/absorbed_modeling/README.md` or link this document from it.

Exit criteria:

- Policy no longer contradicts the requested portable SDK direction.
- Old SDK boundary remains banned.
- Baseline tests are green or known failures are recorded.

## Phase 1 - Additive Build Boundary

Purpose: create SDK-shaped targets without moving behavior yet.

Concrete tasks:

1. Add `cmake/QuaderModelingSdkCore.cmake`.
2. Add interface or thin static targets over existing lower targets:
   - `quader_modeling_sdk_geometry` -> `quader_geometry`
   - `quader_modeling_sdk_polygon` -> `quader_modeling_polygon`
   - `quader_modeling_sdk_qdr_document` -> `quader_qdr_document_io`
   - `quader_modeling_sdk_metadata` -> `quader_editor_metadata`
   - defer `quader_modeling_sdk_runtime` until runtime include hygiene is fixed
3. Move app/render dependency discovery behind app/render configuration, or add a default-on
   `QUADER_BUILD_NATIVE_APP` option so SDK-only configuration does not require Filament, SDL, ImGui,
   material compilation, or Windows render paths.
4. Stop using broad public `src` include roots for SDK targets. Start with exported include roots
   and private implementation include roots.
5. Convert `quader_modeling_polygon` from `file(GLOB ... CONFIGURE_DEPENDS)` to an explicit source
   list before it becomes SDK-exported, so app-only files cannot enter by accident.

Exit criteria:

- A core-only configure/build path can build SDK candidate targets without native app render/UI
  dependencies.
- Native app targets still build unchanged.
- SDK target dependency guardrail proves no dependency on app/render/UI/platform/API targets.

## Phase 2 - Header Hygiene And DTO Split

Purpose: make the future public headers clean enough for external engines.

Concrete tasks:

1. Add guardrail tests scanning exported SDK headers for banned includes and symbols:
   - `scene/`, `assets/`, `render/`, `ui/`, `platform/`, `app/`, `api/`
   - `SceneDocument`, `ProjectDocument`, `AssetRegistry`, `ProjectSession`
   - `ModelingSessionService`, `ViewportOverlayFrame`, `SceneRenderBridge`
   - `SDL`, `ImGui`, `filament`, `glm`, `entt`, `nlohmann`
2. Split `src/editor/modeling/modeling_types.hpp` into:
   - runtime-neutral DTOs for SDK runtime
   - app-facing DTOs containing `EntityId`, `AssetId`, `MeshAsset`, scene bounds, render overlay
     frames, and native service types
3. Move or reclassify `modeling_object_runtime_map.*` out of runtime. It belongs to the native app
   adapter because it maps transient runtime object IDs to scene entities and scene identity.
4. Add `MeshPayload` and `AuthoredMeshPayload` value types for SDK export instead of moving
   `MeshAsset`.
5. Add conversion helpers at app boundaries:
   - SDK `MeshPayload` <-> app `MeshAsset`
   - SDK authored polygon document <-> app `authored_polygon_document`
   - SDK semantic overlay payload <-> app `ViewportOverlayFrame`

Exit criteria:

- Runtime-neutral headers compile without app owner headers.
- App-facing services still compile through conversion adapters.
- Guardrails catch accidental scene/asset/render/platform leaks.

## Phase 3 - Polygon Core Move

Purpose: move the cleanest portable core first.

Concrete source moves:

- `src/mesh/geometry/*` -> `src/sdk/core/modeling/src/geometry/`
- `src/mesh/polygon/document_*` and model/storage files -> `polygon/model/`
- `src/mesh/polygon/quader_poly_document_selection_api.cpp` and liveness helpers -> `polygon/selection/`
- topology helpers and OpenMesh backend wrappers -> `polygon/topology/`
- mesh compilation and UV helpers -> `polygon/mesh/`
- document picking and overlay preview -> `polygon/picking/`
- `src/mesh/polygon/operations/*` -> `polygon/operations/`
- `src/mesh/polygon/internal/*` -> `polygon/internal/`

Keep compatibility forwarding headers under `src/mesh/**` for one transition if needed, but make
new code include SDK headers.

Important details:

- GTE, Mapbox Earcut, OpenMesh, and profiling hooks stay private implementation dependencies.
- Public headers expose only Quader-owned SDK value types.
- Operation IDs and descriptors remain stable.
- Determinism, topology, UV, material-slot, selection, and validation behavior must not change.

Exit criteria:

- `quader_modeling_sdk_polygon` owns the moved sources.
- Existing app targets consume the SDK polygon target.
- Polygon loop tests pass.

## Phase 4 - Portable QDR Document IO

Purpose: separate portable document persistence from native project persistence.

Concrete tasks:

1. Split `src/io/qdr` conceptually and then physically:
   - portable document IO: `qdr_document`, `qdr_polygon_document_codec`, `qdr_root`,
     `qdr_diagnostic`, atomic document write if needed
   - native project IO: project reader/writer, project codecs, project write snapshot
2. Move portable document IO under `src/sdk/core/modeling/src/io/qdr/`.
3. Keep native project save/load under `src/io/qdr/project/` or equivalent app-owned path.
4. Make the SDK QDR API distinguish root kind/version explicitly. Do not infer schema from extension
   or version alone.
5. Keep runtime compatibility IO as an adapter over SDK QDR document DTOs, not as canonical schema
   ownership.
6. Keep native project texture path/material asset packaging out of SDK document IO.

Exit criteria:

- SDK can serialize/deserialize portable documents and authored polygon payloads.
- Native project save/load still round-trips project assets/entities/components.
- QDR fixtures and malformed-file tests pass.

## Phase 5 - Runtime Core Extraction

Purpose: extract headless modeling runtime without app scene/assets/render ownership.

Portable runtime includes:

- `EditorSession` or renamed SDK `ModelingSession`
- `SessionState`, polygon object records, runtime selection, active object/light state
- runtime command IDs/settings/results
- operation registry and operation command execution
- preview transactions
- runtime picking policy
- semantic overlay frame generation
- repeat-last state
- pure tool state and modal tool algorithms
- pure viewport projection/navigation/drag/gizmo/selection-box math

Native app adapter retains:

- `ModelingSessionService`
- `ModelingMeshSyncService`
- `SceneModelingSyncService`
- `SelectionProjectionService`
- `ModelingOverlayService`
- `ModelingTransformDragService`
- hierarchy duplication
- scene transform preview integration
- editor history integration
- document command integration
- scene/entity/material/asset binding

Concrete tasks:

1. Create `quader_modeling_sdk_runtime` target after Phase 2 include hygiene is green.
2. Move session/runtime/operations/interactions into SDK runtime subfolders in small batches.
3. Keep app-facing services as adapters over SDK runtime.
4. Add a host invalidation contract for external engines, but do not move native scene revision
   caching into SDK core.
5. Keep semantic overlay payloads render-agnostic. Native render decides style, depth mode, layer
   realization, and Filament resources.

Exit criteria:

- `qdr_custom_engine_modeling_runtime_smoke_tests` is replaced or supplemented by a real SDK
  consumer test that includes exported SDK headers only.
- Runtime loop tests pass.
- Native `ModelingSessionService` remains a thin adapter and no longer owns duplicated runtime
  algorithms.

## Phase 6 - Metadata And Catalog Split

Purpose: expose stable descriptors without leaking native command routing.

Portable descriptors:

- operation descriptors: IDs, labels, preview support, options, defaults, ranges
- action descriptors: action ID, operation/tool ID, action kind, preview kind, availability,
  panel ordering, visibility
- tool descriptors: ID, label, modal/transform flags
- shortcut and gesture descriptors as host-neutral suggestions
- overlay layer descriptors and semantic style roles
- material recipe descriptors and default texture asset descriptors
- preference descriptors and group descriptors

Native bridge keeps:

- `CommandTool` classes and execution families
- public command IDs and registration
- command palette and keybinding persistence
- `edit.delete`, `edit.apply_material`, and command target mapping
- shortcut remaps such as mirror axis target aliases
- delete-selection split routing
- duplicate hierarchy behavior
- native history/mesh sync/overlay invalidation

Concrete tasks:

1. Move descriptor-only host metadata into SDK metadata.
2. Remove native bridge facts such as `public_command_id` and `registers_public_command` from
   portable operation/action catalog rows.
3. Keep action IDs distinct from operation IDs; do not collapse variants.
4. Add catalog parity tests comparing old and new descriptor output during migration.

Exit criteria:

- Engine hosts can build menus/toolbars/preferences from SDK descriptors.
- Native command bridge still owns command execution and shortcut behavior.
- Metadata loop tests pass.

## Phase 7 - Host Adapter Contracts

Purpose: let other engines integrate without inheriting Quader native app internals.

Recommended adapter interfaces:

```text
IModelingHost
  create_or_update_mesh(object_ref, MeshPayload)
  destroy_mesh(object_ref)
  set_selection(SelectionPayload)
  get_material_slot_mapping()
  begin_undo_transaction(label)
  commit_undo_transaction()
  cancel_undo_transaction()

IViewportHost
  screen_to_ray(position)
  viewport_camera()
  viewport_size()
  grid_settings()
  pointer_modifiers()

IRenderOverlayHost
  submit_semantic_overlay(SemanticOverlayFrame)
  map_overlay_style_roles()

IAssetHost
  resolve_material(material_ref)
  import_texture(asset_descriptor)
  package_authored_mesh(AuthoredMeshPayload)
```

These are adapter contracts, not SDK core owners. The native Quader app implements them through
existing services. Unity, Godot, Unreal, or another engine implements them through its own scene,
asset, material, render, input, and undo systems.

Exit criteria:

- A minimal non-Quader host test can create a session, run operations, compile mesh payloads, and
  receive semantic overlays without linking app targets.

## Phase 8 - Packaging And Distribution

Purpose: make the SDK consumable outside this repo.

Concrete tasks:

1. Add CMake install/export support:
   - `QuaderModelingSdkTargets.cmake`
   - `QuaderModelingSdkConfig.cmake`
   - version file
2. Generate `sdk-dist/quader-modeling/` artifacts from build/install.
3. Include license evidence for SDK dependencies in generated distribution:
   - OpenMesh BSD-3-Clause, if enabled
   - GTE BSL-1.0
   - nlohmann/json MIT, if document IO is included
   - Tracy license only if profiling is enabled in SDK builds
4. Add examples that compile as external consumers:
   - create a polygon document and compile mesh
   - execute an operation
   - create a runtime session and duplicate/move mesh
   - QDR roundtrip
   - C API smoke test, after C ABI exists
5. Add a package README that states thread ownership, memory ownership, exception policy, dependency
   policy, coordinate conventions, and stability policy.

Exit criteria:

- A clean external CMake project can `find_package(QuaderModelingSdk CONFIG REQUIRED)` and run a
  smoke workflow without native app dependencies.

## Guardrails To Add

Add guardrails before large moves:

- `SdkCoreTargetsDoNotDependOnNativeAppTargets`
- `SdkPublicHeadersDoNotIncludeNativeAppOwners`
- `SdkPublicHeadersDoNotExposeThirdPartyRuntimeTypes`
- `SdkQdrDocumentIoDoesNotIncludeProjectDocument`
- `SdkRuntimeDoesNotIncludeSceneAssetsRenderUiPlatform`
- `SdkMetadataDoesNotExposeNativeCommandBridgeFields`
- `NativeCommandBridgeOwnsPublicCommandIds`
- `ModelingRuntimeMapStaysAppAdapter`
- `SdkConsumerBuildLinksOnlySdkTargets`
- `SdkParityTranscriptMatchesNativeModelingWorkflow`

Recommended banned SDK-header strings:

```text
SceneDocument
ProjectDocument
ProjectSession
AssetRegistry
ModelingSessionService
ViewportOverlayFrame
SceneRenderBridge
QuaderEditorApi
SDL
ImGui
filament
nlohmann
glm
entt
```

## Test Plan

Narrow loops:

```powershell
cmake --build --preset vs2026-debug --target quader_modeling_domain_polygon_loop_tests
build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no

cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests
build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no

cmake --build --preset vs2026-debug --target quader_modeling_domain_metadata_loop_tests
build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no
```

Migration verification:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target quader_modeling_domain_tests quader_tests quader_guardrail_tests
ctest --preset vs2026-debug --output-on-failure
cmake --build --preset vs2026-debug --target quader
```

New SDK verification:

```powershell
cmake --build --preset vs2026-debug --target quader_modeling_sdk_consumer_tests
build\vs2026\Debug\quader_modeling_sdk_consumer_tests.exe --gtest_color=no
```

Add parity transcript tests that run the same workflows through native app API and SDK runtime:

- create box
- create authored mesh
- select object/component
- duplicate
- transform
- preview/commit/cancel
- bevel
- thicken
- slice quad
- fill hole
- remove doubles
- QDR roundtrip

Compare:

- canonical polygon document data
- compiled mesh vertices/indices/primitives
- selection summary
- operation status and error messages
- semantic overlay counts and element refs
- portable QDR payload

## Migration Order

Use this order to reduce risk:

1. Policy/guardrail update.
2. Additive SDK targets with existing sources.
3. Header hygiene and DTO split.
4. Geometry/polygon physical move.
5. Portable QDR document IO split.
6. Runtime core extraction.
7. Metadata/catalog split.
8. Host adapter contracts.
9. External package/install support.
10. Engine-specific adapters.

Do not start by moving `ModelingSessionService`; it is the native app composition facade and will
drag scene/assets/history/render behavior into the SDK.

## Main Risks

- Reintroducing the old SDK product boundary under a new name.
- Letting `SceneDocument`, `AssetRegistry`, native command routing, or render overlay policy leak
  into SDK core.
- Treating runtime object IDs as durable engine identity.
- Sharing `MeshAsset::authored_polygon_document` pointers instead of value-copying authored polygon
  documents.
- Duplicating availability/descriptor logic between host metadata and native operation catalogs.
- Exporting GLM/EnTT/nlohmann/SDL/ImGui/Filament types through public SDK headers.
- Breaking delicate picking and overlay policy, especially source-wire occlusion and depth-stamped
  face fills.
- Requiring Filament/SDL/ImGui just to configure a core modeling SDK build.

## First Concrete Milestone

The first useful milestone should be small and testable:

1. Add policy text allowing `src/sdk/core/modeling` as a new app-owned portable source boundary.
2. Add `quader_modeling_sdk_polygon` and `quader_modeling_sdk_qdr_document` wrapper targets.
3. Add `SdkCoreTargetsDoNotDependOnNativeAppTargets`.
4. Add one standalone consumer test that includes SDK candidate headers and links only the SDK
   polygon/QDR targets.
5. Keep native app behavior unchanged and run the polygon loop plus guardrails.

That milestone proves the SDK direction without moving runtime services or destabilizing the app.
