# Exhaustive DRY/OOP Architecture Refactor - 2026-06-26 Current Run

## Completion Rule

This file is the source of truth for the current run. The goal can be marked complete only when every
actionable item below is checked, targeted tests pass, full configure/build/ctest pass when
practical, and an app startup smoke has no error/critical log entries. If any actionable item remains
unchecked because of tool limits, environment limits, broken external dependencies, impossible
conflicting worktree changes, or GUI/GPU limits, the goal must be blocked.

## Big-Issue Follow-Up Audit - 2026-06-26

Six scoped follow-up agents plus main-agent source checks found the previous checklist closed many
owners, but did not prove the app is OOP-complete. The actionable big findings below supersede any
earlier "no actionable current-run checklist items remain" statement for this continuation. Completion
now requires every item in this follow-up checklist to be fixed and verified, or reclassified with
concrete code evidence.

### Follow-Up Dependency Order

1. Guardrail/catalog-source fixes that prevent tests from mirroring production ownership.
2. Resource-lifetime and document/modeling mutation guards that protect owner invariants.
3. Modeling execution/projection semantic consolidation.
4. Public API and scripting transaction boundary work.
5. Input/UI/localization metadata ownership fixes.
6. Targeted tests, full configure/build/ctest, and app smoke where practical.

### Follow-Up Required Findings And Checklist

- [x] Stop modeling-domain tool/operation contract tests from copying production catalogs.
  - Files/functions: `tests/modeling_domain/tool/qdr_modeling_runtime_tool_operation_contract_tests.cpp`,
    `tests/build_guardrail_tests.cpp`.
  - Risk: test-side `ExpectedTool`/`ExpectedOperation` arrays remain a second source of truth and
    can be updated with production, hiding catalog drift.
  - Owner/source of truth: production editor tool/operation registries and descriptor spans.
  - Checklist: iterate owner registries instead of local expected arrays; widen the guardrail to
    forbid copied catalog structs in modeling-domain catalog/contract tests.

- [x] Harden Rml UI Filament draw-resource ownership.
  - Files/functions: `RmlFilamentRenderer::DrawResource`, `RmlFilamentRenderer::RenderGeometry`,
    `RmlFilamentRenderer::destroyDrawResource`.
  - Risk: move assignment can overwrite live raw Filament handles, and ownership transfer after
    scene insertion has weaker cleanup than mesh/overlay renderable creation guards.
  - Owner/source of truth: `RmlFilamentRenderer` RAII/creation guard matching the render proxy
    lifecycle pattern.
  - Checklist: delete unsafe move assignment or make ownership destruction explicit; add a creation
    guard for partially built draw resources; add focused guardrail coverage.

- [x] Reject or delegate plain scene mutations for modeling-owned entities.
  - Files/functions: `QuaderObjectHandle::{destroy,duplicate}`, `QuaderSceneApi`,
    `DocumentCommandService::{destroyEntity,duplicateEntity,setMeshComponent}`.
  - Risk: scene commands can remove, duplicate, or remesh the scene projection of a modeling object
    without updating the modeling runtime source of truth.
  - Owner/source of truth: modeling-aware commands for modeling objects; `DocumentCommandService`
    for plain scene entities only.
  - Checklist: guard modeling-owned entities in document commands or route to modeling commands;
    add public/synthetic behavior tests for destroy, duplicate, and mesh replacement attempts.

- [x] Unify `duplicate_meshes` context-action and instant-command execution semantics.
  - Files/functions: `ModelingActionCatalog`, `ModelingInstantOperationCatalog`,
    `ModelingSessionService::executeContextAction`, `ModelingCommandExecutionService`.
  - Risk: one public execution path preserves scene identity for duplicates while another raw
    runtime path can lose parent, transform, visibility, lock, material, or mesh projection state.
  - Owner/source of truth: instant-operation/command catalog execution path for catalog-owned
    duplicate/delete operations.
  - Checklist: route catalog-owned duplicate context action through the same safe owner used by
    instant commands; add regression coverage for scene identity preservation on context action.

- [x] Centralize world-space selection-center projection policy.
  - Files/functions: `ModelingSessionService::selectionCenter`, `SelectionProjectionService` or a
    narrow projection helper, `SelectionOverlayProjection`, runtime `SelectionCenterPlan`.
  - Risk: transform gizmo pivot, overlay visibility, runtime selection center, and public query
    behavior can drift because selection-center policy is split across runtime, service, and
    projection code.
  - Owner/source of truth: one projection owner using `ModelingObjectRuntimeMap`, `SceneDocument`,
    and runtime normalized selection data.
  - Checklist: move world-space component/object center computation into one owner; remove duplicated
    transform-point helper; cover invisible/object/component selection-center behavior.

- [x] Remove internal owner/domain types from public API headers.
  - Files/functions: `src/api/*.h`, especially `QuaderModelingApi.h`, `QuaderSelectionApi.h`,
    `QuaderSceneApi.h`, `QuaderAssetApi.h`, `QuaderQueryApi.h`, `QuaderRenderApi.h`,
    `QuaderObjectHandle.h`, `QuaderScript.h`.
  - Risk: public API consumers compile against internal modeling, scene, asset, render, and session
    owners, making the API boundary unstable and forcing future scripting bindings through internals.
  - Owner/source of truth: API-owned public DTOs in `src/api`, with internal conversions in `.cpp`
    files or internal conversion helpers.
  - Checklist: replace exposed `Modeling*`, `MeshAsset`, `MaterialDocument`, `TextureAsset`,
    `TransformComponent`, `ViewportOverlayFrame`, and public `ProjectSession` bootstrap signatures
    with `Quader*` value contracts or opaque handles; tighten public-header guardrails.
  - Verification: `cmake --build --preset vs2026-debug --target quader_tests`;
    `build\vs2026\Debug\quader_tests.exe --gtest_filter=ProgrammaticApiGuardrailTests.*:EditorApiTests.*:ViewportToolCatalogTests.*:ViewportToolRuntimeTests.*:ViewportInteractionControllerTests.*:GroupingPublicApiTests.*:ProgrammaticBehaviorScenarioTests.*`.

- [x] Make `QuaderScript::run()` transaction-aware.
  - Files/functions: `QuaderScript`, `QuaderApiMutationRunner`, `EditorHistoryService`.
  - Risk: multi-step public scripts can leave partial state on failure and are not guaranteed to undo
    as one labeled user operation.
  - Owner/source of truth: editor history transaction owner, exposed through a narrow script
    execution path.
  - Checklist: define and test whole-script undo and failed-script rollback semantics; route script
    mutations through one transaction-aware runner.
  - Verification: `cmake --build --preset vs2026-debug --target quader_tests`;
    `build\vs2026\Debug\quader_tests.exe --gtest_filter=ProgrammaticApiTests.*Script*:EditorHistoryServiceTests.TransactionsRecordAlreadyExecutedCommandsAsOneUndoItem`.

- [x] Route modal tool finalize/cycle gestures through keybinding ownership.
  - Files/functions: `ViewportHost`, `QuaderCommandsApi`, `QuaderKeybindingService`,
    `host_contract.cpp`, modal tool event adapters.
  - Risk: `Space`/`Enter` finalize and `F` cycle mode bypass user rebinding and host metadata.
  - Owner/source of truth: keybinding targets and semantic dispatch actions for modal tool gestures.
  - Checklist: add modal finalize/cycle targets and defaults; resolve semantic actions per frame;
    test rebinding changes runtime modal behavior.
  - Verification: `cmake --build --preset vs2026-debug --target quader_tests`; 
    `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.ViewportModalToolGesturesUseKeybindingTargets:EditorApiTests.ViewportNavigationActionCatalogDrivesDefaultsAndResolution:EditorApiTests.ViewportKeybindingContextsSeparateNavigationFromToolShortcuts:UISystemTests.RmlViewportShellCapturesFlyNavigationInput`.

- [x] Use host UI placement metadata for modeling shelf order/placement.
  - Files/functions: `ModelingModeShelfController`, `host_contract.cpp`, shelf tests.
  - Risk: `ui_placement_catalog()` owns the `operations_top_bar` placement metadata, but the runtime
    shelf currently builds directly from operation panels and can drift from host metadata.
  - Owner/source of truth: `ui_placement_catalog()` for shelf placement/order, with
    `operation_panel_catalog()` owning panel contents.
  - Checklist: resolve `operations_top_bar` placements, map target panels through operation-panel
    descriptors, and update ordering guardrails.
  - Verification: `cmake --build --preset vs2026-debug --target quader_tests`;
    `build\vs2026\Debug\quader_tests.exe --gtest_filter=ModelingModeShelfControllerTests.*:ModelingModeShelfComponentTests.*:EditorApiTests.ModelingActionSetExposesActiveOperationPanelForSelectionModeShelf:ProgrammaticApiGuardrailTests.ApiDoesNotTreatHostContractDescriptorsAsCommands`.

- [x] Localize modeling action disabled reasons.
  - Files/functions: `ModelingActionCatalog`, `ModelingModeShelfController`,
    `ModelingModeShelfComponent`, locale resources, localization tests.
  - Risk: user-facing disabled reason text is hard-coded English in modeling services and bypasses
    locale resource contracts.
  - Owner/source of truth: structured disabled-reason codes or a narrow localized presenter backed by
    locale files.
  - Checklist: replace raw English disabled reasons with locale-owned text; add guardrails for
    locale keys and no raw UI-facing disabled strings in modeling catalog.
  - Verification: `cmake --build --preset vs2026-debug --target quader_tests`;
    `build\vs2026\Debug\quader_tests.exe --gtest_filter=LocalizationResourceTests.*:ModelingModeShelfControllerTests.*:ModelingSessionServiceTests.ContextActionsExpose*:ModelingSessionServiceSplitTests.ContextActionServiceOwnsQueryResolutionAndModalSuppression:EditorApiTests.ModelingActionSetExposesActiveOperationPanelForSelectionModeShelf`.

## Audit Coverage

- API/commands/scripting/examples: completed by subagent Maxwell and main-agent scans.
- Modeling/mesh/projection/runtime: completed by subagent Lorentz and main-agent scans.
- Viewport/input/tools/keybindings: completed by subagent Dalton and main-agent scans.
- UI/layout/localization/resources: completed by subagent Parfit and main-agent scans.
- Render/assets/scene/project/QDR: completed by subagent Bernoulli and main-agent scans.
- Tests/docs/build guardrails: completed by subagent Plato and main-agent scans.

Prior completed items were used as context only. Current code evidence shows the prior scene API edit
contract, history API routing, runtime-state splits, render proxy move-only work, UI token/RML
escaping, CMake option owner, and example compilation fixes remain present. The items below are the
current-run actionable findings.

## Dependency Order

1. CMake/test/source-scan and command/keybinding catalog foundations used by later guardrails.
2. Public command/API surface cleanup and project command routing.
3. Keybinding service semantics and viewport/input defaults.
4. Modeling action availability, command tool descriptor ownership, and runtime map/projection
   boundaries.
5. QDR/scene/asset/render ownership fixes.
6. UI MainLayout/modeling shelf/localization ownership fixes.
7. Guardrail conversions, focused tests, full verification, and app smoke.

## Required Findings And Checklist

### Build, Tests, Docs, And Guardrail Infrastructure

- [x] Consolidate CMake dependency source discovery for OpenMesh/Catch2.
  - Files/functions: `CMakeLists.txt`, `cmake/QuaderModeling.cmake`,
    `cmake/QuaderBuildOptions.cmake` or a new narrow CMake helper.
  - Risk: cache defaults and actual dependency resolution can drift.
  - Owner/source of truth: one CMake dependency-source resolver.
  - Checklist: extract env/cache/local/dev path resolution; reuse it for cache defaults and modeling
    setup; keep fetch/license behavior in dependency-specific functions; add a guardrail forbidding
    duplicate `_deps/openmesh`, `_deps/catch2`, and `thirdparty/<dep>` fallback blocks.

- [x] Tighten boot smoke log verdict to fail on normal app errors.
  - Files/functions: `tools/boot_smoke.ps1`, `tests/build_guardrail_tests.cpp`,
    `src/core/Logger.cpp`, `src/core/Logger.h`.
  - Risk: smoke can pass while startup logs contain `[error]` entries.
  - Owner/source of truth: `tools/boot_smoke.ps1` aligned with logger severity formatting.
  - Checklist: fail on case-insensitive `[error]` and `[critical]` plus fatal text; require copied log
    existence unless explicitly disabled; add guardrail coverage for both severities.

- [x] Add exhaustive root `quader_tests` source registration guardrail.
  - Files/functions: `CMakeLists.txt`, `tests/build_guardrail_tests.cpp`,
    `tests/programmatic_api_guardrail_tests.cpp`.
  - Risk: new root tests under `tests/` can be omitted from `quader_tests` while `ctest` still passes.
  - Owner/source of truth: root test CMake registration plus one build guardrail scanner.
  - Checklist: scan `tests/*.cpp` and `tests/support/*.cpp`, excluding `tests/modeling_domain/**`;
    compare against `CMakeLists.txt`; fold the narrow programmatic-only support check into the
    generic guardrail.

- [x] Stop catalog guardrail tests from copying production descriptor catalogs.
  - Files/functions: `tests/modeling_tool_catalog_tests.cpp`, `tests/viewport_tool_catalog_tests.cpp`,
    `ModelingInstantOperationCatalog.cpp`, `host_contract.cpp`, `ViewportToolDescriptorCatalog.cpp`.
  - Risk: tests become a second source of truth and can mirror drift instead of catching it.
  - Owner/source of truth: production descriptor spans and host action catalog.
  - Checklist: iterate owner catalogs in tests; assert every owner descriptor resolves to matching
    commands/tools; assert registered tools/commands have owner descriptors where applicable; add a
    guardrail forbidding local `ExpectedAction`/`ExpectedTool` catalog arrays.

- [x] Derive typed command coverage tests from one typed-ID owner instead of copied lists.
  - Files/functions: `QuaderCommandIds.h`, `editor_api_tests.cpp`, command catalog tests.
  - Risk: public typed IDs and tests can drift together.
  - Owner/source of truth: exported typed command ID table/span or equivalent single owner.
  - Checklist: add a single iterable typed command ID owner; update tests to derive coverage from it
    plus descriptor-driven modeling/tool catalogs; remove duplicated neutral command lists.

### API, Commands, Public Contracts, And Scripting

- [x] Route host-declared project document commands through `QuaderCommandsApi`.
  - Files/functions: `host_contract.cpp`, `QuaderProjectApi.*`, `QuaderCommandsApi.*`,
    `QuaderCommandIds.h`.
  - Risk: metadata advertises New/Open/Save/Save As and shortcuts, but commands/keybindings cannot
    execute them.
  - Owner/source of truth: `QuaderCommandsApi` command catalog delegating to `QuaderProjectApi`.
  - Checklist: add typed `project.*` command IDs; register project commands; map host editor targets;
    remove silent host shortcut skips or replace with explicit allow-list; test shortcuts and command
    execution status parity with `editor.project()`.

- [x] Collapse duplicate default shortcut ownership into one keybinding projection.
  - Files/functions: `QuaderCommandsApi.cpp`, `QuaderKeybindingService.h`, `host_contract.cpp`,
    `HostViewportContractAdapter.h`, `ViewportToolDescriptorCatalog.cpp`.
  - Risk: duplicate effective defaults and stale active shortcuts when one catalog changes.
  - Owner/source of truth: `QuaderCommandsApi` projection fed by host shortcut/action catalogs and
    tool descriptors.
  - Checklist: stop double-registering defaults for targets imported from host catalog or dedupe by
    `(targetId, shortcut, when)`; ensure tool shortcuts appear exactly once; validate host tool
    shortcut entries against `ToolDescriptor.shortcut`; make `setShortcut()` choose canonical default
    context.

- [x] Make shortcut mutation APIs report real no-op `changed` status.
  - Files/functions: `QuaderCommandsApi::setShortcut/removeShortcut/resetShortcut`,
    `QuaderPreferencesApi.*`, `QuaderKeybindingService.h`.
  - Risk: preferences/UI can mark dirty and rewrite files when nothing changed.
  - Owner/source of truth: `QuaderKeybindingService` mutation result.
  - Checklist: return `{ok, changed, message}` from keybinding mutations; update API result status;
    test same shortcut twice, removing already removed shortcuts, reset without override, and invalid
    shortcuts.

- [x] Hide viewport/editor input internals from public command/keybinding headers.
  - Files/functions: `QuaderCommandsApi.h`, `QuaderToolsApi.h`, `QuaderKeybindingTypes.h`,
    `ViewportInputTypes.h`, `programmatic_api_guardrail_tests.cpp`.
  - Risk: public API consumers compile against viewport/router/internal input contracts.
  - Owner/source of truth: API-owned public value types plus internal viewport adapters.
  - Checklist: introduce API value enums/structs where needed; convert internally; forbid
    `viewport/` and `editor/input` includes in public command/keybinding headers.

- [x] Remove internal `ProjectSession` bootstrap from public docs/examples.
  - Files/functions: `QuaderEditorApi.h`, `docs/api-overview.md`, `examples/create_box.cpp`,
    `examples/script_modeling_workflow.cpp`, public docs/examples guardrails.
  - Risk: onboarding docs/examples teach users to instantiate a document owner directly.
  - Owner/source of truth: public API bootstrap wrapper/factory owning `ProjectSession`.
  - Checklist: add a small public bootstrap/factory; update public docs/examples to start from public
    API/bootstrap only; guard public docs/examples against `scene/document/ProjectSession.h`.

- [x] Fix `QuaderScript::run()` aggregate mutation status ownership.
  - Files/functions: `QuaderScript.h`, `QuaderScript.cpp`, script/API tests.
  - Risk: scripts can report `changed=true` with `mutationKind=None` after later no-op/failure steps.
  - Owner/source of truth: a script status aggregation helper.
  - Checklist: aggregate `changed`, mutation kind, revisions, and errors consistently; test changed
    step followed by no-op and changed step followed by missing alias.

- [x] Move standalone mesh replacement invariants out of public API code.
  - Files/functions: `QuaderAssetApi.cpp::replaceStandaloneMesh`, `AssetRegistry::replaceMesh`,
    a project/session asset mutation service.
  - Risk: non-API import/command paths can bypass the standalone-only replacement rule.
  - Owner/source of truth: project/session asset mutation owner that sees assets, scene, and modeling
    ownership.
  - Checklist: add a narrow service/helper for standalone mesh replacement; API delegates; test
    modeling-owned mesh rejection and true standalone mesh replacement at the owner.

### Viewport, Input, And Keybindings

- [x] Use semantic context overlap, not literal `when` string equality, for keybinding conflicts.
  - Files/functions: `QuaderKeybindingService.h`, keybinding tests.
  - Risk: rebinding UI can miss conflicts when contexts differ textually but overlap semantically.
  - Owner/source of truth: `QuaderKeybindingService` and its `WhenParser`.
  - Checklist: add conservative overlap/satisfiability helper; encode known implications such as
    `navigation.flyActive => navigation.active`; use overlap in conflict lookup; test broad viewport
    conflicts and mutually exclusive navigation/tool contexts.

- [x] Prevent persisted keybinding JSON from creating unknown command targets.
  - Files/functions: `QuaderKeybindingService.h`, keybinding persistence tests.
  - Risk: typoed user JSON silently creates ghost shortcuts.
  - Owner/source of truth: registered target catalog owned by `QuaderCommandsApi` and
    `QuaderKeybindingService`.
  - Checklist: reject or explicitly warn on unknown loaded targets; do not call `registerTarget()`
    from JSON load; cover unknown normal and removal entries.

- [x] Unify fly-navigation speed defaults.
  - Files/functions: `tool_constants.h`, `host_contract.cpp`, `HostViewportContractAdapter.h`,
    `ViewportCameraController.cpp`, viewport tests.
  - Risk: host metadata and actual viewport movement/reset speed diverge.
  - Owner/source of truth: `viewportHostDefaults()` carrying runtime fly speed values.
  - Checklist: add fly base/fast/slow values to `ViewportHostDefaults`; initialize/reset camera speed
    from them; align tests and metadata guardrails.

- [x] Thread UI text focus into viewport keybinding context.
  - Files/functions: `RmlUiContextHost`, `UISystem`, `ViewportHost`, `ViewportInputRouter`,
    keybinding/context tests.
  - Risk: future text fields can still fire viewport shortcuts because frame context hard-codes
    `textInputActive=false`.
  - Owner/source of truth: Rml/UI focus owner passed into viewport frame routing.
  - Checklist: expose text-input/editor-text focus state from RML/UI host; pass it into main and
    windowed viewport frames; reset pending chords when text focus blocks dispatch; test focused and
    unfocused cases.

### Modeling, Selection, Projection, And Runtime Ownership

- [x] Make instant operation enablement host/action-catalog owned.
  - Files/functions: `host_contract.cpp`, `ModelingActionCatalog.*`,
    `ModelingInstantOperationCatalog.*`, `ModelingSessionService::executeInstantOperation`.
  - Risk: command palette/instant ops can enable operations that context actions disable.
  - Owner/source of truth: host-backed `ModelingActionCatalog` availability.
  - Checklist: apply full host descriptor min-count/active/single-object rules; route
    `modelingInstantOperationEnabled()` through the owner; reject disabled direct execution; test
    one-too-few selections and direct execution failures.

- [x] Restore scene-aware delete modeling entities through the runtime map owner.
  - Files/functions: `SceneAwareDeleteSelectionCommand.cpp`, `ModelingObjectRuntimeMap.*`,
    `SceneAwareDeleteRuntime` callbacks.
  - Risk: undo/redo can attach snapshots to a stale/duplicate scene component instead of the mapped
    entity.
  - Owner/source of truth: `ModelingObjectRuntimeMap`.
  - Checklist: add narrow lookup/restore callback; remove whole-scene scan; test stale duplicate
    `ModelingObjectComponent` does not steal restore identity.

- [x] Centralize selection projection active fallback policy.
  - Files/functions: `SelectionProjectionService.cpp`, `session_operations.cpp`,
    modeling selection projection tests.
  - Risk: round-trips can flip active entity/object and change pivot/inspector/command behavior.
  - Owner/source of truth: `SelectionProjectionService`.
  - Checklist: add `resolveProjectedActiveSelection` or equivalent; use in both directions; align
    with runtime last-selected fallback unless explicitly documented; add round-trip tests.

- [x] Derive modeling command tool catalog entries from host action metadata.
  - Files/functions: `ModelingCommandToolCatalog.cpp`, `host_contract.cpp`,
    `tests/modeling_tool_catalog_tests.cpp`.
  - Risk: panels/context metadata and command palette/tool registration can diverge.
  - Owner/source of truth: host action catalog/operation panel descriptors, with explicit overrides
    only for behavior not represented there.
  - Checklist: generate context/preview command tools from `action_catalog()`; preserve aliases such
    as apply active material; remove manual expected action array; add exhaustive host-action parity
    guardrail.

- [x] Collapse operation settings factory mapping onto operation command schema.
  - Files/functions: `operation_command.cpp`, `ModelingOperationSettings.cpp`,
    operation command/modeling-domain tests.
  - Risk: a new operation can have valid command schema but mismatched default/current settings.
  - Owner/source of truth: `EditorOperationCommandSchema`.
  - Checklist: add schema-driven settings factory or validation; reduce duplicated switches; test
    every `operation_registry().entries()` item against production `settingsForOperation()`.

- [x] Remove mutable map accessors from `ModelingObjectRuntimeMap`.
  - Files/functions: `ModelingObjectRuntimeMap.h/.cpp`, current read callers, OOP guardrails.
  - Risk: callers can bypass bidirectional cleanup/tombstone invariants.
  - Owner/source of truth: `ModelingObjectRuntimeMap`.
  - Checklist: remove non-const `entitiesByObject()` / `objectsByEntity()`; add narrow iteration or
    lookup APIs; update callers; add source/compile guard that no mutable accessor remains.

- [x] Fix material-slot sync so separate modeling objects keep per-entity materials.
  - Files/functions: `ModelingMeshSyncService.cpp`, `ModelingObjectRuntimeMap`, scene/material-slot
    tests.
  - Risk: unordered-map iteration can collapse two objects using slot `0` onto one material.
  - Owner/source of truth: modeling mesh sync material-slot binding policy.
  - Checklist: define deterministic conflict policy; preserve each entity primary material for
    separate objects; only share slot maps for true multi-slot combined meshes; test two boxes with
    distinct materials through forced sync.

### QDR, Scene, Assets, And Render

- [x] Share validated QDR polygon-document codec between project and legacy paths.
  - Files/functions: `QdrDocumentReader.cpp::polygonDocumentFromJson`, `QdrDocumentWriter.cpp`,
    `qdr_document.cpp`, QDR tests.
  - Risk: malformed project polygon documents can enter truth; authored shading compatibility can
    diverge.
  - Owner/source of truth: shared QDR polygon document parser/serializer/validator.
  - Checklist: extract shared helpers; route project and legacy DTO paths through them; decide and
    test explicit authored-shading compatibility; reject duplicate/missing topology consistently.

- [x] Share QDR atomic-write implementation.
  - Files/functions: `qdr_document.cpp`, `QdrDocumentWriter.cpp`, new narrow QDR atomic writer helper.
  - Risk: temp-file, cleanup, retry, and validation semantics drift between legacy and project
    writers.
  - Owner/source of truth: shared `QdrAtomicFileWriter` or equivalent helper with caller validation.
  - Checklist: extract path/retry/write/replace/cleanup; keep DTO/project validation callbacks; add
    project atomic success/failure tests and guardrail allowing one atomic replace implementation.

- [x] Add scene-owned mutation path for `VisibilityComponent::selectable`.
  - Files/functions: `SceneDocument.*`, `QdrDocumentReader.cpp`, scene/QDR tests.
  - Risk: selectable is persisted scene truth but currently mutated directly by IO with unclear
    revision/history semantics.
  - Owner/source of truth: `SceneDocument` for IO hydration, document command/API only if exposed to
    users later.
  - Checklist: add `SceneDocument::setSelectable`; route QDR through it; test QDR round-trip and
    guard against direct `->selectable =` outside the scene owner.

- [x] Evict obsolete texture revisions in render texture cache.
  - Files/functions: `TextureBindingService.h/.cpp`, render cache tests/guardrails.
  - Risk: repeated texture replacement loads new Filament textures while old revisions stay allocated
    until renderer shutdown.
  - Owner/source of truth: `TextureBindingService` cache lifecycle.
  - Checklist: evict older entries for the same asset/color space when newer revision loads; define
    failed-load behavior; add a GPU-free cache-policy test or source guardrail.

### UI, Layout, Localization, And Component Contracts

- [x] Derive modeling shelf tabs from operation panel metadata.
  - Files/functions: `ModelingModeShelfController.cpp`, `ModelingActionCatalog.cpp`,
    `host_contract.cpp`, UI/modeling shelf tests.
  - Risk: shelf tab identity/order can drift from operation panel metadata; current order already
    differs from metadata.
  - Owner/source of truth: `operation_panel_catalog()` / `ModelingActionSet.panels`.
  - Checklist: build shelf controls from operation panels with hidden panels included as needed; keep
    icon/short-label adapter narrow; remove private hard-coded descriptor array; test ordering and
    group/unknown fallback behavior.

- [x] Remove stale modeling-shelf command locale keys.
  - Files/functions: `resources/locales/en-US.lang`, `resources/locales/it-IT.lang`,
    `ModelingModeShelfController.cpp`, localization tests.
  - Risk: translators update obsolete keys that no UI reads.
  - Owner/source of truth: `modeling.action.<actionId>.label` from action catalog; shelf-specific
    text stays under `ui.modeling_shelf.*`.
  - Checklist: remove or explicitly deprecate unused `ui.modeling_shelf.command.*` keys; require
    `modeling.action.<id>.label` for every action in every locale; guard stale shelf command keys
    except documented shelf-only messages.

- [x] Consolidate viewport minimum layout size constant.
  - Files/functions: `MainLayoutModel.h`, `MainLayoutSashRegistry.cpp`, MainLayout tests.
  - Risk: public model constant says 160dp while measured sash behavior clamps to 120dp.
  - Owner/source of truth: `MainLayoutModel` exported layout constants.
  - Checklist: choose intended value; replace private sash constant with model-owned width/height
    neutral constant or delete unused model constant; test side/bottom sash `oppositeMinSize` and
    guard against duplicate `kMainLayoutViewportMin*` constants.

## Main-Agent Independent Scan Notes

- Confirmed prior completed fixes remain present: public scene mutations route through
  `DocumentCommandService`, API result status copying is guarded outside `QuaderApiMutationRunner`,
  SDL key/mouse conversion tables live in platform adapters, mesh primitive spans are normalized by
  `MeshAsset`, render proxy resources are move-only, and docs/examples are currently classified.
- No additional main-agent-only actionable finding survived evidence checks beyond the consolidated
  items above.

## Verification Log

- Completed targeted regression runs for the failed verification items:
  viewport fly speed/reset, semantic keybinding overlap, default keybinding dedupe, API status
  guardrails, public behavior guardrails, QDR owner guardrails, runtime-map mutable accessor
  guardrail, and host operation command-tool parity.
- `cmake --preset vs2026`: passed.
- `cmake --build --preset vs2026-debug`: passed after all edits.
- First full `ctest --preset vs2026-debug` exposed two stale guardrails:
  `UISystemTests.RmlShellOwnsMainLayoutAndMeasuredViewportPanel` expected the old literal shelf
  selection call, and `OopArchitectureGuardrailTests.TypeDeclarationsHaveMeaningfulDocumentation`
  required comments on the render RAII creation guards.
- Fixed those verification findings, then
  `quader_tests.exe --gtest_filter=UISystemTests.RmlShellOwnsMainLayoutAndMeasuredViewportPanel:OopArchitectureGuardrailTests.TypeDeclarationsHaveMeaningfulDocumentation`
  passed.
- Final `ctest --preset vs2026-debug`: passed, 2/2 tests.
- Final `tools/boot_smoke.ps1 -Exe build\vs2026\Debug\quader.exe -ScreenshotPath build\vs2026\Debug\verification\quader_boot_smoke.png -LogCopyPath build\vs2026\Debug\verification\quader_boot_smoke.log -TimeoutSeconds 30`: passed with no error, critical, or fatal log verdict.
- No actionable current-run checklist items remain unchecked.
