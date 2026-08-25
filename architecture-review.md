<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Architecture Review Against The Portable Modeling SDK Guide

Updated: 2026-08-25

Historical provenance: `modeling_app_architecture_guide.md` was an external audit input. It is not
repository authority and is not required to execute the migration.

This review is Quader's fixed architecture-direction authority. It consolidates the 10-agent audit
and a local repository pass. Executable task IDs, dependencies, acceptance state, and progress live
only in the linked root `REFACTORING_PLAN.md`; historical `AR-*` phases below are evidence and
crosswalk labels, not a second task queue. The final direction is fixed:

- Quader remains the native standalone Windows editor.
- The app-owned portable modeling core under `src/sdk/core/modeling` is the modeling source of
  truth.
- The SDK namespace is `quader::modeling`.
- `QuaderEditorApi` and `QuaderApi` remain native host/editor APIs.
- The old copied SDK workflow stays banned.
- Top-level instruction authority lives in root `AGENTS.md`.
- The deleted top-level instruction and documentation folders have no authority.

## Executive Verdict

Quader has a completed portable-modeling ownership boundary inside its native-app architecture.
The public editor APIs remain coherent host facades. Canonical mesh and polygon behavior is
SDK-owned, while source-only neutral declarations let native owner targets consume the same types
without exposing SDK-private headers. Rendering consumes neutral snapshots and keeps Filament in
the render owner. Viewport input is semantic and toolkit-neutral below the UI adapter. Modeling
domain tests, portable consumer/parity checks, and architecture guardrails protect the boundary.
QDR project save/open remains correctly split: portable document IO is SDK-owned, while file
dialogs, durable native writes, and project-session state remain native.

The current native host shell is retained Qt Widgets: `QApplication` owns the event loop,
`UISystem` composes a `QMainWindow` with `QDockWidget` panels under the `Fusion` style and Quader's
application-owned Obsidian palette, and the central
`QtViewportPanel` embeds one to four native-child `QWindow` Filament surfaces. This is the host
boundary around the completed SDK ownership model; it does not move UI or renderer concepts into
the portable core.

Quader now has a real portable modeling SDK boundary under `src/sdk/core/modeling`: the umbrella
header, geometry, polygon, QDR document, runtime, install/export package, examples, SDK-only build,
and clean installed external-consumer check are present. Native and SDK polygon/QDR targets compile
the same canonical portable sources. The SDK-only vcpkg path disables default features and enables
only the test dependency, so it does not install Qt, EnTT, GLM, Sentry, or other native-host
packages.

Every production modeling operation family now routes through the workspace-local SDK
`ModelingSession` owned by the `ModelingSdkProjectionAdapter`. Native commands and tools adapt
intent once, then publish SDK receipts to scene, assets, selection, history, and renderer-neutral
snapshots through explicit transactions. The replaced native runtime, dual-write paths, operation
catalog, mesh mirrors, and migration-only box fallback are gone. Native DTOs retain only host-owned
scene, asset, viewport, overlay, project, and platform concepts. Revision gates, parity transcripts,
operation-specific behavior tests, QDR compatibility fixtures, external consumers, and the final
CI matrix protect the completed boundary.

## Resolved Architecture Decisions

1. Direction authority: keep modeling truth in the app-owned portable core described in this
   review; any follow-up architecture work uses `REFACTORING_PLAN.md` task IDs or a separately
   approved successor plan.
2. Source path: `src/sdk/core/modeling`.
3. Public namespace: `quader::modeling`.
4. Umbrella header: `quader/modeling/modeling.hpp`.
5. Native app APIs: keep `QuaderEditorApi` and `QuaderApi` as native host APIs.
6. SDK APIs: add separate SDK APIs; do not rename native APIs into SDK APIs.
7. Old copied SDK: keep old package paths, old macros, old presets, and old external checkout
   wiring banned.
8. Build mode: use the default-on native app build option named `QUADER_BUILD_NATIVE_APP`; SDK-only
   configure builds SDK targets without Filament, Qt Widgets, legacy SDL/ImGui backends, native
   resources, or the app executable.
9. SDK CMake targets:
   - `quader_modeling_sdk_geometry`
   - `quader_modeling_sdk_polygon`
   - `quader_modeling_sdk_qdr_document`
   - `quader_modeling_sdk_runtime`
   - `quader_modeling_sdk`
10. SDK dependency policy: native vcpkg dependencies live in the default `native-app` feature;
    SDK-only verification disables defaults and enables only the `tests` feature.
11. ID policy: SDK public IDs are typed and generational.
12. Result policy: SDK operations return structured `Result<T>` values with error codes,
    diagnostics, dirty flags, element deltas, and selection remap data.
13. Compatibility policy: update includes and call sites during moves. Do not create long-lived
    forwarding headers.
14. Preset policy: keep the native app presets. Add SDK-only configure/build coverage through
    CMake options, targets, and tests, not old SDK presets.
15. Test policy: SDK portability is proven by a standalone consumer test that links only SDK
    targets.
16. Documentation policy: root `AGENTS.md` and this review hold fixed architecture guidance;
    `REFACTORING_PLAN.md` alone holds executable refactoring tasks and progress. New local owner
    guidance belongs in the nearest nested `AGENTS.md` file.
17. Native UI baseline: keep the `QApplication`/`QMainWindow`/`QDockWidget` shell on Qt `Fusion`,
    with exactly one application-owned Obsidian palette derived from
    `fusion_style->standardPalette()`. The palette deliberately defines active, inactive, and
    disabled surfaces, text, borders, links, and interaction colors. Fusion supplies native
    control frames and state mechanics; the scoped toolbar proxy adapts only palette-derived hover
    and checked face colors plus checked and keyboard-focus outlines. An explicit Windows high-contrast preference
    bypasses the Obsidian colors and clears Quader's explicit palette so Qt keeps resolving live
    Windows system colors. Do not add an application-wide stylesheet, custom font, or frameless
    chrome. The scoped dock-title QSS uses palette roles and switches between matching packaged
    light and dark PNG glyphs when the inherited palette changes. A local `QPalette::Button`
    override remains
    reserved for semantic color swatches, and semantic X/Y/Z labels may set only local
    `QPalette::WindowText`. The one-to-four pane workspace remains owned by
    `ViewportWorkspaceService`; Qt widgets only adapt and present it.

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
5. Tools are host-agnostic. Raw Qt/platform events become canonical Quader input at the UI boundary;
   SDK tool calls receive the existing `ViewportHostInputSnapshot` and `ViewportCameraSnapshot`
   DTOs through the viewport-owned SDK adapter.
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

This remains a native host layer and adapts modeling intent to SDK-owned behavior.

### Mesh And Polygon Core

`src/sdk/core/modeling/internal/mesh` is the canonical geometry and polygon implementation:

- geometry, polygon documents, topology, picking, compilation, and operations are isolated from
  the Qt host UI, legacy UI backends, and Filament;
- source-only declarations under
  `src/sdk/core/modeling/neutral/include/quader/modeling/neutral` are shared by the SDK
  implementation and native modeling owner targets without exposing SDK-private headers;
- the former `src/mesh` mirror tree is deleted and guarded against reintroduction;
- `quader_modeling_polygon` links to geometry, optional OpenMesh, and profiling support;
- modeling-domain tests cover polygon documents, liveness, validation, operations, QDR document IO,
  runtime operations, runtime tools, and editor metadata.

### Final Compatibility Disposition

The migration has no selected-owner switch, dual modeling runtime, native mesh mirror, or direct
box-commit fallback. `ModelingSdkProjectionAdapter`, viewport host/input adapters, and render
projection adapters are the supported final host boundaries, not temporary exceptions.

Four portable SDK 0.1.x shims remain deliberately supported and are installed with the package
ledger at `src/sdk/core/modeling/COMPATIBILITY.md`: the borrowed document pointer, the void batch
callback, and the unsupported pivot/profiling facades. Each has a safe replacement, consumer
behavior coverage, an SDK owner, and a 0.2.0 removal review. The eight deprecated native typed
command spellings in `quader_command_ids.hpp` remain source aliases for their identical canonical
IDs; `ProgrammaticApiGuardrailTests.PublicCommandAliasesStayExplicitAndOutOfTypedCatalog` prevents
them from becoming duplicate catalog truth. Serialized QDR/project/settings compatibility readers
remain data-compatibility policy and are not refactor adapters.

### Command, History, And Preview Ownership

The native app already routes user-visible mutation through public APIs, command services, editor
services, document commands, and modeling runtime services. `EditorHistoryService`, document
commands, modeling mementos, and preview command services are established owners. Preview
begin/update/commit/cancel behavior has deep coverage for transform and generated topology
workflows.

These host services now adapt to the SDK session contract while retaining native command and UI
policy.

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
- `src/ui/qt_input_adapter.*` converts Qt key, pointer, wheel, modifier, focus, and text-capture
  state at the host boundary;
- `src/viewport/modeling_sdk_viewport_input_adapter.*` converts canonical viewport state into the
  public SDK `ViewportHostInputSnapshot`/`ViewportCameraSnapshot` DTOs;
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

Current modeling-domain targets that stay valid during SDK adapter migration:

- `quader_geometry`
- `quader_modeling_polygon`
- `quader_modeling_runtime`
- `quader_editor_metadata`
- `quader_modeling_domain_tests`
- `quader_modeling_domain_polygon_loop_tests`
- `quader_modeling_domain_runtime_loop_tests`
- `quader_modeling_domain_metadata_loop_tests`

Current portable targets are `quader_modeling_sdk_geometry`,
`quader_modeling_sdk_polygon`, `quader_modeling_sdk_qdr_document`,
`quader_modeling_sdk_runtime`, and the umbrella `quader_modeling_sdk`. They install through
`QuaderModelingSdk`, and a separate consumer configures against the installed package.

## Main Gaps

### SDK Build Boundary Already Established

Required state:

- `src/sdk/core/modeling` exists;
- SDK targets build without native app dependencies;
- `QUADER_BUILD_NATIVE_APP` gates app, Qt Widgets UI, render backend, native resources, Filament,
  and executable setup;
- SDK targets use narrow include roots;
- SDK targets have no transitive link to native app targets;
- a standalone consumer test links only SDK targets.

Current state:

- `src/sdk/core/modeling` and `quader/modeling/modeling.hpp` are live;
- native dependency discovery is gated by `QUADER_BUILD_NATIVE_APP`;
- SDK examples link only `quader_modeling_sdk`;
- SDK targets have install/export metadata and a clean external-consumer build;
- SDK-only vcpkg configuration installs only the `tests` feature dependency;
- dependency and public-header guardrails reject native owner leakage.

### Native Adapter Extraction Is Incomplete

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

### Topology Identity And Result Consistency Still Need Hardening

Public SDK IDs are already typed/generational, SDK operations already return structured
`Result<T>`, and operation results already carry dirty flags, element deltas, diagnostics, and
selection remaps. The remaining gap is semantic enforcement: the polygon core still stores raw
`ElementId`, public topology generations are effectively fixed at one, undo branching can reuse an
issued index, and result/typed-selection values can represent inconsistent arms or kinds.

Remaining required state:

- public IDs are typed and generational;
- stale ID detection is part of SDK API behavior;
- operations return structured success/error;
- results carry created, deleted, affected, and modified elements;
- results carry dirty flags and selection remap data;
- destructive operation remap behavior is centralized.

### QDR And OBJ IO Are Native-App Shaped

Current native behavior is correct for the app, but not portable:

- QDR scene read/write headers expose `SceneContentDocument`;
- reader hydration touches `SceneContentDocument`, `AssetRegistry`, and `SceneDocument`;
- QDR reader includes editor projection to compile polygon documents into mesh assets;
- OBJ writer consumes `MeshAsset`;
- durable replace logic has native Win32 policy.

Required state:

- portable QDR document IO target has neutral DTOs and no scene/assets/editor/API/render/UI/platform
  includes;
- native project persistence owns hydration/extraction adapters;
- OBJ writer consumes neutral mesh spans/views;
- Win32 durable replace stays native.

### Render Snapshots Need Stronger Contracts

Current render snapshots are useful but still too coupled:

- scene mesh/light records are value inputs and the scene source/revision gates already reuse
  unchanged snapshots;
- overlay snapshots still expose authored `quader_poly::Document`/topology spans and need an
  explicit stable or same-frame lifetime at the neutral seam;
- some overlay/material preparation still occurs before the narrowest owner revision gate;
- overlay builders traverse authored polygon topology and include modeling/editor concepts;
- `SceneRenderBridge` request types expose modeling selection concepts;
- material/texture services still have live `AssetRegistry` overloads.

Required state:

- renderer-facing snapshots declare stable owned, pinned, or explicitly borrowed lifetimes;
- semantic overlay records come from extraction/projection layers;
- render builders do not become topology owners;
- dirty gates run before expensive snapshot construction;
- material payloads become renderer-neutral PBR records.

### UI And Viewport Boundary Is Too Wide

The host boundary is now explicit: `UISystem` owns a retained Qt shell, `QtViewportPanel` presents
the one-to-four pane workspace, `QtViewportSurface` owns native-child surfaces, and the
`qt_input_adapter` functions convert raw Qt input. The remaining extraction gap is that
`QtViewportPanel`
still coordinates input frames, interaction routing, pane render preparation, and frame commands.

Required state:

- UI panels present state and dispatch intent;
- viewport command resolution lives in a viewport/input owner;
- frame bounds, light picking candidates, pivot snap scanning, and overlay composition live in
  owner services;
- `ViewportToolContext` stops carrying native scene/runtime service concepts into tool code that
  belongs below the host boundary;
- Qt event and widget types stay at the UI adapter boundary.

### Some Performance Dirty Gates Still Run Too Late

Already-established scene-source matching, transform-only refresh, overlay snapshot caching, and
retained-UI revision gates are protected by deterministic tests. Remaining observed hot paths are:

- transform preview sync can compile every modeling object;
- hover cache validation hashes whole mesh geometry on mouse move;
- preview transactions copy whole session state;
- tool preview meshes regenerate and then get hashed by render caches.

Required state:

- revision/signature checks happen before compile/hash/regenerate steps;
- preview updates operate on affected objects;
- render API can reuse previous snapshots;
- picking signatures are cached by object revision, transform, and pick-affecting options.

### SDK Portability Coverage Needs Operation-Specific Depth

Established state:

- SDK consumer test, public-header hygiene, and dependency guardrails;
- SDK-only configure/build/test without native vcpkg packages;
- SDK target install/export and clean external-consumer check;
- SDK public examples;
- QDR golden fixtures plus deterministic seeded malformed/round-trip coverage.

Remaining state:

- broader native/SDK parity transcripts;
- focused golden/seeded/property cases co-located with the high-risk topology operation or
  persistence contract they protect, rather than a new blanket framework.

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

## Execution Authority And Legacy Crosswalk

Crosswalk status as of 2026-07-17:

- `architecture-review.md` owns fixed direction, boundaries, and do-not-do decisions.
- `REFACTORING_PLAN.md` owns the only executable task IDs, prerequisites, progress checkboxes, and
  completion evidence.
- `AR-*` below identifies this document's legacy roadmap phases. Those labels are historical and
  never authorize work by themselves.
- `Complete` means the legacy outcome is live and protected; `Partial` means useful pieces are live
  but named canonical work remains; `Open` means the legacy cleanup outcome has not yet landed.

| Legacy phase | Status | Live evidence | Remaining canonical work or disposition |
| --- | --- | --- | --- |
| AR-0 — Lock authority | Complete | Root instructions are self-contained, old copied-SDK shapes are banned, the app-owned SDK path is fixed, and architecture/test-policy changes route to guardrails | P0-T00/P0-T01/P0-T08 protect this one-way authority link; P6-T06/P6-T07 keep final documentation aligned |
| AR-1 — SDK build boundary | Complete | P0/P5/P6 produced isolated installable SDK targets, one canonical source graph, external consumers, total target policy, and clean MSVC/GCC/Clang/native CI jobs | Final supported boundary; native app remains Windows-only |
| AR-2 — Header/DTO split | Complete | P1/P3/P4/P5 leave installed SDK headers owner-neutral, neutral low-level declarations source-only, and native DTOs behind host adapters | No private SDK or native-owner include exception remains |
| AR-3 — Polygon/contracts | Complete | P1/P3/P5/P6 leave SDK-owned polygon sources, lifecycle-safe contracts, operation/metadata coverage, and no mirror tree | Automatic validation remains operation-owned and test-protected rather than a global release hook |
| AR-4 — QDR/mesh IO | Complete | P1/P3 provide portable binary QDR, atomic native transactions/import, exact topology conversion, and explicit lazy asset behavior | Native OBJ disposition remains deliberately scoped below |
| AR-5 — SDK public API | Complete | P1/P3/P5/P6 provide umbrella/install surfaces, safe handles/results/lifetimes, complete supported workflows, examples, and a versioned compatibility ledger | Four 0.1.x shims revisit at 0.2.0 |
| AR-6 — Runtime/preview | Complete | P1/P2/P3/P4 leave one SDK modeling session/preview/history truth per workspace and renderer-neutral records | Native command, clipboard, and hit adaptation remain host policy by disposition |
| AR-7 — Host adapters | Complete | P2/P4 provide Qt input translation, viewport decision owners, material/editor ownership, neutral render projection, and revision-gated retained UI | Stable adapters are final boundaries, not migration exceptions |
| AR-8 — Performance | Complete | P0/P2/P4/P6 protect revision/cache/allocation counters and comparable fixed-host raw evidence without flaky shared-CI timing thresholds | New optimizations remain evidence-gated |
| AR-9 — Tests/package/CI | Complete | P0/P2/P3/P5/P6 provide consumer/install/external checks, parity, native/modeling suites, affected routing, GCC/Clang/MSVC, sanitizers, and checksum-pinned native CI | GPU/window smoke remains the explicit local gate |
| AR-10 — Compatibility cleanup | Complete | P5/P6 removed mirrors, Taskflow, the unused QDR helper, dead abstractions, the box fallback, stale docs, and temporary exceptions while retaining only versioned/tested compatibility | Old copied-SDK bans and the compatibility ledger remain permanent governance |

### Legacy action coverage

The `A*` labels below refer to the numbered actions inside each retained AR section. They provide
total mapping without duplicating those action descriptions.

- **AR-0 actions:** A1/A2/A3/A4/A5/A6 are Complete. P0-T00/P0-T08 protect the authority/status
  link; P6-T06/P6-T07 preserve its accuracy through closeout.
- **AR-1 actions:** A1/A2/A3/A4/A5/A6/A7 are Complete through P0-T04/P0-T05/P0-T07,
  P5-T01/P5-T02, and P6-T01/P6-T04.
- **AR-2 actions:** A1/A2/A3/A4/A5/A6 are Complete through P1-T01/P1-T04/P1-T05/P1-T06,
  P2-T02, P3-T01/P3-T02/P3-T07, P4-T01, and P5-T02.
- **AR-3 actions:** A1/A2/A3/A4/A5/A6/A7 are Complete through P1-T01/P1-T05,
  P3-T02/P3-T04/P3-T06/P3-T08, P5-T04, and P6-T01. Validation is operation-owned and covered by
  focused debug/test contracts rather than a global release-only hook.
- **AR-4 actions:** A1/A2/A3/A4/A5/A6/A7/A8 are Complete or deliberately dispositioned through
  P1-T07/P1-T09 and P3-T09. `QdrDocumentDto` plus SDK mesh views are the neutral equivalents; the
  obsolete proposed names `QdrMeshDto` and `ObjMeshView` are not required, and A7 follows the OBJ
  disposition below.
- **AR-5 actions:** A1/A2/A3/A4/A5 are Complete through P1-T01/P1-T02/P1-T04/P1-T05/P1-T08,
  P2-T03/P2-T04, the P3 operation ledger, P5-T02, and P6-T06.
- **AR-6 actions:** A1/A2/A3/A4 are Complete or deliberately dispositioned through P1-T02/P1-T03,
  P2-T01 through P2-T05, P3-T01 through P3-T08, and P4-T01. Whole-session mementos are private
  transaction implementation, not a second public preview owner.
- **AR-7 actions:** A1/A2/A3/A4/A5/A6/A7/A8/A9 are Complete through P1-T06, P4-T01 through
  P4-T05, and P4-V01 through P4-V09.
- **AR-8 actions:** A1/A2/A3/A4 are Complete and protected by P0-T06/P0-T09, P2-T07, P4-T06,
  and P6-T05 evidence plus deterministic counter tests.
- **AR-9 actions:** A1/A2/A3/A4/A5/A6/A7/A8 are Complete or deliberately scoped through P2-T01,
  P3-T04/P3-T08, P5-T05/P5-T06, and P6-T03/P6-T04; the blanket expansion disposition below still
  prevents speculative test infrastructure.
- **AR-10 actions:** A1/A2/A3/A4 are Complete through P0-T05/P0-T08, P3-T07, P4-T01/P4-T05,
  P5-T01/P5-T02/P5-T06, and P6-T01/P6-T02/P6-T06/P6-T07.

### Explicit legacy-task dispositions

- **AR-4 OBJ writer:** `src/io/mesh/obj_writer.*` remains a native geometry-only exporter that
  consumes `MeshAsset`. A portable OBJ abstraction is deferred until a real standalone SDK
  consumer or second exporter exists; portable QDR and neutral SDK snapshots remain required.
- **AR-6 picking and repeat ownership:** native hit adaptation, command IDs, clipboard, and repeat
  routing remain host-owned under D-006 and P3-T02/P3-T05. The SDK owns modeling selection,
  operations, preview state, and receipts. The broader legacy wording is superseded by this split.
- **AR-7 general `UIContext` decomposition:** Complete. The live `UIIntentContext`,
  `UIScenePresentationContext`, `UIInspectorPreviewContext`, and `UIPreferencesContext` split owns
  the proven seams. A further blanket context rewrite is rejected; P4-T04 separately owns the
  evidenced material mutation issue.
- **AR-8 speculative picking/preview caches:** the listed revision gates, previous-snapshot reuse,
  picking signatures, and preview caches already exist and remain protected. Only a newly proposed
  optimization requires P0-T09 evidence before the corresponding task adds the smallest cache or
  earlier gate. Unmeasured cache work is deferred.
- **AR-9 blanket golden/fuzz expansion:** current QDR golden and deterministic seeded coverage makes
  the old “absent” premise stale. Additional fixtures/properties co-land with the applicable P3-T04
  operation and P3-T08 verification row rather than becoming a repository-wide framework project.

## Historical Progressive Migration Plan

The labels in this section are retained as `AR-0` through `AR-10` so earlier evidence remains
traceable. They describe the original direction and are not executable phase IDs. Use the crosswalk
above and the stable `P*-T*` IDs in `REFACTORING_PLAN.md` for all implementation work.

Historical snapshot recorded 2026-07-13; superseded by the final 2026-07-17 crosswalk above:

- AR-0 through AR-10 are complete or explicitly dispositioned by the canonical P0-P6 work.
- The public SDK contracts, runtime target, canonical polygon/QDR ownership, packaging, examples,
  SDK consumer, install/export checks, portable/native CI, and compatibility ledger are live.
- Stale blanket golden/fuzz, speculative-cache, native OBJ, and broader host-policy wording remains
  governed by the explicit dispositions rather than being silently treated as unfinished work.
- The native mesh mirror tree and migration-only box fallback are deleted; guardrails prevent both
  from returning as parallel ownership paths.

### AR-0 — Lock Authority

Goal: make the architecture direction legal and unambiguous.

Actions:

1. Keep root `AGENTS.md` as the single self-contained agent authority.
2. Keep this review as fixed architecture direction and link executable work to
   `REFACTORING_PLAN.md`.
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

### AR-1 — Additive SDK Build Boundary

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

- SDK-only configure builds core SDK targets without Filament, Qt Widgets, legacy SDL/ImGui
  backends, native app resources, or the app executable;
- native app build remains unchanged with default options;
- guardrails reject native target dependencies from SDK targets.

### AR-2 — Header Hygiene And DTO Split

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
   - `SceneContentDocument`
   - `SceneSession`
   - `ProjectDocument`
   - `ProjectSession`
   - `AssetRegistry`
   - `ModelingSessionService`
   - `SceneRenderBridge`
   - `ViewportOverlayFrame`
   - `QuaderEditorApi`
   - `Qt`
   - `SDL`
   - `ImGui`
   - `filament`
   - `entt`
   - `nlohmann`

Exit criteria:

- SDK-neutral headers compile without native owners;
- native app adapters compile through explicit conversions;
- SDK public headers have automated banned-include coverage.

### AR-3 — Polygon Core And Operation Contracts

Goal: package pure modeling behavior and stabilize operation outputs.

Actions:

1. Move geometry and polygon implementation into the SDK boundary after AR-1 and AR-2 guardrails
   pass.
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

### AR-4 — Portable QDR And Mesh IO Split

Goal: separate portable document interchange from native project persistence.

Actions:

1. Create portable QDR document IO under the SDK boundary.
2. Keep the portable SDK surface binary-only through
   `quader_modeling_sdk_qdr_document`, with `std::vector<std::byte>` writes and
   `std::span<const std::byte>` reads. The retired JSON document/root/migration
   codecs and native runtime compatibility targets do not ship.
3. Keep native scene save/load and project-workspace persistence under native IO:
   - `SceneContentDocument` hydration/extraction;
   - folder-backed `ProjectSession` and `.qproj` manifest persistence;
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

### AR-5 — SDK Public API

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

### AR-6 — Runtime And Preview Extraction

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
   - Qt viewport panel and render bridge wiring.
3. Replace whole-session preview copies with affected-object or patch-style updates after parity
   tests lock behavior.
4. Add parity transcript tests comparing native API workflows against SDK runtime workflows.

Exit criteria:

- runtime loop tests pass through SDK runtime targets;
- native services are adapters over runtime algorithms;
- preview update/commit/cancel semantics remain unchanged.

### AR-7 — Host Input, Viewport, UI, And Render Adapters

Goal: keep the current Qt host boundary explicit while extracting reusable viewport and render
ownership below it.

Actions:

1. Introduce `ViewportHostInputSnapshot`:
   - pointer position;
   - viewport rect;
   - wheel/buttons/modifiers;
   - focus/text/nav capture;
   - pressed chords;
   - frame timing.
2. Keep Qt event conversion in `src/ui/qt_input_adapter.*`; lower owners consume canonical Quader
   input and semantic viewport frames only.
3. Extract remaining viewport command resolution from `QtViewportPanel` into viewport/input
   owners.
4. Narrow `UIContext` into per-panel intent and read-only presentation contexts.
5. Move frame-all/frame-selection bounds logic into viewport/editor services.
6. Move point-light picking candidates, pivot snap candidate scanning, and overlay composition out
   of `QtViewportPanel`.
7. Neutralize render overlay inputs:
   - render selection context;
   - neutral authored-wire records;
   - topology traversal in extraction/projection.
8. Convert render snapshots toward owned or pinned payloads.
9. Expand material snapshots into renderer-neutral PBR payloads.

Exit criteria:

- UI is a host surface and presentation coordinator;
- render consumes renderer-neutral snapshots;
- the Qt host has one input/render translation boundary.

### AR-8 — Performance And Dirty-Flag Work

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

### AR-9 — SDK Tests, Packaging, And CI

Goal: prove the guide's portability promise continuously.

Actions:

1. **Complete:** `quader_modeling_sdk_consumer_tests` is live.
2. **Complete:** the external CMake consumer fixture:
   - includes only exported SDK headers;
   - links only SDK targets;
   - creates a mesh/document;
   - runs one operation;
   - validates;
   - round-trips portable QDR.
3. **Complete under the operation-ledger disposition:** retained native/SDK parity transcripts
   cover the migrated user-level workflows, while operation-specific semantics stay in SDK and
   modeling-domain behavior suites for:
   - create box;
   - select object/component;
   - transform;
   - preview/commit/cancel;
   - bevel;
   - bridge;
   - thicken;
   - slice;
   - QDR roundtrip.
4. **Complete under the fixture disposition:** operation-specific golden fixtures exist for
   high-risk topology behavior not already protected by QDR golden coverage; no blanket fixture
   expansion is required.
5. **Complete under the seeded-coverage disposition:** deterministic validation,
   malformed-input, and round-trip properties co-land with the operation or persistence contract
   they protect.
6. **Complete:** SDK-local guidance and examples live under the SDK source boundary.
7. **Complete:** CMake install/export package files and license evidence are live.
8. **Complete:** CI runs the no-Filament SDK graph with MSVC, GCC, and Clang; a focused Clang
   ASan/UBSan job covers the SDK consumer suite; and checksum-pinned Filament plus pinned OpenMesh
   provision the complete native Windows configure/build/CTest/app job.

Exit criteria:

- SDK portability is proven by automation;
- native app behavior remains protected by existing tests;
- external consumers have examples that avoid native internals.

### AR-10 — Compatibility Cleanup

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

## Initial Milestone Delivered

The first milestone delivered:

1. Root `AGENTS.md` is self-contained.
2. Top-level instruction and documentation folders are deleted.
3. This review contains fixed architecture direction and links the executable
   `REFACTORING_PLAN.md` tracker.
4. Guardrails route instruction, architecture, and example changes through root guidance and live
   examples.
5. `src/sdk/core/modeling` as the only allowed SDK path.
6. SDK targets:
   - `quader_modeling_sdk_geometry`
   - `quader_modeling_sdk_polygon`
   - `quader_modeling_sdk_qdr_document`
   - `quader_modeling_sdk_runtime`
   - `quader_modeling_sdk`
7. `QUADER_BUILD_NATIVE_APP` gating of native dependency discovery and targets, with the SDK-only
   graph proven free of OpenMesh/native modeling owners.
8. SDK target dependency and manifest-feature guardrails with total coverage of every live
   first-party target and explicit P4/P5 removal ownership for temporary reverse edges.
9. SDK consumer tests, public examples, install/export, and a clean external consumer.
10. Native app compatibility verification.

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
- Do not expose `SceneDocument`, `SceneContentDocument`, `SceneSession`, `ProjectDocument`,
  `ProjectSession`, `AssetRegistry`,
  `SceneRenderBridge`, `ViewportOverlayFrame`, or native command IDs from SDK public headers.
- Do not create a top-level copied SDK package.
- Do not add old SDK-specific presets.
- Do not start with a large physical file move before target and header guardrails exist.
- Do not let render caches become modeling topology owners.
- Do not let UI panels own command, scene, viewport, render, asset, history, or modeling truth.
- Do not treat native test coverage as proof of SDK portability.
- Do not add compatibility forwarding headers that survive the migration patch that needed them.

## First Production Slice Ownership Contract

The box/select/translate-preview slice has one modeling owner and one host
projection boundary:

- Every `SceneSession` owns one `ModelingSessionService`, whose
  `ModelingSdkProjectionAdapter` owns exactly one `quader::modeling::ModelingSession`.
  The adapter and its SDK/native object map are workspace-scoped. Activating a
  different scene changes the active facade binding; it never moves or shares
  SDK state or mappings between workspaces.
- The SDK session owns box polygon documents, typed object/element identities,
  component and object selection, translate preview base/current state,
  portable operation receipts, dirty/revision state, and SDK identity
  high-water marks. No persistent native polygon document mirrors an
  SDK-owned object. Temporary native picking projections are read-only input
  adapters and are discarded after the hit is committed through the SDK.
- The native workspace owns scene/editor entity identities, hierarchy and
  transforms, mesh/material asset identities, whole-editor history labels and
  grouping, project/session lifecycle, QDR project hydration, render
  invalidation, and Filament publication. `ModelingObjectComponent::sdk_owned`
  persists which modeling owner must hydrate a scene object after reload.
- Publication order is SDK mutation, mesh compilation/validation, asset
  staging, scene entity/bindings/material, scene selection and mapping, then
  host finalization/history commit. Asset, scene, mapping, selection, and SDK
  semantic checkpoints are restored together on failure. A failed command
  publishes no history or render-visible revision. SDK object/preview/topology
  allocator high-water marks are intentionally not rewound: aborted IDs may
  leave gaps, remain absent/stale, and can never alias a later live identity.
- Translate preview updates the same staged native mesh binding once per input
  event. Commit retains that projection and records one native history action;
  cancel restores the exact pre-preview asset/scene checkpoint. Read-only
  summary/export/render queries never compile or publish topology.
- Project replacement completes candidate load, validation, and manifest
  persistence before canceling old workspace transients. A late candidate
  failure therefore leaves the old SDK preview and workspace exact; successful
  replacement crosses the cancellation boundary once immediately before old
  workspace teardown.

The native `QuaderEditorApi`, `QuaderApi`, command IDs, public object handles,
and scene selection/history surfaces remain the compatibility boundary. The
removed native runtime box commit/create commands must not be reintroduced.

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
| Governance | Root `AGENTS.md` owns agent policy, this review owns fixed architecture direction, and `REFACTORING_PLAN.md` alone owns executable refactoring tasks/progress. |
| Build | SDK targets, native app gating, install/export, clean consumer, and vcpkg feature isolation are live. |
| Core modeling | Polygon and portable QDR implementation have one canonical SDK-owned compiled source; typed IDs, structured results, dirty flags, and remaps are public SDK contracts. |
| Public API | Native APIs stay host-facing; the separate SDK API is under `quader::modeling`. |
| UI/viewport | Qt Widgets/Fusion remains the host shell; semantic input, viewport decisions, and split UI contexts keep modeling and render truth in their owners. |
| Render | Filament is contained behind renderer-neutral scene, overlay, selection, and preview snapshots with early revision gates. |
| IO/assets | Native project IO remains native; portable QDR document IO is SDK-owned. |
| Tests | Native, SDK consumer, header/dependency guardrail, parity, package-consumer, QDR golden, deterministic seeded, fatal-path, and stress-fixture coverage protect the final graph. |
| Performance | Dirty gates run before compile/hash/regenerate work; deterministic counters and fixed-host raw comparisons provide regression evidence without flaky CI timing assertions. |
| Migration | Complete: replaced owners and mirrors are deleted; stable host adapters and four versioned SDK compatibility shims are explicitly governed. |

The final graph is one-way: native APIs and tools express host intent, the SDK transaction owns
modeling semantics, native projection publishes host state, and render consumes neutral snapshots.
CMake targets, manifest features, headers, guardrails, examples, consumers, and CI enforce that
boundary without widening the portable dependency graph.
