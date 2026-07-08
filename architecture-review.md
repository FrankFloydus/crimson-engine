<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Architecture Review Against The Portable Modeling SDK Guide

Generated: 2026-07-08

Reference: `C:\Users\Drako\Desktop\modeling_app_architecture_guide.md`

This review is the active deterministic plan for aligning Quader with the reference guide. It
consolidates the 10-agent audit and a local repository pass. The final direction is fixed:

- Quader remains the native standalone Windows editor.
- A new app-owned portable modeling core is created under `src/sdk/core/modeling`.
- The SDK namespace is `quader::modeling`.
- `QuaderEditorApi` and `QuaderApi` remain native host/editor APIs.
- The old copied SDK workflow stays banned.
- Top-level instruction authority lives in root `AGENTS.md`.
- The deleted top-level instruction and documentation folders have no authority.

## Executive Verdict

Quader already has strong native-app architecture. The public editor APIs are coherent. Mesh and
polygon code is mostly engine-clean. Rendering has a real snapshot path and keeps Filament in the
render owner. Viewport input already has semantic, toolkit-neutral structures below the UI adapter.
Modeling-domain tests and guardrails are broad. QDR project save/open is correctly native: file
dialogs and project session state stay outside low-level format code.

Quader does not yet satisfy the reference guide's portable modeling SDK contract. There is no
copyable SDK source boundary, SDK umbrella header, SDK install/export package, SDK-only configure
path, or standalone SDK consumer test. Current runtime/editor DTOs still carry app scene, asset,
viewport, overlay, and history concepts into layers that look like extraction candidates. Public
APIs are native editor APIs, not SDK APIs. Mesh IDs, operation results, render snapshots, QDR DTOs,
and preview transactions need SDK-grade contracts before runtime code moves.

The plan starts with governance and build guardrails, then moves pure geometry/polygon behavior,
portable QDR document IO, SDK result/ID contracts, runtime-neutral DTOs, headless modeling sessions,
semantic overlays, host input adapters, render snapshots, performance gates, packaging, and tests.

## Resolved Architecture Decisions

1. Active plan: create the app-owned portable modeling core described in this review.
2. Source path: `src/sdk/core/modeling`.
3. Public namespace: `quader::modeling`.
4. Umbrella header: `quader/modeling/modeling.hpp`.
5. Native app APIs: keep `QuaderEditorApi` and `QuaderApi` as native host APIs.
6. SDK APIs: add separate SDK APIs; do not rename native APIs into SDK APIs.
7. Old copied SDK: keep old package paths, old macros, old presets, and old external checkout
   wiring banned.
8. Build mode: add a default-on native app build option named `QUADER_BUILD_NATIVE_APP`; SDK-only
   configure builds SDK targets without Filament, SDL3, ImGui, native resources, or the app
   executable.
9. First SDK CMake targets:
   - `quader_modeling_sdk_geometry`
   - `quader_modeling_sdk_polygon`
   - `quader_modeling_sdk_qdr_document`
10. Deferred SDK target: `quader_modeling_sdk_runtime` is added only after DTO and header hygiene
    guardrails pass.
11. ID policy: SDK public IDs are typed and generational.
12. Result policy: SDK operations return structured `Result<T>` values with error codes,
    diagnostics, dirty flags, element deltas, and selection remap data.
13. Compatibility policy: update includes and call sites during moves. Do not create long-lived
    forwarding headers.
14. Preset policy: keep the native app presets. Add SDK-only configure/build coverage through
    CMake options, targets, and tests, not old SDK presets.
15. Test policy: SDK portability is proven by a standalone consumer test that links only SDK
    targets.
16. Documentation policy: root `AGENTS.md` and this review hold the architecture plan. New
    architecture guidance belongs in these files or nearest nested `AGENTS.md` files.

## Reference Guide Constraints

The guide translates into these Quader-specific constraints:

1. The SDK owns modeling truth. Host code adapts input, rendering, UI, project persistence, scene
   identity, materials, and platform behavior around SDK-owned modeling state.
2. Dependency direction is mechanical. SDK code does not include or link SDL3, ImGui, Filament,
   Win32 app shell, Qt, native dialogs, app scene/project classes, render backends, UI panels, or
   native public editor APIs.
3. Public SDK usage is standalone. A consumer includes one SDK umbrella header, creates a modeling
   document or session, selects elements, runs operations, previews, commits or cancels, undoes and
   redoes, validates, serializes, and inspects results without launching the app.
4. Low-level mesh APIs and high-level session APIs both exist. Low-level APIs operate on mesh or
   polygon documents. High-level APIs own selection, history, previews, scene membership inside the
   SDK, dirty state, and change tracking.
5. Tools are host-agnostic. SDL, ImGui, Qt, and platform events are translated into SDK input DTOs
   at host boundaries.
6. Rendering is snapshot-based. The SDK exposes renderer-neutral mesh, material, overlay, selection,
   and dirty payloads. Filament is one adapter.
7. Expected failures are structured. Invalid IDs, stale IDs, invalid settings, non-manifold
   topology, unsupported selections, IO diagnostics, and validation failures return structured
   errors.
8. Portability is continuously verified. Native app tests do not prove the SDK. The SDK has its own
   consumer/configure/build/test path.

## Current Strengths

### Native Public APIs

`src/api` is coherent for the native app:

- `QuaderEditorApi` is the whole-editor front door.
- `QuaderApi` covers document/modeling workflows that do not need runtime editor domains.
- Examples use public APIs, result helpers, typed command IDs, object handles, scene batches, and
  scripting surfaces.
- Public API guardrails keep owner internals out of public API headers.

This remains a native host layer. It becomes an adapter over SDK behavior where modeling behavior
overlaps.

### Mesh And Polygon Core

`src/mesh/geometry` and `src/mesh/polygon` are the cleanest SDK seed:

- geometry, polygon documents, topology, picking, compilation, and operations are already isolated
  from SDL, ImGui, and Filament;
- `quader_modeling_polygon` links to geometry, optional OpenMesh, and profiling support;
- modeling-domain tests cover polygon documents, liveness, validation, operations, QDR document IO,
  runtime operations, runtime tools, and editor metadata.

### Command, History, And Preview Ownership

The native app already routes user-visible mutation through public APIs, command services, editor
services, document commands, and modeling runtime services. `EditorHistoryService`, document
commands, modeling mementos, and preview command services are established owners. Preview
begin/update/commit/cancel behavior has deep coverage for transform and generated topology
workflows.

This behavior maps to the SDK session contract after DTO hygiene is complete.

### Rendering

The render package already has guide-aligned pieces:

- `RenderSceneSnapshot` carries scene, mesh, light, revision, and dirty records without exposing
  Filament types in the snapshot contract;
- `SceneRenderBridge` is a facade over sync, caches, materials, meshes, overlays, previews, and
  viewport renderers;
- Filament resources are contained under `src/render`;
- overlay policy is centralized.

### Viewport Input

Viewport input is partly host-neutral:

- `src/viewport/viewport_input_types.hpp` defines semantic input frames and navigation input without
  raw SDL/ImGui/Qt types;
- `ViewportInputRouter` consumes semantic actions, mouse deltas, wheel input, tool state, and
  resolved shortcuts;
- `QuaderCommandsApi` owns command/keybinding APIs and user shortcut persistence.

### Tests And Guardrails

Current test architecture is strong for a native app:

- `quader_guardrail_tests` protects source-policy, architecture, dependency, naming, CMake,
  instruction, and tooling invariants;
- `quader_tests` covers app/runtime/public API behavior;
- `quader_modeling_domain_tests` covers polygon/runtime/modeling-domain contracts;
- operation tests are categorized and include loop targets;
- QDR has stable fixtures, malformed input diagnostics, deterministic serialization, and
  round-trip coverage.

## Current CMake Target Map

The native target map remains split across these areas and must stay split during SDK extraction:

- `core`
- `math`
- `geometry`
- `polygon`
- `assets`
- `document`
- `commands`
- `editor_modeling`
- `viewport`
- `render`
- `io_qdr`
- `api`
- `ui`
- `app`

Current modeling-domain targets that stay valid while SDK targets are added:

- `quader_geometry`
- `quader_modeling_polygon`
- `quader_modeling_runtime`
- `quader_qdr_document_io`
- `quader_qdr_runtime_io`
- `quader_editor_metadata`
- `quader_modeling_domain_tests`
- `quader_modeling_domain_polygon_loop_tests`
- `quader_modeling_domain_runtime_loop_tests`
- `quader_modeling_domain_metadata_loop_tests`

The SDK adds wrapper/extraction targets around the clean lower layers first. Runtime waits for DTO
hygiene.

## Main Gaps

### P0 - SDK Build Boundary Is Missing

Required state:

- `src/sdk/core/modeling` exists;
- SDK targets build without native app dependencies;
- `QUADER_BUILD_NATIVE_APP` gates app, UI, render-backend, resource, SDL3, ImGui, Filament, and
  executable setup;
- SDK targets use narrow include roots;
- SDK targets have no transitive link to native app targets;
- a standalone consumer test links only SDK targets.

Current state:

- no SDK source boundary exists;
- no SDK umbrella header exists;
- top-level configure validates native app dependencies;
- examples link the native runtime;
- no install/export package exists for a modeling SDK.

### P0 - Header Hygiene Is Not SDK-Ready

Current candidate runtime/modeling headers carry app concepts:

- `src/editor/modeling/modeling_types.hpp` includes app assets and carries `EntityId`, `MeshAsset`,
  viewport cameras, scene preview fields, and app selection records;
- runtime maps tie modeling objects to `SceneDocument` and `EntityId`;
- `ModelingSessionService` coordinates runtime, scene sync, history, overlays, picking, command
  status, settings, and app-side identity.

Required state:

- SDK-neutral DTOs contain only SDK concepts;
- native adapter DTOs carry scene, asset, material, viewport, command, and render bridge state;
- runtime object to scene entity mapping lives in native adapter code;
- `ModelingSessionService` stays native and shrinks into an adapter over SDK runtime owners.

### P1 - Operation Results And IDs Are Not SDK-Grade

Current polygon core uses raw `ElementId`, typed adapters for vertices/faces, raw edge endpoint
pairs, per-operation selection mutation, and `OperationResult` with limited fields.

Required state:

- public IDs are typed and generational;
- stale ID detection is part of SDK API behavior;
- operations return structured success/error;
- results carry created, deleted, affected, and modified elements;
- results carry dirty flags and selection remap data;
- destructive operation remap behavior is centralized.

### P1 - QDR And OBJ IO Are Native-App Shaped

Current native behavior is correct for the app, but not portable:

- QDR project read/write headers expose `ProjectDocument`;
- reader hydration touches `ProjectDocument`, `AssetRegistry`, and `SceneDocument`;
- QDR reader includes editor projection to compile polygon documents into mesh assets;
- OBJ writer consumes `MeshAsset`;
- durable replace logic has native Win32 policy.

Required state:

- portable QDR document IO target has neutral DTOs and no scene/assets/editor/API/render/UI/platform
  includes;
- native project persistence owns hydration/extraction adapters;
- OBJ writer consumes neutral mesh spans/views;
- Win32 durable replace stays native.

### P2 - Render Snapshots Need Stronger Contracts

Current render snapshots are useful but still too coupled:

- render mesh and overlay snapshots contain same-frame spans into live app-owned data;
- snapshot building happens before some revision gates skip work;
- overlay builders traverse authored polygon topology and include modeling/editor concepts;
- `SceneRenderBridge` request types expose modeling selection concepts;
- material/texture services still have live `AssetRegistry` overloads.

Required state:

- renderer-facing snapshots declare stable owned, pinned, or explicitly borrowed lifetimes;
- semantic overlay records come from extraction/projection layers;
- render builders do not become topology owners;
- dirty gates run before expensive snapshot construction;
- material payloads become renderer-neutral PBR records.

### P2 - UI And Viewport Boundary Is Too Wide

Strong pieces exist, but `ViewportPanel` owns too much: pane rendering, input frame building,
command dispatch, hover, transform gizmo routing, overlay cache, context menu, render prep,
point-light billboard picking, pivot snap candidate scanning, frame-all/frame-selection logic, and
pane drawing.

Required state:

- UI panels present state and dispatch intent;
- viewport command resolution lives in a viewport/input owner;
- frame bounds, light picking candidates, pivot snap scanning, and overlay composition live in
  owner services;
- `ViewportToolContext` stops carrying native scene/runtime service concepts into tool code that
  belongs below the host boundary;
- ImGui and SDL stay at adapter boundaries.

### P2 - Performance Dirty Gates Run Too Late

Observed hot paths:

- transform preview sync can compile every modeling object;
- render snapshots rebuild every renderable frame before sync skips work;
- hover cache validation hashes whole mesh geometry on mouse move;
- preview transactions copy whole session state;
- tool preview meshes regenerate and then get hashed by render caches.

Required state:

- revision/signature checks happen before compile/hash/regenerate steps;
- preview updates operate on affected objects;
- render API can reuse previous snapshots;
- picking signatures are cached by object revision, transform, and pick-affecting options.

### P3 - SDK Portability Tests Are Missing

Required state:

- SDK consumer test;
- SDK header hygiene guardrails;
- SDK dependency guardrails;
- SDK target package/export check;
- SDK public examples;
- mesh-operation golden fixtures;
- deterministic seeded fuzz/property tests for topology, validation, malformed data, and
  serialization.

## Architecture Mantras

### Mantra 1: App-Owned SDK, Not Old SDK Resurrection

Create the new SDK as first-party source in this repo. Ban the old external SDK shape:

- no old top-level copied SDK package;
- no old SDK CMake presets;
- no `QUADER_MODELING_SDK_DIR`;
- no old compile-time SDK presence macros;
- no app fallback path for missing first-party SDK source.

### Mantra 2: Host APIs Adapt To SDK Truth

`QuaderEditorApi` and `QuaderApi` remain native host/editor APIs. They orchestrate scene, asset,
render, UI, command, and project behavior. The SDK API is separate and focused on modeling.

### Mantra 3: Move In Dependency Order

Migration order is fixed:

1. policy and guardrails;
2. additive SDK build targets;
3. header and DTO hygiene;
4. geometry and polygon core;
5. portable QDR document IO;
6. SDK public API;
7. headless runtime and preview;
8. host input, viewport, UI, and render adapters;
9. performance dirty gates;
10. SDK packaging, examples, and consumer tests;
11. compatibility cleanup.

### Mantra 4: Guardrails Before Moves

Add guardrails before file motion:

- SDK targets do not depend on native app, UI, render, platform, API, scene, or asset targets;
- SDK public headers do not include native owner headers or native third-party runtime types;
- SDK consumer tests link only SDK targets;
- native command IDs remain native;
- portable QDR document IO does not include project document owners.

### Mantra 5: Snapshot Means Stable Payload

Snapshot contracts explicitly state lifetime. SDK and renderer-facing snapshots use stable owned or
pinned payloads for external consumption. Borrowed same-frame spans are confined to native adapter
internals with documented lifetime.

### Mantra 6: Dirty Gates Run Before Expensive Work

Revision and signature gates run before:

- object mesh compilation;
- render scene snapshot construction;
- picking face geometry hashing;
- tool preview mesh generation;
- whole-session preview-state copies.

## Progressive Migration Plan

### Phase 0 - Lock Authority

Goal: make the architecture direction legal and unambiguous.

Actions:

1. Keep root `AGENTS.md` as the single self-contained agent authority.
2. Keep this review as the active architecture migration plan.
3. Delete the top-level instruction and documentation folders.
4. Keep old copied SDK guardrails for old paths, old macros, old presets, and old checkout wiring.
5. Add new guardrails that permit only the app-owned SDK path `src/sdk/core/modeling`.
6. Update affected-check routing and guardrail tests so instruction and architecture changes route
   through root guidance.

Exit criteria:

- no source file asks agents to read deleted top-level instruction or documentation folders;
- guardrails validate root guidance and this review;
- old copied SDK names remain banned;
- `src/sdk/core/modeling` is the only allowed SDK source path.

### Phase 1 - Additive SDK Build Boundary

Goal: prove a portable SDK-shaped build without moving behavior.

Actions:

1. Add SDK CMake wiring for:
   - `quader_modeling_sdk_geometry`
   - `quader_modeling_sdk_polygon`
   - `quader_modeling_sdk_qdr_document`
2. Add `QUADER_BUILD_NATIVE_APP` with default `ON`.
3. Gate native app dependency discovery, app resources, material compilation, UI, render backend,
   platform app shell, examples that require the app, and executable targets behind
   `QUADER_BUILD_NATIVE_APP`.
4. Remove broad `src` include roots from SDK targets.
5. Use explicit source lists for SDK targets.
6. Add target dependency guardrails.
7. Add one CMake configure/build test for SDK-only targets.

Exit criteria:

- SDK-only configure builds core SDK targets without Filament, SDL3, ImGui, native app resources,
  or the app executable;
- native app build remains unchanged with default options;
- guardrails reject native target dependencies from SDK targets.

### Phase 2 - Header Hygiene And DTO Split

Goal: make candidate SDK headers clean before physical moves.

Actions:

1. Split `src/editor/modeling/modeling_types.hpp` into SDK-neutral DTOs and native adapter DTOs.
2. Remove `EntityId`, `MeshAsset`, `ViewportOverlayFrame`, scene preview transforms, scene
   selection records, app materials, asset references, and native viewport cameras from SDK-neutral
   headers.
3. Move runtime object to scene entity maps into native adapter code.
4. Add SDK `Result<T>`, structured errors, operation diagnostics, dirty flags, element references,
   and typed generational IDs.
5. Add neutral `MeshPayload`, `AuthoredPolygonPayload`, `SemanticOverlayPayload`, and
   `MaterialPayload` records.
6. Add banned symbol/include guardrails for SDK public headers:
   - `SceneDocument`
   - `ProjectDocument`
   - `ProjectSession`
   - `AssetRegistry`
   - `ModelingSessionService`
   - `SceneRenderBridge`
   - `ViewportOverlayFrame`
   - `QuaderEditorApi`
   - `SDL`
   - `ImGui`
   - `filament`
   - `entt`
   - `nlohmann`

Exit criteria:

- SDK-neutral headers compile without native owners;
- native app adapters compile through explicit conversions;
- SDK public headers have automated banned-include coverage.

### Phase 3 - Polygon Core And Operation Contracts

Goal: package pure modeling behavior and stabilize operation outputs.

Actions:

1. Move geometry and polygon implementation into the SDK boundary after Phase 1 and Phase 2
   guardrails pass.
2. Update all include paths at moved call sites.
3. Standardize operation results with:
   - success/error status;
   - error code;
   - diagnostic message;
   - created, deleted, affected, and modified element deltas;
   - dirty flags;
   - selection remap result.
4. Add typed generational `VertexId`, `EdgeId`, `FaceId`, and canonical `EdgeKey`.
5. Centralize selection remapping for destructive operations.
6. Run automatic document validation and selection-liveness validation in debug/test paths after
   operations.
7. Convert high-risk operations first:
   - merge/remove doubles;
   - dissolve/delete;
   - bevel;
   - bridge;
   - connect/split;
   - slice/plane cut;
   - extrude/inset/thicken;
   - transform selection.

Exit criteria:

- polygon loop tests pass through SDK targets;
- operation results are consumed by runtime/app layers without inspecting operation internals;
- selection remap behavior is centralized.

### Phase 4 - Portable QDR And Mesh IO Split

Goal: separate portable document interchange from native project persistence.

Actions:

1. Create portable QDR document IO under the SDK boundary.
2. Move or wrap portable pieces:
   - `qdr_document`
   - `qdr_polygon_document_codec`
   - `qdr_root`
   - `qdr_diagnostic`
3. Keep native project save/load under native IO:
   - `ProjectDocument` hydration/extraction;
   - `AssetRegistry` hydration/extraction;
   - `SceneDocument` entity/component wiring;
   - app material/texture packaging;
   - project dirty/current path state.
4. Add neutral DTOs:
   - `QdrDocumentDto`
   - `QdrMeshDto`
   - `QdrMaterialDto`
   - `ObjMeshView`
5. Move polygon document mesh compilation behind a portable mesh compiler adapter.
6. Split portable path/stream/buffer writing from native Win32 durable replace policy.
7. Change OBJ writer to consume neutral mesh spans/views.
8. Keep `MeshAsset` conversion in native project/API adapters.

Exit criteria:

- portable QDR document IO target contains no scene/assets/editor/API/render/UI/platform includes;
- native project save/open behavior remains unchanged;
- QDR document and project tests pass.

### Phase 5 - SDK Public API

Goal: add the standalone modeling API without disturbing native APIs.

Actions:

1. Add SDK include boundary:

```text
src/sdk/core/modeling/include/quader/modeling/
  modeling.hpp
  result.hpp
  ids.hpp
  mesh/
  operations/
  session/
  commands/
  tools/
  render/
  io/
```

2. Add the core C++ surface:
   - SDK `Result<T>`;
   - typed generational IDs;
   - polygon document facade;
   - editable mesh facade;
   - mesh selection;
   - validation;
   - portable QDR document roundtrip;
   - one representative operation.
3. Add high-level session workflow:
   - `ModelingSession`;
   - `EditableMesh`;
   - selection;
   - operation commit;
   - undo/redo.
4. Add SDK examples:
   - standalone mesh operation;
   - session selection plus undo/redo;
   - QDR roundtrip.
5. Keep native `QuaderEditorApi` and `QuaderApi` unchanged.

Exit criteria:

- a consumer includes `quader/modeling/modeling.hpp` and runs one modeling workflow without linking
  the native app;
- native public APIs delegate overlapping modeling behavior to SDK owners.

### Phase 6 - Runtime And Preview Extraction

Goal: extract headless modeling runtime below native adapters.

Actions:

1. Extract runtime-neutral session state:
   - documents and objects owned by SDK runtime;
   - runtime selection;
   - operation registry;
   - preview transactions;
   - picking policy;
   - semantic overlay payloads;
   - repeat-last state;
   - pure tool state/math.
2. Keep native:
   - `ModelingSessionService`;
   - `ModelingMeshSyncService`;
   - `SceneModelingSyncService`;
   - scene/entity/material/asset binding;
   - command palette and typed Quader command IDs;
   - `EditorHistoryService` integration;
   - viewport panel and render bridge wiring.
3. Replace whole-session preview copies with affected-object or patch-style updates after parity
   tests lock behavior.
4. Add parity transcript tests comparing native API workflows against SDK runtime workflows.

Exit criteria:

- runtime loop tests pass through SDK runtime targets;
- native services are adapters over runtime algorithms;
- preview update/commit/cancel semantics remain unchanged.

### Phase 7 - Host Input, Viewport, UI, And Render Adapters

Goal: make host boundaries explicit without rewriting the UI first.

Actions:

1. Introduce `ViewportHostInputSnapshot`:
   - pointer position;
   - viewport rect;
   - wheel/buttons/modifiers;
   - focus/text/nav capture;
   - pressed chords;
   - frame timing.
2. Keep SDL and ImGui conversion in UI/platform adapter code.
3. Extract viewport command resolution from `ViewportPanel` into viewport/input owners.
4. Narrow `UIContext` into per-panel intent and read-only presentation contexts.
5. Move frame-all/frame-selection bounds logic into viewport/editor services.
6. Move point-light picking candidates, pivot snap candidate scanning, and overlay composition out
   of `ViewportPanel`.
7. Neutralize render overlay inputs:
   - render selection context;
   - neutral authored-wire records;
   - topology traversal in extraction/projection.
8. Convert render snapshots toward owned or pinned payloads.
9. Expand material snapshots into renderer-neutral PBR payloads.

Exit criteria:

- UI is a host surface and presentation coordinator;
- render consumes renderer-neutral snapshots;
- Qt or engine adapters have one input/render translation boundary.

### Phase 8 - Performance And Dirty-Flag Work

Goal: make the extracted architecture efficient.

Actions:

1. Add measured profiling scenarios:
   - idle large scene;
   - orbit camera;
   - dense mesh hover;
   - component drag one object among many;
   - box preview idle/update;
   - mirror preview;
   - overlay/wireframe enabled.
2. Instrument:
   - render snapshot building;
   - render sync;
   - modeling mesh projection;
   - object mesh compilation;
   - picking geometry signatures;
   - preview mesh generation;
   - overlay snapshot builders.
3. Move dirty gates earlier:
   - skip render snapshot construction when scene/assets revisions are unchanged;
   - pass previous snapshots through render API access;
   - compile only affected preview objects;
   - cache picking geometry signatures by object revision, transform, and pick-affecting options;
   - cache preview mesh outputs by descriptor and source revisions.
4. Preserve behavior with focused tests before optimizing state copying.

Exit criteria:

- expensive hot-path work is skipped before compile/hash/regenerate steps;
- measurements record before and after results for targeted scenarios.

### Phase 9 - SDK Tests, Packaging, And CI

Goal: prove the guide's portability promise continuously.

Actions:

1. Add `quader_modeling_sdk_consumer_tests`.
2. Add a minimal external CMake consumer fixture:
   - includes only exported SDK headers;
   - links only SDK targets;
   - creates a mesh/document;
   - runs one operation;
   - validates;
   - round-trips portable QDR.
3. Add SDK parity transcripts:
   - create box;
   - select object/component;
   - transform;
   - preview/commit/cancel;
   - bevel;
   - bridge;
   - thicken;
   - slice;
   - QDR roundtrip.
4. Add mesh-operation golden fixtures for high-risk topology operations.
5. Add deterministic seeded fuzz/property tests around validation, malformed topology, operation
   preconditions, and serialize/deserialize round trips.
6. Add SDK-local guidance and examples under the SDK source boundary.
7. Add CMake install/export package files and license bundle generation.
8. Add CI coverage to the existing automation path:
   - SDK-only configure/build/test;
   - native configure/build/test;
   - final `quader` build.

Exit criteria:

- SDK portability is proven by automation;
- native app behavior remains protected by existing tests;
- external consumers have examples that avoid native internals.

### Phase 10 - Compatibility Cleanup

Goal: remove transitional scaffolding after boundaries are real.

Actions:

1. Delete temporary aliases and migration helpers after call sites migrate.
2. Tighten guardrails:
   - no broad runtime grab bags;
   - no native owner includes in SDK;
   - no descriptor-only wrapper files when catalog data is enough;
   - no layer-ambiguous file names;
   - no live `AssetRegistry` overloads in render binding after snapshots own the path.
3. Update root `AGENTS.md`, this review, nested package guides, and code maps embedded near owners
   to match actual source layout.
4. Keep old copied SDK guardrails.

Exit criteria:

- no transitional SDK/app boundary exceptions remain;
- source layout, CMake, tests, examples, and guardrails tell the same story.

## First Milestone

The first milestone is fixed:

1. Root `AGENTS.md` is self-contained.
2. Top-level instruction and documentation folders are deleted.
3. This review contains the active deterministic architecture plan.
4. Guardrails route instruction, architecture, and example changes through root guidance and live
   examples.
5. Add `src/sdk/core/modeling` as the only allowed SDK path.
6. Add additive SDK wrapper targets:
   - `quader_modeling_sdk_geometry`
   - `quader_modeling_sdk_polygon`
   - `quader_modeling_sdk_qdr_document`
7. Add `QUADER_BUILD_NATIVE_APP`.
8. Add SDK target dependency guardrails.
9. Add one SDK consumer test.
10. Build the native app.

Verification for this milestone:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target quader_guardrail_tests
build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no
cmake --build --preset vs2026-debug --target quader
```

## Do Not Do

- Do not move `ModelingSessionService` into the SDK.
- Do not rename `QuaderEditorApi` or `QuaderApi` into the SDK API.
- Do not expose `SceneDocument`, `ProjectDocument`, `ProjectSession`, `AssetRegistry`,
  `SceneRenderBridge`, `ViewportOverlayFrame`, or native command IDs from SDK public headers.
- Do not create a top-level copied SDK package.
- Do not add old SDK-specific presets.
- Do not start with a large physical file move before target and header guardrails exist.
- Do not let render caches become modeling topology owners.
- Do not let UI panels own command, scene, viewport, render, asset, history, or modeling truth.
- Do not treat native test coverage as proof of SDK portability.
- Do not add compatibility forwarding headers that survive the migration patch that needed them.

## Verification Map

Use long build/test timeouts.

Instruction, architecture, guardrail, and example changes:

```powershell
cmake --build --preset vs2026-debug --target quader_guardrail_tests
build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no
cmake --build --preset vs2026-debug --target quader
```

Polygon/core SDK changes:

```powershell
cmake --build --preset vs2026-debug --target quader_modeling_domain_polygon_loop_tests quader_guardrail_tests
build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no
build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no
cmake --build --preset vs2026-debug --target quader
```

Runtime/preview/tool changes:

```powershell
cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests quader_tests
build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no
build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*Preview*:*Transform*:*Viewport*
cmake --build --preset vs2026-debug --target quader
```

Render/UI/viewport adapter changes:

```powershell
cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests
build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*Viewport*:*Render*:*Overlay*:*Input*
build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no
cmake --build --preset vs2026-debug --target quader
```

QDR/IO changes:

```powershell
cmake --build --preset vs2026-debug --target quader_modeling_domain_polygon_loop_tests quader_tests
build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=*Qdr*
build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*Qdr*:*ObjExport*
cmake --build --preset vs2026-debug --target quader
```

Cross-cutting or target/package changes:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target quader_modeling_domain_tests quader_tests quader_guardrail_tests
ctest --preset vs2026-debug --output-on-failure
cmake --build --preset vs2026-debug --target quader
```

## Consolidated Agent Findings

| Area | Final Finding |
| --- | --- |
| Governance | Root `AGENTS.md` and this review are the active authorities. |
| Build | Lower modeling targets exist; SDK wrapper targets and native app gating are next. |
| Core modeling | Mesh/polygon code is the SDK seed; IDs, results, dirty flags, and remaps need SDK contracts. |
| Public API | Native APIs stay host-facing; SDK APIs are separate under `quader::modeling`. |
| UI/viewport | Semantic input exists; `ViewportPanel` and UI contexts need owner extraction. |
| Render | Filament is contained; snapshots and overlays need neutral stable payloads. |
| IO/assets | Native project IO remains native; portable QDR document IO moves to SDK. |
| Tests | Native tests are strong; SDK consumer, target, header, golden, and fuzz tests are required. |
| Performance | Dirty gates move before compile/hash/regenerate work. |
| Migration | Start with authority, guardrails, additive targets, DTO hygiene, and consumer tests. |

The core theme is fixed: Quader has many strong native pieces, and the SDK promise becomes real only
when CMake targets, headers, guardrails, tests, examples, and host adapters enforce the same
direction.
