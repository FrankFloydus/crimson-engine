# Daily DRY/OOP Architecture Refactor - 2026-06-28

## Completion Rule

This file is the source of truth for the 2026-06-28 daily audit run. The Codex goal can be marked
complete only when every actionable item below is checked, targeted verification passes, full
configure/build/ctest pass when practical, and an app startup smoke has no error/critical log
entries. If an actionable item remains unchecked because of tool limits, environment limits, broken
external dependencies, impossible conflicting worktree changes, or GUI/GPU limits, the goal must be
blocked.

## Audit Coverage

The run used six parallel read-only audit subagents plus main-agent source inspection:

- API/commands/public scripting.
- Modeling, mesh, projections, runtime maps, and picking.
- Viewport, input, tools, keybinding state, and viewport host routing.
- UI, RmlUi layout shell, components, localization, and resources.
- Render, assets, scene, and QDR persistence.
- Tests, docs, build/tooling guardrails, examples, and dependency evidence.

The main agent also ran repo-wide searches for duplicated command strings, status/revision stamping,
operation schema projection, direct owner bypasses, raw command contracts, runtime-map exposure,
QDR parsing/validation, Filament raw-resource ownership, and affected-check routing.

## Dependency Order

1. Shared metadata/status/command helper owners that other fixes should call.
2. Public API and command-routing fixes.
3. Modeling/catalog/projection ownership fixes.
4. Scene/QDR/render owner-boundary fixes.
5. UI/viewport/input ownership fixes.
6. Tests, guardrails, dependency evidence, app smoke, and final verification.

## Actionable Findings And Checklist

- [x] Centralize public command target ownership and remove dynamic command bypasses.
  - Affected files/classes/functions: `src/api/quader_commands_api.cpp` (`commandIdForModelingAction`,
    `commandIdForToolActivation`, `Impl::canonicalTargetId`, `Impl::registerCommands`),
    `src/api/quader_command_ids.hpp`, `src/editor/commands/command_tool.hpp`,
    `src/viewport/host_viewport_contract_adapter.hpp`, `tests/editor_api_tests.cpp`,
    `tests/modeling_tool_catalog_tests.cpp`.
  - Risk/impact: command target strings are built and parsed in several layers, and the
    `tools.activate.` prefix handler can execute target strings that were never registered in the
    command catalog or keybinding target set.
  - Owner/source of truth: API command target helpers plus exact command registrations derived from
    the modeling command-tool catalog and viewport tool catalog.
  - Fix checklist:
    - [x] Move modeling/tool command target builders and prefix tests into one reusable helper.
    - [x] Replace local string builders/parsers in API, command tools, host viewport adapter, and
      positive tests with the helper or typed `QuaderCommand` constants.
    - [x] Remove the dynamic `tools.activate.` prefix registration and prove unknown tool command
      IDs fail through the normal registry path.
    - [x] Remove the pass-through `canonicalTargetId` seam.
    - [x] Add/adjust guardrail coverage for no raw positive command target literals outside the
      command ID owner and serialization/negative-test allowlists.

- [x] Centralize API status and revision stamping.
  - Affected files/classes/functions: `src/api/quader_api_mutation_runner.cpp`,
    `src/api/quader_tools_api.cpp`, `src/api/quader_modeling_api.cpp`
    (`resultFromTransformDragPreview`), `src/api/quader_project_api.cpp`, related API status tests.
  - Risk/impact: revision stamps and mutation kinds are constructed by multiple APIs with slightly
    different logic, so direct tools/project/preview paths can drift from command-runner semantics.
  - Owner/source of truth: one internal API status/revision helper used by mutation runner and direct
    non-command API surfaces.
  - Fix checklist:
    - [x] Add a shared internal helper for current revisions, void-result status, failure status,
      and transform-drag preview result stamping.
    - [x] Route mutation runner, tools API, modeling transform drag results, and direct project/API
      status construction through the helper.
    - [x] Add focused tests for direct tools/project/preview status stamps and mutation kinds.

- [x] Move tool setting mutation policy out of `QuaderToolsApi`.
  - Affected files/classes/functions: `src/api/quader_tools_api.cpp` (`setSetting`),
    `src/editor/modeling/services/modeling_operation_settings.*`,
    `src/editor/modeling/operations/operation_schema.hpp`, `tests/modeling_session_service_split_tests.cpp`,
    public API tests.
  - Risk/impact: public API owns operation option aliases and field mutation while runtime schema and
    interaction-settings services own the same data, creating a second policy surface.
  - Owner/source of truth: modeling operation-settings service/schema owner.
  - Fix checklist:
    - [x] Add a modeling-owned setting application helper that maps tool/operation option IDs and
      accepted aliases to `ModelingInteractionSettings`.
    - [x] Make `QuaderToolsApi::setSetting` delegate to that owner.
    - [x] Add schema-backed tests for known settings and unknown setting rejection.

- [x] Derive host operation option/preference descriptors from operation schema owners.
  - Affected files/classes/functions: `src/editor/metadata/host_contract.cpp`
    (`options_for_operation`, `preference_descriptors`, operation preference construction),
    `src/editor/modeling/operations/operation_schema.hpp`, `src/editor/modeling/operations/operation_registry.*`,
    host metadata tests.
  - Risk/impact: host metadata manually enumerates operation options and preference IDs, so operation
    schema additions can appear in runtime tools without appearing in host UI/preference metadata.
  - Owner/source of truth: operation schema/registry owner, with host metadata as a projection.
  - Fix checklist:
    - [x] Expose iterable operation option descriptors/group IDs from the operation schema owner.
    - [x] Build host operation options and operation-scoped preferences from those descriptors.
    - [x] Keep cut-mode and viewport preferences in their current owners or explicitly document why
      they are separate metadata surfaces.
    - [x] Add guardrails that host operation descriptors and preferences cover the operation registry.

- [x] Consolidate modeling action/selection metadata conversions.
  - Affected files/classes/functions: `src/editor/modeling/services/modeling_action_catalog.cpp`,
    `src/editor/commands/modeling_command_tool_catalog.cpp`,
    `src/editor/metadata/host_contract.cpp` (`selection_mode_catalog`, action descriptors),
    `src/ui/controllers/modeling_mode_shelf_controller.cpp`, `tests/modeling_tool_catalog_tests.cpp`,
    `tests/core_tests.cpp`.
  - Risk/impact: selection-mode IDs and action/preview ownership are converted independently in
    host metadata, modeling catalogs, command tools, and UI shelf code.
  - Owner/source of truth: a shared host/modeling metadata adapter derived from runtime action and
    selection-mode catalog owners.
  - Fix checklist:
    - [x] Extract shared selection-mode ID/label/mode conversion helpers.
    - [x] Route modeling action catalog, host metadata, command tool ownership tests, and shelf
      control mapping through the shared helper.
    - [x] Add guardrail tests that selection mode IDs are not copied as local string tables.

- [x] Derive legacy box command descriptors from the viewport tool catalog.
  - Affected files/classes/functions: `src/editor/tools/base/viewport_tool_descriptor_catalog.cpp`,
    `src/editor/modeling/services/modeling_session_service.cpp` (`makeBoxToolDescriptor`,
    `makeCreateBoxDescriptor`), `tests/core_tests.cpp`.
  - Risk/impact: the Box tool label/shortcut/viewport-scoped metadata is stored in both the viewport
    tool descriptor catalog and legacy modeling command descriptors.
  - Owner/source of truth: viewport tool catalog for tool activation metadata; modeling service only
    for modeling command execution.
  - Fix checklist:
    - [x] Derive the legacy Box activation descriptor from the viewport tool catalog or remove the
      legacy descriptor if no call site needs it.
    - [x] Keep create-box-from-bounds command metadata distinct if it is not a viewport tool.
    - [x] Add a catalog parity test.

- [x] Split modal tool overlay and preview projection ownership out of `ModelingSessionService`.
  - Affected files/classes/functions: `src/editor/modeling/services/modeling_session_service.cpp`
    (modal overlay, cut/mirror preview projection, operation overlay projection),
    `src/editor/modeling/services/*`, overlay/projection tests.
  - Risk/impact: the facade still owns projection/cache behavior alongside command orchestration,
    making it easy for new previews to bypass established projection/cache invalidation owners.
  - Owner/source of truth: a dedicated modeling tool overlay/projection service, with
    `ModelingSessionService` as orchestration only.
  - Fix checklist:
    - [x] Extract modal active-tool overlay and preview projection functions into a narrow service.
    - [x] Route `ModelingSessionService` through that service without behavior changes.
    - [x] Add/adjust guardrail tests that overlay/projection code no longer lives in the facade.

- [x] Hide `ModelingObjectRuntimeMap` raw maps behind narrow owner queries.
  - Affected files/classes/functions: `src/editor/modeling/services/modeling_object_runtime_map.hpp`,
    `src/editor/projection/picking_cache_projection.cpp`,
    `src/editor/selection/selection_projection_service.cpp`,
    `src/editor/modeling/services/modeling_mesh_sync_service.cpp`,
    `src/editor/modeling/services/modeling_session_service.cpp`.
  - Risk/impact: projection/sync callers inspect raw bidirectional maps, spreading identity
    invariants outside the runtime-map owner.
  - Owner/source of truth: `ModelingObjectRuntimeMap` lookup/iteration methods that return only the
    data each caller needs.
  - Fix checklist:
    - [x] Add narrow iteration/lookup methods for scene identity, entity-to-object, and object-to-entity
      use cases.
    - [x] Replace direct `objectsByEntity()`/`entitiesByObject()` calls.
    - [x] Add guardrail tests forbidding raw map access outside the owner/tests.

- [x] Route click pick-selection through cached picking inputs.
  - Affected files/classes/functions: `src/editor/modeling/services/modeling_session_service.cpp`
    (`pickSelect`), `src/editor/modeling/services/modeling_selection_service.*`,
    `src/editor/projection/picking_cache_projection.*`, picking tests.
  - Risk/impact: hover and selected-hit preservation use cached pick bounds/face geometry, but
    actual click/multiclick selection calls rebuild or bypass part of the cached owner path.
  - Owner/source of truth: modeling selection/picking service using the shared cached picking inputs.
  - Fix checklist:
    - [x] Extend the selection service or cached-pick helper to cover normal, edge-loop, face-ring,
      and vertex-multiclick selection paths.
    - [x] Route `pickSelect` through that owner.
    - [x] Add regression coverage for cache signature reuse and identical pick behavior.
    - [x] Preserve object-mode hover source-wire overlay through the runtime hover overlay owner after
      cached pick routing exposed the gap.

- [x] Put viewport input-frame projection in one owner.
  - Affected files/classes/functions: `src/ui/controllers/viewport_host.cpp`
    (`prepareFrame`, `buildMainViewportInputFrame`, `buildWindowedViewportInputFrame`, accumulated
    input helpers), `src/viewport/*`, viewport host tests.
  - Risk/impact: embedded and windowed viewport input frames are assembled by parallel code paths,
    so cursor capture, button/key state, DPI, and transient state can diverge.
  - Owner/source of truth: a shared viewport input surface/frame projector helper used by both
    main and windowed viewport paths.
  - Fix checklist:
    - [x] Extract the common frame assembly into one helper/data contract.
    - [x] Replace both host paths with the helper.
    - [x] Add tests that embedded and windowed frame projection stay equivalent for shared inputs.

- [x] Make workspace grid preset the single mutable owner.
  - Affected files/classes/functions: `src/viewport/viewport_workspace_service.*`,
    `src/viewport/viewport_input_router.*`, `src/ui/controllers/viewport_host.cpp`,
    `src/api/quader_viewport_input_types.hpp`, viewport tests.
  - Risk/impact: grid preset state is stored and mutated by both router and workspace, with host
    synchronization bridging two mutable sources.
  - Owner/source of truth: `ViewportWorkspaceService` durable viewport grid preset; router emits
    requested preset changes or consumes a snapshot only.
  - Fix checklist:
    - [x] Remove persistent grid preset mutation from the router.
    - [x] Route input requests to workspace, then feed workspace snapshots back to router/frame code.
    - [x] Add tests for revision/preset sync and shortcut-driven changes.

- [x] Route transform-gizmo tool conversion through the tool catalog.
  - Affected files/classes/functions: `src/editor/viewport/transform_gizmo_interaction.cpp`,
    `src/editor/tools/base/viewport_tool_catalog.cpp`,
    `src/editor/modeling/services/modeling_session_service.cpp`, viewport/modeling tests.
  - Risk/impact: gizmo logic maps viewport tool IDs to runtime tools with a local switch that can
    drift from catalog/modeling conversion helpers.
  - Owner/source of truth: viewport tool catalog/runtime conversion helper.
  - Fix checklist:
    - [x] Expose or reuse the existing runtime conversion helper.
    - [x] Replace the local gizmo switch.
    - [x] Add a catalog parity guardrail.

- [x] Route measured sidebar clamps through `MainLayoutController`.
  - Affected files/classes/functions: `src/ui/controllers/main_layout_sash_registry.cpp`
    (`normalizeMeasuredBounds`), `src/ui/controllers/main_layout_controller.*`,
    `tests/core_tests.cpp`.
  - Risk/impact: measured-width normalization writes `MainLayoutModel` side dock widths directly,
    bypassing collapsed-state and clamp semantics owned by `MainLayoutController`.
  - Owner/source of truth: `MainLayoutController`.
  - Fix checklist:
    - [x] Add a controller method for measured side-dock width clamping that preserves collapsed
      state and uses the same clamp policy.
    - [x] Replace direct model writes in the sash registry.
    - [x] Add a focused controller/sash test for collapsed side docks and measured clamps.

- [x] Remove localized English defaults from retained modeling shelf state.
  - Affected files/classes/functions: `src/ui/components/organisms/modeling_mode_shelf_component.hpp`,
    `src/ui/controllers/modeling_mode_shelf_controller.cpp`, `resources/locales/*.lang`,
    `tests/core_tests.cpp`.
  - Risk/impact: retained component state has a raw English default while the controller/locales own
    UI copy, allowing untranslated fallback text to leak if controller wiring regresses.
  - Owner/source of truth: localization/controller projection.
  - Fix checklist:
    - [x] Remove the `"More"` default from retained state and require controller-supplied text.
    - [x] Add or update guardrail coverage that component state does not own raw localized copy.

- [x] Centralize QDR root/header parsing.
  - Affected files/classes/functions: `src/io/qdr/qdr_document.cpp`,
    `src/io/qdr/qdr_document_reader.cpp`, `src/io/qdr/qdr_document.hpp`,
    `src/io/qdr/qdr_format.hpp`, `tests/io_qdr_tests.cpp`.
  - Risk/impact: root format/version parsing and common keys are duplicated between generic QDR
    document parsing and project document reader paths.
  - Owner/source of truth: a shared QDR root/header parser/helper in `src/io/qdr`.
  - Fix checklist:
    - [x] Add shared root/header parse helpers and constants.
    - [x] Route both QDR document and project document readers through them.
    - [x] Add invalid/oversized version and missing-root tests through both reader surfaces.

- [x] Move project asset-reference validation to the document/asset owner.
  - Affected files/classes/functions: `src/io/qdr/qdr_document_reader.cpp`, `src/scene/document/project_document.*`,
    `src/assets/*`, `tests/io_qdr_tests.cpp`, scene/document tests.
  - Risk/impact: QDR reader validates scene mesh/material/material-slot references and object
    records while `ProjectDocument` owns only identity invariants, so non-QDR document creation can
    bypass the same checks.
  - Owner/source of truth: `ProjectDocument`/asset boundary validation report.
  - Fix checklist:
    - [x] Add a `ProjectDocument` asset-reference validation report that checks scene and material
      references against the project asset registry.
    - [x] Route QDR reader validation through that owner.
    - [x] Add invariant tests independent of QDR deserialization.

- [x] Harden Filament resource creation with render-owned guards.
  - Affected files/classes/functions: `src/render/light_renderable_cache.cpp`
    (`createLightProxy`), `src/render/viewport_grid_renderer.cpp` (`createGroundGrid`),
    `src/render/viewport_render_target.cpp` (`resize`), render tests/guardrails.
  - Risk/impact: partial allocation failure can leak or leave half-owned Filament resources because
    several render owners allocate multiple raw handles without local creation guards.
  - Owner/source of truth: render-layer RAII/creation guard helpers per resource owner.
  - Fix checklist:
    - [x] Add local creation guards for light proxies, grid geometry/material/renderable, and
      render-target textures/views.
    - [x] Ensure guards release ownership only after the owning object has fully recorded resources.
    - [x] Add source/behavior guardrails for non-transferable render owners and guarded creation.

- [x] Move windowed viewport swapchain lifetime behind a render-owned RAII handle.
  - Affected files/classes/functions: `src/render/render_system.*`,
    `src/ui/controllers/viewport_host.cpp` (`ViewportWindowInstance`, `spawnWindow`,
    `shutdownWindowedViewport`), `tests/core_tests.cpp`.
  - Risk/impact: UI host owns raw Filament swapchain pointers directly, duplicating render-system
    resource lifetime policy outside the render layer.
  - Owner/source of truth: `RenderSystem` RAII handle for additional swapchains.
  - Fix checklist:
    - [x] Add a non-copyable render-owned additional-swapchain handle that destroys through
      `RenderSystem`.
    - [x] Store the handle in `ViewportWindowInstance` instead of a raw pointer.
    - [x] Keep `beginFrame` using an explicit raw pointer accessor.
    - [x] Update source-policy tests.

- [x] Complete dependency license/manifest evidence for declared direct dependencies.
  - Affected files/classes/functions: `vcpkg.json`, `thirdparty/THIRD_PARTY_MANIFEST.md`,
    `thirdparty/licenses/*`, `tests/build_guardrail_tests.cpp`,
    `agents/dependencies.md`.
  - Risk/impact: direct dependencies declared in `vcpkg.json` can be added without third-party
    manifest/license evidence because the guardrail is hand-curated and incomplete.
  - Owner/source of truth: declared direct dependency list plus manifest/license evidence.
  - Fix checklist:
    - [x] Compare direct `vcpkg.json` dependencies with manifest/license evidence.
    - [x] Add missing manifest/license records or document non-bundled/system cases.
    - [x] Update the guardrail to derive expected entries from a declared source or explicit allowlist.

- [x] Add missing docs-route affected-check guardrail.
  - Affected files/classes/functions: `tools/affected_check.ps1`,
    `tests/programmatic_api_guardrail_tests.cpp` (`PublicExamplesStartFromEditorApiOrScriptSurface`),
    `tests/build_guardrail_tests.cpp`.
  - Risk/impact: docs/public-example edits can skip the existing public entry-surface guardrail in
    the affected workflow.
  - Owner/source of truth: affected-check route table.
  - Fix checklist:
    - [x] Route relevant docs/examples/API documentation changes to the public entry-surface guardrail.
    - [x] Add a tool/source guardrail so future affected-check edits keep that route.

- [x] Stop public API behavior tests from bootstrapping through internal session owners.
  - Affected files/classes/functions: `tests/programmatic_api_tests.cpp`,
    `tests/editor_api_tests.cpp`, `tests/support/BehaviorScenario.*`,
    `src/api/quader_api_session_internal_access.hpp`, `tests/programmatic_api_guardrail_tests.cpp`.
  - Risk/impact: public-intent tests can pass while exercising internal `ProjectSession` bootstrap
    seams that real public API consumers cannot use.
  - Owner/source of truth: `QuaderStandaloneEditor`, `QuaderEditorApi`, and BehaviorScenario public
    test harnesses; internal owner tests remain explicit and isolated.
  - Fix checklist:
    - [x] Migrate public-intent behavior tests to public standalone editor/BehaviorScenario helpers
      where feasible.
    - [x] Keep explicitly internal owner tests in lower-level suites.
    - [x] Tighten guardrails so public API behavior tests do not reach `ProjectSession` or
      `QuaderApiSessionInternalAccess`.

## Reclassified As Not Actionable For This Run

- [x] Previous-run findings for script transactions, public API header internal types, selection
  center projection, modal keybinding gestures, and host UI placement metadata were rechecked and
  remain covered by current code/tests.
  - Evidence: `src/api/quader_script.cpp` runs scripts through an `EditorTransaction`;
    `tests/modeling_session_service_split_tests.cpp` asserts selection center projection lives in
    `SelectionProjectionService`; `tests/editor_api_tests.cpp` contains modal gesture keybinding
    coverage; `src/ui/controllers/modeling_mode_shelf_controller.cpp` resolves
    `operations_top_bar` host placements.

- [x] Viewport color/grid preference values duplicated in `host_contract.cpp` and viewport/render
  code are not changed in this run because the current host metadata target is compiled in the
  absorbed modeling/core metadata layer and cannot depend on runtime viewport/render packages without
  reversing package ownership.
  - Evidence: `src/editor/metadata/host_contract.cpp` lives under editor metadata and includes
    modeling/runtime headers; viewport/render preference defaults are in `src/viewport` and
    `src/render`. The actionable operation metadata subset remains above because it already depends
    on operation schema owners in the same metadata layer.

## Verification Log

- [x] Targeted tests for command/status/tool-setting phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.DirectApiStatusesUseFullRevisionStamp:EditorApiTests.RetiredModelingActionCommandAliasesDoNotRouteThroughCommandRegistry:EditorApiTests.ViewportKeybindingsResolveContextAndUserOverrides:ModelingToolCatalogTests.PublicCommandBridgeMetadataLivesOnCommandDescriptors:ModelingSessionServiceSplitTests.ToolSettingMutationComesFromOperationSettingsOwner:ModelingSessionServiceSplitTests.InstantOperationSettingsMappingHasSingleOwner`;
  `cmake --build --preset vs2026-debug --target quader_guardrail_tests`;
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiGuardrailTests.PublicCommandTargetsUseSharedPrefixOwner:ProgrammaticApiGuardrailTests.ApiStatusStampsUseSharedInternalHelper:ProgrammaticApiGuardrailTests.PublicModelingCommandBridgeIsDescriptorDriven`.
- [x] Targeted tests for operation metadata phase:
  `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`;
  `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadataSchemaMirrorTests.*:QdrEditorMetadataPreferenceCatalogTests.*`;
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceSplitTests.HostOperationMetadataUsesSchemaOptionDescriptorCatalog:ModelingSessionServiceSplitTests.ToolSettingMutationComesFromOperationSettingsOwner:ModelingSessionServiceSplitTests.InstantOperationSettingsMappingHasSingleOwner`.
- [x] Targeted tests for selection metadata phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=SelectionModeAdapterTests.*:ModelingModeShelfControllerTests.ControlsFollowOperationsTopBarPlacementOrder:ModelingSessionServiceSplitTests.HostOperationMetadataUsesSchemaOptionDescriptorCatalog`.
- [x] Targeted tests for Box tool descriptor phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceTests.ExposesBoxCommandContract`.
- [x] Targeted tests for overlay/projection extraction phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceTests.CachesSelectionOverlayFrameByRevisionAndHoverState:ModelingSessionServiceTests.*Overlay*:ModelingSessionServiceSplitTests.*Overlay*`.
- [x] Targeted tests for runtime-map ownership phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceSplitTests.ObjectRuntimeMapStaysOutOfServiceBucket:ModelingSessionServiceSplitTests.ObjectSelectionMappingLivesInSelectionProjectionService:ModelingSessionServiceSplitTests.MeshSyncPreservesPerEntityPrimaryMaterialsForSharedModelingSlots:ModelingSessionServiceSplitTests.DuplicateSceneIdentityLivesInRuntimeMapAndMeshSync:ModelingSessionServiceTests.InstantOperationsRefreshSceneObjectsAndSelection:ModelingSessionServiceTests.CachesSelectionOverlayFrameByRevisionAndHoverState:ModelingSessionServiceTests.SelectionCenterAndOverlayRespectSceneTransforms`.
- [x] Targeted tests for cached click-pick selection phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceSplitTests.ClickPickSelectionUsesSelectionServiceCachedInputs:ModelingSessionServiceTests.PickAndHoverRespectSceneTransformsForLocalMeshes:ModelingSessionServiceTests.RmbMeshPickInFaceModeSelectsTargetAndAllowsContextMenu:ModelingSessionServiceTests.RmbObjectPickOnSelectedMultiSelectionPreservesSelection:ModelingSessionServiceTests.FacePickKeepsSceneEntitySelectedForInspectorAndGizmo:ModelingSessionServiceTests.InsertEdgeLoopUsesViewportPickRadius:ModelingSessionServiceTests.CachesSelectionOverlayFrameByRevisionAndHoverState`.
- [x] Targeted tests for transform-gizmo catalog conversion phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportToolCatalogTests.*:ViewportToolGuardrailTests.TransformGizmoEligibilityLivesInViewportToolMetadata`.
- [x] Targeted tests for viewport input-frame and grid ownership phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportWorkspaceServiceTests.GridPresetIsClampedAtWorkspaceOwner:ViewportInputRouterTests.GridPresetWorldSizesMatchMapUnitContract:ViewportInputRouterTests.GridPresetShortcutReportsOnlyActualChanges:UISystemTests.RmlViewportShellCapturesFlyNavigationInput:ViewportToolCatalogTests.ExposesToolMetadataAndConversions:ViewportToolGuardrailTests.TransformGizmoEligibilityLivesInViewportToolMetadata`.
- [x] Targeted tests for measured layout clamps and modeling shelf localization phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=MainLayoutSashRegistryTests.SidebarsAndBottomDockSnapCollapseThroughMainLayoutSashes:MainLayoutSashRegistryTests.MeasuredSidebarClampsRouteThroughControllerWithoutUncollapse:MainLayoutSashRegistryTests.ViewportMinimumSizeComesFromMainLayoutModel:ModelingModeShelfComponentTests.RendersOverflowToggleAndMenuFromState:ModelingModeShelfControllerTests.ControlsFollowOperationsTopBarPlacementOrder`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=UISystemTests.RmlShellOwnsMainLayoutAndMeasuredViewportPanel`.
- [x] Targeted tests for QDR root/header and project asset-reference ownership phase:
  `cmake --preset vs2026`;
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProjectDocumentTests.ValidatesAssetReferencesAtDocumentOwner:QdrDocumentIoTests.RootHeaderParsingUsesSharedHelperAcrossReaderSurfaces:QdrDocumentIoTests.RejectsUnknownCanonicalProjectPayloadKindWithoutLegacyMigration`;
  `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`;
  `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=QdrIoDocumentTests.*`.
- [x] Targeted tests for render resource guards and swapchain RAII phase:
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=UISystemTests.WindowedViewportComponentsAreBoundedAndNonExperimental:RenderProxyLifecycleTests.FilamentResourceCreationUsesRenderOwnedGuards:RenderProxyLifecycleTests.RenderResourceOwnersAreNonTransferable:TextureRegistryTests.TracksPerTexturePresentationColorSpace`.
- [x] Targeted tests for dependency evidence, docs affected-check routing, and public API test bootstrap phase:
  `cmake --build --preset vs2026-debug --target quader_guardrail_tests`;
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=DependencyLicenseGuardrailTests.*:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns:ProgrammaticApiGuardrailTests.PublicBehaviorTestsUseApiOrScenarioDsl:ProgrammaticApiGuardrailTests.PublicExamplesStartFromEditorApiOrScriptSurface`;
  `cmake --build --preset vs2026-debug --target quader_tests`;
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiTests.*:EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.ProjectDocumentCommandsRouteThroughProjectApi:EditorApiTests.PublicAssetApiOwnsStandaloneMeshReplacementPolicy:EditorApiTests.SceneCommandsRejectModelingOwnedEntityStructuralAndMeshMutations:EditorApiTests.PreferencesPersistAndResolveCustomCommandShortcuts:EditorApiTests.PersistedKeybindingsRejectUnknownTargets:EditorApiTests.PublicMutationsMarkStandaloneProjectDirty:EditorApiTests.PublicAssetMutationsRejectDanglingReferences:EditorApiTests.DirectApiStatusesUseFullRevisionStamp:ProjectSessionTests.DirtyStateAggregatesProjectSceneAssetAndModelingContentRevisions:ProjectSessionTests.PublicHistoryIgnoresUntrackedModelingSessionHistory`;
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -PlanOnly -ChangedPath docs\api-overview.md`.
- [x] Final configure: `cmake --preset vs2026`.
- [x] Final build: `cmake --build --preset vs2026-debug`.
- [x] Final tests: `ctest --preset vs2026-debug --output-on-failure`.
- [x] App startup smoke:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\boot_smoke.ps1 -Exe build\vs2026\Debug\quader.exe -ScreenshotPath build\vs2026\Debug\smoke\quader-smoke.png -LogCopyPath build\vs2026\Debug\smoke\quader.log -TimeoutSeconds 30`;
  screenshot and log were captured, and the copied log had no error/critical/fatal matches.
