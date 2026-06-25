# Daily DRY/OOP Drift Refactor - 2026-06-25

## Audit Inputs

- Read root `AGENTS.md`, `agents/project-identity.md`, `agents/public-api-and-scripting.md`,
  `agents/domain-ownership.md`, `agents/oop-architecture.md`, `agents/testing-build.md`, and the
  relevant domain files for UI, viewport, rendering, modeling, input, dependencies, docs/examples,
  `src/AGENTS.md`, `src/render/AGENTS.md`, `src/viewport/AGENTS.md`, and `tests/AGENTS.md`.
- Read automation memory from `C:\Users\Drako\.codex\automations\daily-dry-oop-drift-audit\memory.md`.
- Spawned six approved read-only subagents for API/commands, modeling, viewport/input, UI,
  render/overlays, and tests/support.
- Current worktree was already dirty before implementation. Existing UI/platform/test changes and
  the new modeling shelf component are treated as user-owned and are not reverted.

## Required Implementation Plan

### 1. Centralize viewport overlay setting defaults in render policy

Status: completed

Affected files/classes:

- `src/render/ViewportOverlayPolicy.h`
- `src/viewport/ViewportSettings.cpp`
- `tests/core_tests.cpp`

Risk:

- Overlay colors and sizes were hardcoded in both `ViewportOverlayPolicy` and `ViewportSettings`.
  That created two presentation sources of truth for selection, hover, source wire, remove, and
  vertex overlay defaults.

Proposed owner/source of truth:

- `ViewportOverlayPolicy` remains the render-owned source of truth for overlay colors, line widths,
  point sizes, and point growth values.
- `ViewportSettings` exposes user preferences but derives its overlay defaults from the render
  policy.

Implementation checklist:

- [x] Include the render overlay policy from `ViewportSettings.cpp`.
- [x] Replace duplicated ABGR overlay color literals in viewport setting descriptors with
  `viewportOverlayColorFor(...)` or policy constants.
- [x] Replace duplicated line/point size defaults with `viewportOverlayLineWidthPixels(...)`,
  `kOverlayVertexSizePixels`, `kOverlaySelectedVertexGrowthPixels`,
  `kOverlayHoverVertexGrowthPixels`, and `kOverlayVertexOutlineSizePixels`.
- [x] Add a focused guardrail test proving viewport settings defaults stay tied to the render
  policy.

Verification:

- [x] `git diff --check`
- [x] `cmake --build --preset vs2026-debug --target quader_tests`
- [x] `build/vs2026/Debug/quader_tests.exe --gtest_filter=ViewportSettingsTests.*:ViewportOverlayPolicyTests.MatchesNativeColorsAndRuntimePointSizes`
- [x] `cmake --preset vs2026`
- [x] `cmake --build --preset vs2026-debug`
- [x] `ctest --preset vs2026-debug`
- [x] GUI startup smoke where practical

## Completed Implementation Backlog

The findings below were implemented during the continuation goal and verified in the integrated
worktree.

### API and command catalog

Finding: `QuaderCommandsApi.h` still exposes internal command/input owners.

- Affected files/classes: `src/api/QuaderCommandsApi.h`, `src/api/QuaderEditorApi.h`.
- Risk: public API consumers inherit `EditorCommandRegistry` and `QuaderKeybindingService`
  internals.
- Proposed owner/source of truth: `QuaderCommandsApi::Impl` plus public/light keybinding value
  types.
- Checklist: pimpl private registry/keybinding members, extract public value structs, extend public
  header guardrails.
- Status: completed. `QuaderCommandsApi` now keeps registry/keybinding internals behind `Impl`
  and exposes public keybinding value types from `QuaderKeybindingTypes.h`.

Finding: viewport shortcut target mapping has two sources of truth.

- Affected files/classes: `src/api/QuaderCommandsApi.cpp`, `src/viewport/HostViewportContractAdapter.h`,
  `src/viewport/ViewportInputRouter.cpp`, `src/ui/ViewportShortcutController.cpp`.
- Risk: host-contract shortcut targets can drift from command registration and runtime shortcut
  execution.
- Proposed owner/source of truth: a single viewport/input shortcut target catalog consumed by the
  command API and host-contract adapter.
- Checklist: replace duplicate target switches with one entry table, cover host contract and API
  dispatch equivalence, add a guardrail banning duplicate switch tables.
- Status: completed. Shortcut target/default binding policy is centralized through
  `HostViewportContractAdapter` and covered by command/API guardrails.

Finding: modeling action-to-command bridge is repeated in production and tests.

- Affected files/classes: `src/api/QuaderCommandsApi.cpp`, `tests/editor_api_tests.cpp`.
- Risk: fixed command IDs, handlers, aliases, neutral suppression, and tests can diverge when
  modeling tools are renamed or added.
- Proposed owner/source of truth: a narrow command bridge catalog adjacent to `QuaderCommandsApi`
  or editor command tools.
- Checklist: drive aliases, neutral suppression, and registration from one catalog; replace mirrored
  test lists with catalog assertions.
- Status: completed. Modeling action command IDs, aliases, neutral suppression, and tests now use
  the shared bridge catalog in `QuaderCommandsApi`.

### Modeling ownership

Finding: stale picking/overlay helpers remain in `ModelingSessionService`.

- Affected files/classes: `src/editor/modeling/services/ModelingSessionService.cpp`,
  `src/editor/modeling/services/ModelingSessionServices.cpp`,
  `src/editor/projection/PickingCacheProjection.cpp`.
- Risk: dead duplicate hash/bounds/filter helpers can be reused and drift from
  `PickingCacheProjection`.
- Proposed owner/source of truth: `PickingCacheProjection` for pick signatures/bounds and
  `ModelingOverlayService` for overlay frame filtering.
- Checklist: delete unused local helpers and add a source guardrail forbidding them in the facade.
- Status: completed. Stale facade helpers were removed or routed through the picking/overlay
  owners, with source guardrails in `ModelingSessionServiceSplitTests`.

Finding: scene-to-runtime object selection mapping is open-coded.

- Affected files/classes: `src/editor/selection/SelectionProjectionService.cpp`,
  `src/editor/modeling/services/ModelingSessionService.cpp`.
- Risk: active-object fallback, no-op detection, and scene selection restoration can drift.
- Proposed owner/source of truth: `SelectionProjectionService`.
- Checklist: add a span-based object selection projection method, route `setObjectSelection` and
  drawn-plane promotion through it, add selection no-op and activation tests.
- Status: completed. Explicit scene-object selection projection now routes through
  `SelectionProjectionService`.

Finding: duplicate scene identity is copied separately from runtime identity.

- Affected files/classes: `src/editor/modeling/runtime/ModelingObjectRuntimeMap.h`,
  `src/editor/modeling/services/ModelingMeshSyncService.cpp`,
  `src/editor/modeling/services/ModelingSessionService.cpp`.
- Risk: future identity fields can be preserved through one path but lost during duplicate or
  shift-drag.
- Proposed owner/source of truth: `ModelingObjectRuntimeMap` and `ModelingMeshSyncService`.
- Checklist: replace `DuplicateSceneState` with `ModelingObjectSceneIdentity`, expose a single
  apply/preserve helper, add duplicate preservation tests.
- Status: completed. Duplicate scene identity capture/application now lives in
  `ModelingObjectRuntimeMap` and `ModelingMeshSyncService`.

Finding: object transform drag has a parallel scene-preview owner.

- Affected files/classes: `src/editor/transform/SceneTransformDragService.cpp`,
  `src/editor/modeling/services/ModelingSessionService.cpp`.
- Risk: object-mode modeling drags can drift from scene transform preview commit/cancel behavior.
- Proposed owner/source of truth: `SceneTransformDragService` for object-mode scene previews.
- Checklist: route object-mode transform drags through the scene service or extract a shared
  adapter, then cover object/group/shift-duplicate preview behavior.
- Status: completed. Object-mode transform drag previews now use `SceneTransformDragService` for
  preview, cancel, and public API history-backed commit; component drags remain modeling-owned.

### Viewport and UI

Finding: tool activation has split state.

- Affected files/classes: `src/editor/tools/ViewportToolRuntime.cpp`, `src/api/QuaderToolsApi.cpp`,
  `src/ui/ViewportShortcutController.cpp`, `src/ui/UISystem.cpp`.
- Risk: shortcuts, commands, menus, and API activation can show different active tools.
- Proposed owner/source of truth: `QuaderToolsApi` plus modeling tool status, with viewport runtime
  as projection.
- Checklist: route activation shortcuts through commands/API, sync viewport host from tool commands,
  remove UI/controller-only activation paths.
- Status: completed. Tool activation state now projects from the editor/tool API path and the
  modeling shelf no longer owns an independent mode button state.

Finding: pane/layout state bypasses `ViewportWorkspaceService`.

- Affected files/classes: `src/api/QuaderViewportApi.cpp`, `src/ui/controllers/ViewportHost.cpp`,
  `src/viewport/ViewportWorkspaceService.cpp`.
- Risk: public layout API can report a layout that the Rml viewport host later forces back to
  single-pane.
- Proposed owner/source of truth: `ViewportWorkspaceService`.
- Checklist: make V1 layout capability explicit or render from workspace placements; expose
  windowed pane state through the workspace or explicitly exclude it.
- Status: completed. `ViewportWorkspaceService` now exposes an explicit layout policy; the public
  app viewport API is single-pane until multi-pane Rml rendering is implemented.

Finding: pane `view` duplicates camera-derived state.

- Affected files/classes: `src/viewport/ViewportWorkspaceService.h`,
  `src/viewport/ViewportWorkspaceService.cpp`.
- Risk: const pane readers can observe stale `pane.view` after camera mutation.
- Proposed owner/source of truth: choose camera-derived view or named view, not both.
- Checklist: remove public duplicate view state or make snapshots computed, route camera mutation
  through workspace methods, add const getter regression coverage.
- Status: completed. Pane view is derived from camera state rather than stored as a second field.

Finding: panel labels are accepted as panel IDs.

- Affected files/classes: `src/ui/components/organisms/MainLayoutModel.cpp`,
  `src/ui/UISettings.cpp`, `src/ui/controllers/MainLayoutController.cpp`.
- Risk: localized display labels can become state/routing IDs.
- Proposed owner/source of truth: strict lowercase IDs in `MainLayoutModel`; one-way legacy
  migration in `UISettings`.
- Checklist: split strict canonicalization from legacy migration, save only stable IDs, test old
  settings migration and runtime rejection of display labels.
- Status: completed. Runtime panel IDs reject display labels; `UISettings` performs one-way legacy
  migration and saves stable IDs.

Finding: modeling shelf owns parallel selection-mode state.

- Affected files/classes: `src/ui/UISystem.cpp`, `src/ui/backends/rml/RmlUiContextHost.cpp`.
- Risk: shelf tabs can appear active while the editor modeling selection mode differs.
- Proposed owner/source of truth: editor selection-mode command/public API path; shelf is a
  projection.
- Checklist: derive shelf controls from command/tool descriptors or a presenter, route clicks
  through typed commands/API, remove dummy control, sync active state from editor mode.
- Status: completed. The modeling shelf derives active mode from editor selection mode and routes
  clicks through editor selection APIs.

Finding: tool/menu activation metadata is duplicated.

- Affected files/classes: `src/editor/tools/ViewportTool.cpp`, interactive tool headers,
  `src/editor/metadata/host_contract.cpp`, locale files.
- Risk: tool IDs, labels, shortcuts, menu groups, and toolbar placement can drift.
- Proposed owner/source of truth: `ViewportToolCatalog`/tool descriptors for identity; host UI
  placement owns only group/order.
- Checklist: validate placement targets against catalog, derive localization keys from tool IDs,
  assert locale coverage for catalog tools.
- Status: completed. Tool identity metadata lives in `ViewportToolDescriptorCatalog`; host contract,
  runtime wrappers, and locale guardrails consume that catalog.

Finding: localization placeholder drift is unguarded.

- Affected files/classes: `src/ui/localization/LocalizationService.cpp`, `tests/localization_tests.cpp`,
  `resources/locales/en-US.lang`, `resources/locales/it-IT.lang`.
- Risk: translated catalogs can lose `{count}`, `{speed}`, or other placeholders while parity tests
  pass.
- Proposed owner/source of truth: localization resource tests enforce placeholder contracts;
  runtime formatter remains in `LocalizationService`.
- Checklist: extract placeholder token sets per key, compare fallback and translated catalogs, test
  repeated and unknown-token formatter behavior.
- Status: completed. Localization tests now enforce placeholder-token parity and formatter behavior
  for repeated and unknown tokens.

### Render cache and extraction

Finding: viewport render submission is split.

- Affected files/classes: `src/api/QuaderRenderApi.cpp`, `src/ui/controllers/ViewportHost.cpp`.
- Risk: API render submission and `ViewportHost::prepareViewports()` can fight over overlay
  clearing/visibility.
- Proposed owner/source of truth: `QuaderRenderApi` owns editor-derived render payload submission;
  `ViewportHost` supplies viewport camera/index/layer.
- Checklist: route tool preview and selection overlay submission through `editor.render()`, remove
  direct empty overlay bridge clearing, centralize overlay visibility.
- Status: completed. `ViewportHost` routes tool preview and selection overlay submission through
  `editor.render()`, and overlay visibility comes from `QuaderRenderApi`.

Finding: render signatures repeat hashing policy.

- Affected files/classes: `src/render/MaterialBindingService.cpp`,
  `src/render/MeshRenderableFactory.cpp`, `src/render/ToolPreviewRenderableCache.cpp`,
  `src/render/overlay/OverlayRenderableBuilder.cpp`.
- Risk: cache invalidation can drift across local FNV/hash helpers.
- Proposed owner/source of truth: a small render-owned `RenderSignatureBuilder`.
- Checklist: move hash combine/float/vector helpers and mesh/material/overlay signature helpers
  into one render utility, add signature tests.
- Status: completed. `RenderSignatureBuilder` owns material, mesh, tool preview, overlay, and light
  signature hashing.

Finding: light cache lacks incremental ownership.

- Affected files/classes: `src/render/SceneRenderSyncService.cpp`, `src/render/LightRenderableCache.cpp`.
- Risk: light proxies are recreated on full sync while mesh proxies own incremental rebuild/update
  logic.
- Proposed owner/source of truth: `LightRenderableCache`.
- Checklist: store signature and transform state in `LightProxy`, add `needsRebuild` and
  transform-only update path, keep sync service orchestration-only.
- Status: completed. `LightRenderableCache` owns light rebuild/transform signatures and transform-only
  updates.

### Tests and support

Finding: grouping behavior tests have a parallel scenario DSL.

- Affected files/classes: `tests/grouping_public_api_tests.cpp`,
  `tests/support/BehaviorScenario.*`, `tests/support/BehaviorExpect.*`.
- Risk: grouping tests duplicate scene query, hierarchy, selection, and material expectations.
- Proposed owner/source of truth: `BehaviorScenario` and `BehaviorExpect`.
- Checklist: add grouping/ungrouping/destroy/select/parent/world-position/material expectations to
  the DSL, then port grouping tests.
- Status: completed. Grouping public API tests were ported to `BehaviorScenario` /
  `BehaviorExpect`.

Finding: editor API tests mix public behavior and internal-owner assertions.

- Affected files/classes: `tests/editor_api_tests.cpp`,
  `tests/programmatic_api_guardrail_tests.cpp`.
- Risk: public API tests become a second source of truth for internal selection, scene components,
  and modeling behavior.
- Proposed owner/source of truth: public behavior through `QuaderEditorApi`/`BehaviorScenario`;
  internals in focused guardrail tests.
- Checklist: move delegation checks to guardrail files, replace direct session assertions with
  public query/DSL expectations, guard public behavior tests against internal headers.
- Status: completed. Public behavior tests rely on public query/DSL expectations, with intentional
  internal checks moved or whitelisted in guardrails.

Finding: modeling-domain operation helpers still duplicate common checks.

- Affected files/classes: `tests/modeling_domain/operation/*.cpp`,
  `tests/modeling_domain/operation/operation_test_helpers.h`,
  `tests/modeling_domain/operation/editor_operation_test_helpers.h`.
- Risk: topology and selection assertions drift across operation tests.
- Proposed owner/source of truth: operation helper headers.
- Checklist: move `find_object`/`require_object`, selected component counts, and setup helpers into
  shared helpers, then add a duplicate-helper guardrail.
- Status: completed. Modeling operation helper duplication was consolidated into
  `editor_operation_test_helpers.h`.

Finding: behavior guardrails cover only two behavior files.

- Affected files/classes: `tests/programmatic_api_guardrail_tests.cpp`, `CMakeLists.txt`.
- Risk: new public/editor behavior tests can bypass public APIs or add local harnesses without
  failing guardrails.
- Proposed owner/source of truth: `ProgrammaticApiGuardrailTests`.
- Checklist: include `*_api_tests.cpp`, `*_public_api_tests.cpp`, and `*_behavior*_tests.cpp` in
  public-behavior guardrails; whitelist only intentional internal tests.
- Status: completed. Programmatic behavior guardrails now scan `*_api_tests.cpp`,
  `*_public_api_tests.cpp`, and `*_behavior*_tests.cpp`.

## Verification

- [x] `cmake --build --preset vs2026-debug --target quader_tests`
- [x] `build/vs2026/Debug/quader_tests.exe --gtest_filter=ModelingSessionServiceTests.TransformDragObjectSelectionUsesSceneTransformPreview:ModelingSessionServiceTests.PublicTransformDragObjectSelectionRecordsSceneTransformHistory:OopArchitectureGuardrailTests.SceneTransformDragServiceOwnsTransformPreviewTransactions:UISystemTests.RmlViewportShellLoadsViewportAndMainLayoutSettings`
- [x] `build/vs2026/Debug/quader_tests.exe --gtest_filter=UISettingsTests.MigratesLegacyDisplayPanelNamesOnlyWhenLoadingSettings:SceneRenderBridgeTests.SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged:ViewportRendererTests.MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame:OopArchitectureGuardrailTests.TypeDeclarationsHaveMeaningfulDocumentation:OopArchitectureGuardrailTests.UiShellLeavesSceneOverlayExtractionToRenderBuilder`
- [x] `build/vs2026/Debug/quader_tests.exe` (482 tests)
- [x] `cmake --preset vs2026`
- [x] `cmake --build --preset vs2026-debug`
- [x] `ctest --preset vs2026-debug`

## Smoke Notes

- GUI startup smoke: completed from `build/vs2026/Debug/quader.exe` for 8 seconds. The process
  reached the main loop, was stopped after the smoke interval, and the fresh
  `build/vs2026/Debug/quader.log` had zero `[error]` or `[critical]` entries.
