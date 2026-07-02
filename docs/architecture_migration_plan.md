<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Architecture Migration Plan

Internal architecture plan generated from a 10-agent audit against
`C:\Users\Drako\Desktop\modeling_app_architecture_guide.md` on 2026-07-02.

## Summary Verdict

Quader already follows several core ideas from the proposed architecture:

- `AGENTS.md` routing and nested package guidance are strong.
- `QuaderEditorApi` / `QuaderApi` are established front doors for user/editor intent.
- Scene edits mostly route through `DocumentCommandService`, `EditorHistoryService`, and public scene APIs.
- Mesh/polygon code is mostly engine-clean and separated from UI/render/app code.
- Render resources and overlay caches are concentrated under `src/render`.
- QDR I/O avoids UI file dialogs and has versioned format constants plus round-trip tests.

The repo is not yet a clean implementation of the proposed architecture. The biggest gaps are:

- CMake targets are too broad to enforce dependency direction.
- `ModelingSessionService`, `ProjectSession`, `UIContext`, and `RenderSystem` expose wide mutable surfaces.
- Mesh element IDs are weak, non-generational, and not split by element kind.
- Mesh validation is ad hoc instead of a central executable invariant.
- Asset mutations bypass undoable command ownership.
- Object selection has split mutation paths between document commands and modeling runtime projection.
- Render and QDR paths still consume live document/asset/runtime internals rather than stable snapshots.
- Internal architecture docs, code map, ADR/task templates, and a full dependency guardrail are missing.

## Migration Principles

- Preserve native app identity. Do not recreate an external SDK boundary.
- Keep `QuaderEditorApi` as the whole-editor intent API and `QuaderApi` as the document/modeling API.
- Move behavior to the owner that owns the invariant instead of adding parallel helpers.
- Add guardrails before broad refactors so each migration step is testable.
- Prefer thin facades over new large services. `ModelingSessionService` and `SceneRenderBridge` should delegate, not accumulate more ownership.
- Preserve behavior, ordering, revision stamping, undo/redo state, selection sync, cache invalidation, and QDR compatibility while moving code.

## Phase 0 - Baseline Docs And Guardrails

- [ ] Add internal architecture docs expected by the guide:
  `docs/architecture.md`, `docs/code_map.md`, `docs/decisions/ADR-template.md`,
  `docs/tasks/TASK-template.md`, and `docs/agent_mistakes.md`.
- [ ] Add a docs guardrail that requires those files to exist and be listed in
  `docs/public-docs-examples.txt` as internal docs.
- [ ] Add a dependency-matrix guardrail for project includes and CMake links. Start with a short
  allowlist for current exceptions, then burn the allowlist down.
- [ ] Fix stale guardrail expectations that no longer match current naming or command constants,
  especially command-ID spelling and camelCase checks.
- [ ] Broaden matching-header-first and self-contained-header checks beyond `src/editor/interactions`.
- [ ] Promote the cheap path-only part of `tools/check_cpp_style.py` into a guardrail/CI path, while
  keeping full clang-tidy as a heavier pre-commit or CI job.

## Phase 1 - Enforce Real Module Boundaries

- [ ] Define the repo-specific target map before moving files:
  `foundation/core`, `math`, `mesh_geometry`, `mesh_polygon`, `assets`, `scene_document`,
  `editor_commands`, `editor_modeling`, `viewport`, `render`, `io_qdr`, `api`, `ui`, and `app`.
- [ ] Split `quader_core`. It currently mixes assets, core utilities, document commands, history,
  selection, QDR I/O, scene, UI models, and viewport services in one target.
- [ ] Split `quader_runtime` into platform, render, UI, and app/runtime composition targets.
- [ ] Reduce broad `PUBLIC src` include exposure where possible after target splits.
- [ ] Move editor-service composition out of `src/scene/document/ProjectSession`, or split it into a
  pure `ProjectDocument` plus an app/editor-level session owner.
- [ ] Keep runtime compatibility I/O as an editor adapter target, not core QDR document I/O.

## Phase 2 - Mesh Core Contracts

- [ ] Introduce strong element handle types such as `VertexId`, `FaceId`, and an edge handle/key.
  Keep compatibility adapters for the current `ElementId` API while migrating call sites.
- [ ] Add generation/liveness validation, or an explicit document revision plus liveness table, so
  stale selected/deleted element IDs can be detected.
- [ ] Add `validate_polygon_document(Document)` with structured diagnostics for duplicate IDs,
  missing vertex refs, repeated face vertices, degenerate loops, nonmanifold edges, UV mismatch,
  hard-normal edge validity, unreferenced vertices, and triangulation failure.
- [ ] Require modeling-domain operation tests to call the central validator after every successful
  topology mutation.
- [ ] Split `src/mesh/polygon/document.hpp` into narrower headers for model/storage, selection,
  operations, topology query, mesh compilation, picking, and overlay/preview data.
- [ ] Move operation-specific created/deleted/affected IDs into structured results, and let higher
  layers handle editor selection remapping.

## Phase 3 - One Mutation Path For User-Visible State

- [ ] Add `AssetCommandService` plus undoable asset commands for add/replace/update material,
  texture, and mesh edits. Route `QuaderAssetApi` through it when edits are user-visible.
- [ ] Narrow mutable `ProjectSession` access. Prefer const read views plus command/modeling/asset
  services for mutation.
- [ ] Formalize `ModelingMeshSyncService` as the single allowed modeling-owned scene/asset sync
  owner and guard against direct spread of `SceneDocument` / `AssetRegistry` mutation.
- [ ] Make object/group selection command-owned. Runtime picking can provide hit results, but object
  pick and box selection should apply through `EditorSelectionService` / `SelectEntitiesCommand`.
  Keep component selection runtime-owned.
- [ ] Consolidate scene selection application into one helper/owner used by `SelectEntitiesCommand`,
  `EditorSelectionService`, and `SelectionProjectionService`, with explicit undoable vs runtime-sync
  modes.
- [ ] Revisit `ProjectObjectRecord` duplication. Either remove duplicated live fields or add strict
  invariant tests proving scene components stay canonical.
- [ ] Move default asset bootstrap from `App` into a project/asset bootstrap service.

## Phase 4 - Modeling Tools And Runtime

- [ ] Extract focused owners out of `ModelingSessionService`, starting with selection application,
  transform drag/preview, modal preview state, runtime tool command construction, and overlay
  projection.
- [ ] Keep `ModelingSessionService` as a compatibility facade while call sites migrate to the focused
  owners.
- [ ] Define one owner for modal preview state. Remove duplicate `ModelingBoxPreview` ownership from
  viewport modal state, or make it a read-only projection of runtime state.
- [ ] Add a typed `RuntimeToolCommandService` or command factory for Insert Edge Loop, Cut, Mirror,
  Poly, and Box flows. The broad session facade should not manually assemble command settings.
- [ ] Replace `ViewportToolContext::modeling` with narrow ports for hover, preview, commit, cancel,
  selection-mode sync, and tool-command result handling.
- [ ] Normalize transform tools: either make Move/Rotate/Scale real state-machine tools, or document
  them as descriptors while `TransformGizmoInteraction` remains the actual state machine.
- [ ] Add behavior tests proving modal previews never become document truth and viewport object
  selection undo/redo matches public selection API behavior.

## Phase 5 - Render Snapshots And Backend Adapters

- [ ] Add a first-class `RenderSceneSnapshot` built from scene/assets at revision or frame boundaries.
- [ ] Move mesh/light extraction from pointer-based `SceneRenderExtractionPlan` toward value/span
  snapshot records with explicit dirty/revision fields.
- [ ] Update `SceneRenderSyncService`, renderable caches, light caches, and preview sync to consume
  snapshot records instead of `SceneDocument` / `AssetRegistry`.
- [ ] Add overlay snapshot records for selected-object wires, scene wireframe, hierarchy selection
  wires, group bounds, selected light markers, and tool overlays.
- [ ] Remove scene/asset overlay extraction from `ViewportPanel`; UI should submit viewport/tool
  presentation data, not derive render truth.
- [ ] Move UI icon texture creation/destruction into a render-owned texture service.
- [ ] Narrow `RenderSystem` so raw Filament access stays inside render adapters/factories.
- [ ] Add guardrails preventing UI from allocating/destroying Filament resources and preventing
  render code from calling document/asset mutators.

## Phase 6 - UI, App, And Platform Boundaries

- [ ] Route `ToolSettingsPanel` setting writes through `context.editor.tools().set_setting(...)` or a
  typed public batch/update API.
- [ ] Move `editor.ui().set_runtime_hooks(...)` and `editor.preferences().set_runtime_hooks(...)`
  out of `UISystem::draw` into app startup/composition.
- [ ] Split `UIContext` into narrower contexts: public API for intent, read-only scene/query data for
  presentation, and viewport/render-specific context only for viewport surfaces.
- [ ] Add guardrails forbidding UI direct calls to mutating owner APIs when a public API route exists.
- [ ] Decide whether diagnostic globals such as `Logger` and fatal-error handling are explicit allowed
  exceptions or should move behind injected app-owned diagnostics services.
- [ ] Keep SDL/platform types inside platform/app adapters where practical as runtime targets split.

## Phase 7 - I/O, Serialization, And Background Work

- [ ] Add `QdrDiagnostic` / `QdrError` with code, severity, JSON pointer/path, and optional
  line/column/source path. Preserve `message` for API/UI compatibility.
- [ ] Make current-schema project codec parsing strict for enum fields. Use migration/default helpers
  only for legacy compatibility.
- [ ] Sort `MaterialComponent::material_slots` before QDR serialization and add deterministic project
  serialization coverage.
- [ ] Add a current project-document golden fixture and idempotent rewrite test.
- [ ] Make project atomic-write validation policy explicit. Either add debug/test/save-as validation
  or document the hot-save skip with guardrail coverage requiring round-trip tests.
- [ ] Introduce `QdrProjectWriteSnapshot` or an equivalent immutable export DTO before real background
  project export work. Worker threads should consume snapshots, not live `ProjectDocument` or
  `AssetRegistry` pointers.

## Phase 8 - Public API And Documentation Polish

- [ ] Pick one typed command spelling. Either restore/document `QuaderCommand::...` or migrate docs
  and guardrails fully to `quader_command::k...`.
- [ ] Remove or clearly deprecate duplicate command constants that alias canonical IDs.
- [ ] Update `docs/api-overview.md` and examples to match current C++ names and result helpers.
- [ ] Add richer public-header contract comments for `QuaderEditorApi`, `QuaderApi`,
  `QuaderCommandsApi`, `QuaderSceneBatch`, `QuaderScript`, and result types.
- [ ] Keep public docs/examples starting from public APIs only; keep internal owner discussion in
  clearly labeled internal architecture docs.

## Suggested Execution Order

1. Land Phase 0 guardrails and docs first.
2. Fix small concrete bypasses and stale guardrails: tool settings API route, command spelling drift,
   QDR material-slot determinism, and docs manifest coverage.
3. Split CMake targets enough to make dependency direction visible.
4. Add mesh validation and require operation tests to use it.
5. Extract `ModelingSessionService` owners in narrow vertical slices, each with behavior tests.
6. Introduce render and QDR snapshot DTOs once owner boundaries are stable.

## Verification Map

- Source-policy and architecture guardrails: `quader_guardrail_tests`.
- Public API, command, selection, history, scene, viewport, and render behavior: `quader_tests`.
- Polygon and runtime modeling operations: `quader_modeling_domain_tests`.
- Final handoff after project file changes: `cmake --build --preset vs2026-debug --target quader`.
