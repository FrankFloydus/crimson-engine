# Daily DRY/OOP Drift Audit - 2026-06-30

This closed historical ledger for the 2026-06-30 daily audit is not current implementation
authority. Architecture direction lives in `architecture-review.md`; executable refactoring task
IDs and progress live only in `REFACTORING_PLAN.md`. The historical run was complete only when each
item was checked here with its fix and verification recorded, or moved to the reclassified section
with concrete code evidence.

## Run Status

- Status: complete.
- Automation: `daily-dry-oop-drift-audit`.
- Audit sources: six scoped subagents plus main repo-wide inspection.
- Global verification:
  - `cmake --preset vs2026`: passed.
  - `cmake --build --preset vs2026-debug`: passed.
  - `ctest --preset vs2026-debug`: passed, 3/3 test targets.
- GUI smoke: passed with `tools/boot_smoke.ps1` against
  `build/vs2026/Debug/quader.exe`; screenshot and log were written under
  `build/vs2026/Debug/boot-smoke/`.

## Dependency Order

1. Public API and command source-of-truth fixes.
2. Selection/modeling ownership fixes that affect editor intent routing.
3. Viewport input/tool/runtime ownership fixes.
4. UI resource/layout cleanup that removes duplicate policy.
5. Scene/render/assets/QDR source-of-truth fixes.
6. Docs/build/dependency guardrails and affected-check contracts.
7. Targeted tests after each phase, then full configure/build/ctest and smoke
   when practical.

## Actionable Findings And Checklist

### API, Commands, And Docs

- [x] A01 - `docs/api-overview.md` publishes a broken chained example.
  - Evidence: the chained snippet uses `editor.object(...).rename(...).select()`,
    but `QuaderObjectHandle::rename` returns `QuaderApiVoidResult`.
  - Risk: public documentation teaches a compile-time-invalid workflow.
  - Owner/source of truth: `QuaderObjectHandle` result contracts and public docs.
  - Fix checklist: update the snippet to preserve behavior without pretending the
    void result is chainable; add a guardrail that rejects this stale pattern.

- [x] A02 - `QuaderObjectHandle` bypasses the query API for object validity and
  compiled meshes.
  - Evidence: `src/api/quader_object_handle.cpp` reaches through
    `QuaderApiSessionInternalAccess`, `ProjectSession`, and
    `ModelingSessionService` even though `QuaderQueryApi` exposes
    `modelingObjectForEntity` and `compiledObjectMesh`.
  - Risk: handle helpers can drift from public query behavior and duplicate
    validation/conversion policy.
  - Owner/source of truth: `QuaderQueryApi`.
  - Fix checklist: route `valid()` and `compiledMesh()` through `api_->query()`;
    add a guardrail that the handle implementation does not include the session
    internal access header.

- [x] A03 - `QuaderSceneBatch` is copyable despite single-commit semantics.
  - Evidence: `src/api/quader_scene_api.hpp` stores mutable operations,
    `scene_`, and `committed_` without deleting copy construction/assignment.
  - Risk: accidental copies can commit the same logical batch twice or keep stale
    commit state.
  - Owner/source of truth: `QuaderSceneBatch`.
  - Fix checklist: delete copy operations, define move operations, and add a
    compile-time API test.

- [x] A04 - History snapshot projection is duplicated between history and query
  APIs.
  - Evidence: `QuaderHistoryApi::snapshot()` and `QuaderQueryApi::history()`
    independently copy `canUndo`, `canRedo`, and undo/redo labels.
  - Risk: future history metadata can drift between public surfaces.
  - Owner/source of truth: `QuaderHistoryApi::snapshot()`.
  - Fix checklist: route `QuaderQueryApi::history()` through the history API or a
    shared API helper; add a behavior test that the two snapshots match.

### Modeling And Selection Ownership

- [x] M01 - Scene/document selection route predicates are duplicated.
  - Evidence: Object/Group mode ownership checks appear in
    `editor_selection_service.cpp`, `scene_aware_delete_selection_command.cpp`,
    `quader_modeling_api.cpp`, and `quader_api_mutation_runner.cpp`.
  - Risk: one command path can choose document selection while another chooses
    modeling selection for the same mode.
  - Owner/source of truth: `selection_mode_adapter.hpp`.
  - Fix checklist: add a named predicate for document-owned selection modes and
    route call sites through it; update guardrails.

- [x] M02 - Group selection picking bypasses the cached modeling picking owner.
  - Evidence: `EditorSelectionService::pickMeshEntityByBounds` and
    `boxPickMeshEntitiesByBounds` rebuild bounds/ray/rectangle picking against
    scene meshes instead of using `ModelingSelectionService` cached picking
    projections.
  - Risk: group selection can drift from object selection picking, cache
    invalidation, and projected-hit policy.
  - Owner/source of truth: `ModelingSelectionService` and
    `PickingCacheProjection`.
  - Fix checklist: expose object-picking query methods through
    `ModelingSessionService`; route group selection picking through them; remove
    local scene-mesh picking helpers; add a focused behavior or guardrail test.

- [x] M03 - Interaction tool math helpers are copied outside
  `tool_math_internal.hpp`.
  - Evidence: `extrude_tool.cpp`, and smaller local helpers in knife/snapping
    paths, repeat vector conversion/normalization/grid-size logic.
  - Risk: tool math edge-case handling can drift between modal tools.
  - Owner/source of truth: `src/editor/interactions/tool_math_internal.hpp`.
  - Fix checklist: move or reuse the owner helpers and remove copied local math;
    add a guardrail against reintroducing duplicate helpers in interaction
    tools.

### Viewport, Input, And Tool Runtime

- [x] V01 - Active viewport tool state has two unsynchronized owners.
  - Evidence: `QuaderToolsApi` updates modeling tool state while
    `ViewportInputRouter` keeps its own `activeTool`; `ViewportPanel` only syncs
    on some menu/input paths.
  - Risk: programmatic tool activation and UI activation can diverge.
  - Owner/source of truth: public modeling/tool status, with the router as the
    frame-local input projection.
  - Fix checklist: sync router state from `editor.modeling().toolStatus()` during
    panel draw; reset selection/gizmo transient state when the source tool
    changes; add a guardrail.

- [x] V02 - Viewport commands hardcode the perspective pane instead of the
  focused pane.
  - Evidence: command lambdas in `QuaderCommandsApi` call
    `setView(ViewportPaneId::Perspective, ...)` and related pane operations.
  - Risk: shortcuts in split layouts target a different pane than the focused
    viewport input path.
  - Owner/source of truth: `ViewportWorkspaceService::focusedPane()`, exposed
    through `QuaderViewportApi`.
  - Fix checklist: add a public focused-pane query and route view/focus/frame
    commands through it; update tests.

- [x] V03 - `ViewportSpawnWindow` advertises an F12 shortcut without a runtime
  hook.
  - Evidence: `QuaderCommandsApi` registers `ViewportSpawnWindow` with default
    `F12`, while native runtime does not currently wire a spawn callback.
  - Risk: a default shortcut appears supported but is a no-op or disabled path.
  - Owner/source of truth: command registration plus viewport runtime hooks.
  - Fix checklist: expose spawn-window availability, remove the default shortcut
    until supported, and mark command execution unavailable when no hook exists;
    update tests.

- [x] V04 - Host viewport view catalog uses a different stable id than viewport
  layout.
  - Evidence: `viewportViewStableId(ViewportView::Side)` returns `"right"`,
    while `editor/metadata/host_contract.cpp` publishes `"side"`.
  - Risk: host metadata, scripts, and saved UI expectations can diverge.
  - Owner/source of truth: `viewport_layout`.
  - Fix checklist: route the host catalog id through the viewport owner or align
    the literal; update metadata tests.

- [x] V05 - Outliner hardcodes Ctrl+G/Ctrl+Shift+G outside the keybinding owner.
  - Evidence: `OutlinerPanel::drawContent` polls `ImGuiKey_G` directly even
    though `UISystem::dispatchFocusedPanelCommandShortcut` already dispatches
    command bindings for focused panels.
  - Risk: user rebinding and command availability can drift from panel behavior.
  - Owner/source of truth: command/keybinding dispatch.
  - Fix checklist: remove the raw shortcut branch and rely on focused panel
    command dispatch; add a guardrail.

- [x] V06 - Tool runtime classification repeats descriptor policy with marker
  inheritance checks.
  - Evidence: `ViewportToolRuntime` uses `dynamic_cast` to
    `ModalViewportTool`/`TransformViewportTool` even though `ToolDescriptor`
    already exposes `ToolKind`.
  - Risk: a tool can report one kind in metadata but route through another
    runtime because inheritance and descriptors disagree.
  - Owner/source of truth: `ToolDescriptor::kind`.
  - Fix checklist: route runtime classification through `Tool::kind()` and add a
    guardrail against runtime `dynamic_cast` classification.

- [x] V07 - `ViewportInteractionController::reset()` clears selection state but
  leaves transform gizmo drag state.
  - Evidence: the controller owns both `selection_` and `transformGizmo_`, but
    reset only calls `selection_.reset()`.
  - Risk: switching tools or modes can leave stale gizmo drag/session state.
  - Owner/source of truth: `ViewportInteractionController`.
  - Fix checklist: add a transform-gizmo reset/cancel path and have controller
    reset both owned interactions.

- [x] V08 - `ViewportPanel` wraps workspace/controller accessors without adding
  behavior.
  - Evidence: `activePanePlacements`, `activeSplitHandles`, `paneRectMin`, and
    `paneRectSize` only forward to `ViewportWorkspaceService` or
    `ViewportPaneController`.
  - Risk: a third name for the same viewport geometry state complicates future
    ownership changes.
  - Owner/source of truth: `ViewportWorkspaceService` and
    `ViewportPaneController`.
  - Fix checklist: remove the pass-through wrappers and call owners directly.

### UI, Resources, Render, Scene, And Assets

- [x] R01 - Retired ImGui UI icons no longer remain in the Qt runtime package.
  - Evidence: the stock Fusion shell uses standard Qt widget visuals, while the
    build guardrail requires the old hide/show/lock/unlock/mesh icon assets and
    their former `UIIconSet` owners to be physically absent.
  - Risk: retaining unused theme assets would leave a second visual contract
    beside the uncustomized Fusion style.
  - Owner/source of truth: Qt Widgets and `BuildGuardrailTests`.
  - Fix checklist: keep only renderer-owned runtime icons such as the logo and
    point-light viewport marker.

- [x] R02 - Overlay pixel policy is encoded in producers instead of a named
  policy owner.
  - Evidence: overlay builders and gizmo interactions carry raw values for box
    preview widths, light markers, transform gizmo sizes, and pick radii.
  - Risk: visual policy changes require searching producers and can drift.
  - Owner/source of truth: `viewport_overlay_policy.hpp`.
  - Fix checklist: add named policy constants and route producers through them;
    add a guardrail.

- [x] R03 - Overlay vector geometry helpers are duplicated across overlay
  builders.
  - Evidence: box/marquee/gizmo overlay builders repeat normalize/fallback
    vector helpers.
  - Risk: zero-length and fallback geometry handling can drift between overlay
    producers.
  - Owner/source of truth: a render overlay geometry helper.
  - Fix checklist: add a small shared helper and remove local duplicates; add a
    guardrail.

- [x] R04 - IBL environment catalog is split between scene/API defaults,
  inspector options, and render path inference.
  - Evidence: `"lightroom_14b"` and environment labels/paths appear in scene/API
    type defaults, inspector UI, and `FilamentEnvironmentLibrary`.
  - Risk: UI can offer an environment that the renderer cannot load, or defaults
    can drift from packaged resources.
  - Owner/source of truth: a scene environment catalog.
  - Fix checklist: introduce a shared environment descriptor catalog and use it
    from scene/API defaults, inspector options, and render environment loading;
    add a catalog guardrail.

- [x] R05 - Runtime/default material resource contracts are repeated.
  - Evidence: Filament material filenames and default material texture paths are
    repeated across CMake, runtime loading, app defaults, and host metadata.
  - Risk: packaged resources, runtime loaders, and host metadata can drift.
  - Owner/source of truth: render/asset resource catalogs plus CMake resource
    lists.
  - Fix checklist: centralize C++ runtime/default resource names and reduce CMake
    material target repetition; add guardrails for material/default resources.

- [x] R06 - QDR component field codecs mirror reader/writer logic.
  - Evidence: QDR reader and writer independently enumerate transform, mesh,
    light, material, and scene-setting component fields.
  - Risk: persistence read/write shape can drift when a component adds a field.
  - Owner/source of truth: focused QDR document codec helpers.
  - Fix checklist: move paired component field helpers to
    `qdr_project_document_codecs.*` and update reader/writer call sites; add a
    guardrail.

- [x] R07 - API and scene light defaults disagree.
  - Evidence: `LightComponent{}` defaults to `Directional`, while
    `QuaderLightComponent{}` defaults to `Point`.
  - Risk: default light construction differs depending on whether code starts in
    the scene layer or public API.
  - Owner/source of truth: explicit scene/API light default conversion.
  - Fix checklist: make API-created defaults explicit and document/test the
    intended default behavior without relying on mismatched aggregate defaults.

### Tests, Build, And Guardrails

- [x] T01 - Affected-check routing does not classify `resources/ibl/**`.
  - Evidence: `tools/affected_check.ps1` has resource routes but no IBL route.
  - Risk: environment resource-only changes can skip relevant render/UI tests.
  - Owner/source of truth: affected-check route table.
  - Fix checklist: add an IBL route and tests/guardrail coverage.

- [x] T02 - Docs/examples affected checks and internal-owner guardrails are too
  narrow.
  - Evidence: public docs/examples guardrails focus on existing examples but do
    not cover manifest/internal owner patterns broadly enough.
  - Risk: public examples can grow new internal API dependencies without test
    pressure.
  - Owner/source of truth: docs/examples guardrail tests and affected-check
    routes.
  - Fix checklist: broaden affected-check and guardrails for public docs/examples
    internal owner references.

- [x] T03 - Root test placement guardrail names individual guardrail files
  instead of enforcing the naming contract.
  - Evidence: `tests/build_guardrail_tests.cpp` hardcodes several guardrail test
    paths.
  - Risk: new `*_guardrail_tests.cpp` files can be added to the wrong target.
  - Owner/source of truth: build guardrail tests.
  - Fix checklist: enforce that all root `*_guardrail_tests.cpp` files are in
    `quader_guardrail_tests`.

- [x] T04 - Dependency commit guardrail omits `vcpkg.json`.
  - Evidence: dependency guardrails cover lockfile/third-party paths but not the
    vcpkg manifest.
  - Risk: dependency manifest changes can avoid dependency review checks.
  - Owner/source of truth: dependency/build guardrails.
  - Fix checklist: add `vcpkg.json` to dependency affected-check/guardrail
    coverage.

## Reclassified Findings

- [x] C01 - Selection mode ownership split is not separately actionable in this
  run after M01.
  - Evidence to verify: current public summaries already route through
    `EditorSelectionService`; the actionable drift is the duplicated
    Object/Group document-selection predicate handled by M01.

- [x] C02 - Scene selection sync leaks are not separately actionable in this run.
  - Evidence to verify: the current owner boundary is
    `SelectionProjectionService` and editor selection summaries; remaining direct
    scene selection writes are command/document mutation paths that need
    operation-specific behavior rather than a broad owner migration.

- [x] C03 - Raw ImGui mouse checks for viewport presentation/capture are not
  actionable DRY/OOP drift.
  - Evidence to verify: those checks are frame input capture/presentation
    mechanics, not command/keybinding policy.
