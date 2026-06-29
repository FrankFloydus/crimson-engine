# Daily DRY/OOP Drift Audit - 2026-06-29

This file is the source of truth for the current daily audit run. A run item is
complete only when it is checked here with the fix and verification recorded, or
when it is moved to the reclassified section with concrete code evidence.

## Run Status

- Status: complete.
- Audit sources: six scoped subagents plus main repo-wide inspection.
- Global verification:
  - `cmake --preset vs2026`
  - `cmake --build --preset vs2026-debug`
  - `ctest --preset vs2026-debug`
- GUI smoke: `tools/boot_smoke.ps1` against
  `build/vs2026/Debug/quader.exe`; screenshot copied to
  `build/vs2026/Debug/daily-dry-oop-boot-smoke.png`, log copied to
  `build/vs2026/Debug/daily-dry-oop-boot-smoke.log`, and no
  error/critical/fatal log entries were detected.

## Dependency Order

1. Command, shortcut, and keybinding source-of-truth fixes.
2. UI panel/localization/menu and public-behavior guardrails.
3. Modeling metadata/settings/picking/projection ownership fixes.
4. Scene/render/assets/QDR source-of-truth fixes.
5. Docs/build/dependency guardrails and filename/resource contracts.
6. Targeted tests after each phase, then full configure/build/ctest and smoke
   when practical.

## Actionable Findings And Checklist

### API, Commands, Viewport Input

- [x] A01 - Viewport shortcut command mapping lives in
  `src/ui/viewport_shortcut_controller.cpp` instead of the command/host contract
  owners.
  - Evidence: local `commandForMirrorAxis`, `executeViewportModelingShortcut`,
    and `ViewportShortcutKind` to `QuaderCommand` switch duplicate
    `QuaderCommandsApi::dispatchViewportInputBinding` and
    `src/viewport/host_viewport_contract_adapter.hpp` command target helpers.
  - Risk: shortcut contracts can drift across UI, host metadata, and command
    registry.
  - Owner/source of truth: `QuaderCommandsApi` plus host viewport contract
    target helpers.
  - Fix checklist: remove local UI command remapping, route router-produced
    shortcut intents through the shared command target owner, and update
    `ProgrammaticApiGuardrailTests.ViewportShortcutControllerDoesNotRemapCommands`.
  - Fix: `ViewportShortcutController` now resolves command-worthy router
    shortcuts through `viewportCommandTargetIdForRouterShortcut` and executes the
    returned public command target. It no longer includes command IDs or modeling
    service dependencies.
  - Verification: `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.ViewportShortcutControllerDoesNotRemapCommands`;
    `quader_tests`
    `EditorApiTests.ViewportHostShortcutCatalogDrivesCommandKeybindings` and
    `GroupingPublicApiTests.ViewportDeleteShortcutDeletesSelectedGroup`.

- [x] A02 - Selection-mode shortcuts are handled twice.
  - Evidence: `ViewportPanel` handles `ViewportShortcutKind::SetSelectionMode`,
    then still calls `ViewportShortcutController`, which handles the same kind.
  - Risk: side effects for selection mode, modal state, hover state, and overlay
    invalidation are split.
  - Owner/source of truth: `ViewportPanel` for presentation cleanup and public
    `editor.selection()` for the intent.
  - Fix checklist: execute selection-mode shortcut once and remove unused
    `ModelingSessionService&` dependency from shortcut-controller API/tests.
  - Fix: selection-mode shortcuts remain router/panel intents; the shortcut
    controller no longer applies selection mode and no longer takes
    `ModelingSessionService&`.
  - Verification: `quader_tests`
    `EditorApiTests.ViewportSelectionModeShortcutsResolveAsRouterIntents`;
    source sweep found no `scenario.session().modeling()` controller call sites.

- [x] A03 - `EditorCommandRegistry` still exposes an unused prefix alias route.
  - Evidence: `registerPrefix`, `PrefixRoute`, `prefixes_`, and prefix execution
    exist with no active call sites.
  - Risk: dormant alias/canonicalization path conflicts with exact typed command
    IDs and retired-alias guardrails.
  - Owner/source of truth: exact command registrations plus
    `core/public_command_targets.hpp`.
  - Fix checklist: remove prefix registration/execution and strengthen the
    public command target guardrail.
  - Fix: removed `PrefixHandler`, `registerPrefix`, `PrefixRoute`, `prefixes_`,
    and prefix execution fallback.
  - Verification: `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.PublicCommandTargetsUseSharedPrefixOwner`.

- [x] A04 - Viewport view command mapping has two owners.
  - Evidence: `QuaderCommandsApi` registers/executes view commands for the
    Perspective pane, while `ViewportPanel` locally maps the same command IDs for
    focused-pane shortcut dispatch.
  - Risk: adding or renaming view commands can update one path but not the other;
    direct command execution and focused-pane shortcuts can drift.
  - Owner/source of truth: shared viewport command mapping in the API/viewport
    command layer.
  - Fix checklist: extract one command-to-view mapping, use it from command
    registration and focused-pane shortcut dispatch, and document/test the direct
    command versus focused-pane shortcut contract.
  - Fix: `host_viewport_contract_adapter.hpp` now owns
    `viewportViewForCommandTargetId`; view command keybindings dispatch as
    `ViewportShortcutKind::SetView`, and `ViewportPanel` applies that focused-pane
    request from `ViewportInputResult`.
  - Verification: `quader_tests`
    `EditorApiTests.ViewportHostShortcutCatalogDrivesCommandKeybindings` and
    `ViewportInputRouterTests.ViewShortcutReportsFocusedPaneRequest`.

- [x] A05 - Viewport shading mode conversions are duplicated.
  - Evidence: `src/api/quader_viewport_api.cpp` and
    `src/ui/viewport_panel.cpp` both define `QuaderShadingMode` <->
    `ViewportShadingMode` switches.
  - Risk: future shading modes can drift between API and UI pane chrome.
  - Owner/source of truth: viewport shading conversion helper used by both call
    sites.
  - Fix checklist: extract shared conversion functions and add a guardrail
    preventing local UI/API duplicate switches.
  - Fix: added `src/viewport/viewport_shading_mode_conversions.hpp` and replaced the
    local conversion switch pairs in `QuaderViewportApi` and `ViewportPanel`.
  - Verification: `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.ViewportShadingConversionUsesSharedApiHelper`.

- [x] A06 - Modal cancel still has a raw Escape bypass.
  - Evidence: `ViewportPanel` checks `ImGuiKey_Escape` directly even though
    cancel is registered as a viewport keybinding target.
  - Risk: user rebinding can move cancel away from Escape, but raw Escape still
    cancels modal tools.
  - Owner/source of truth: `QuaderCommandsApi`, `QuaderKeybindingService`, and
    `ViewportInputRouter` cancel result.
  - Fix checklist: remove raw Escape cancel path and add a regression for
    rebinding cancel away from Escape.
  - Fix: removed the raw `ImGuiKey_Escape` modal cancel path from
    `ViewportPanel`; modal cancel now flows through resolved viewport shortcuts.
  - Verification: source sweep found no raw Escape cancel path in
    `ViewportPanel`; affected tests built and focused viewport shortcut tests
    passed.

- [x] A07 - Tool activation group-selection preservation is duplicated in UI.
  - Evidence: `ViewportPanel::activateToolFromMenu` and the shortcut activation
    path snapshot/restore group selection even though
    `ModelingSessionService::activateViewportTool` owns preservation through
    `Tool::activate()`.
  - Risk: menu, shortcut, command palette, and API activation can diverge.
  - Owner/source of truth: `ModelingSessionService::activateViewportTool` via
    `QuaderToolsApi::activate`.
  - Fix checklist: remove UI snapshot/restore and keep only router sync after
    activation.
  - Fix: removed UI snapshot/restore selection policy from menu and shortcut
    tool activation; `ViewportPanel` now syncs router state from the public
    selection summary after owner activation.
  - Verification: source sweep found no `preserveGroupSelection` activation
    logic in `ViewportPanel`; affected test targets built and focused shortcut
    tests passed.

- [x] A08 - Live ImGui viewport bypasses the editor-owned viewport interaction
  controller, and transform gizmo mapping/state is duplicated in UI.
  - Evidence: `ViewportPanel` owns `TransformGizmoController` and
    `ViewportSelectionController` while `src/editor/viewport` has tested
    `ViewportInteractionController`, `TransformGizmoInteraction`, and
    `ViewportSelectionInteraction`; guardrails already expect UI transform
    controller removal.
  - Risk: synthetic viewport interaction tests protect a path the live ImGui
    viewport does not use; transform and fallback selection behavior can drift.
  - Owner/source of truth: `ViewportInteractionController` and editor viewport
    interaction classes.
  - Fix checklist: wire `ViewportPanel` through `ViewportInteractionController`,
    remove UI transform-gizmo mapping/state ownership, and strengthen guardrails.
  - Fix: `ViewportPanel` now owns a `ViewportInteractionController`, feeds live
    `ViewportInteractionFrame` data through `handleBeforeActiveTool` and
    `handleAfterActiveTool`, uses controller-owned gizmo/marquee state for render
    overlays, and routes context-menu target picking through the same owner.
    Deleted the obsolete UI `TransformGizmoController` and
    `ViewportSelectionController` sources from the runtime target.
  - Verification: `quader_tests`
    `ViewportInteractionControllerTests.*`,
    `ViewportSelectionMarqueeOverlayBuilderTests.*`,
    `GroupingPublicApiTests.ViewportDeleteShortcutDeletesSelectedGroup`,
    `ProgrammaticBehaviorScenarioTests.RayPickingAndContextClickSemanticsPreserveComponentAuthority`,
    and
    `ProgrammaticBehaviorScenarioTests.SelectionAndHoverOverlaysAreBoundedDistinctDataLayers`;
    `quader_guardrail_tests`
    `OopArchitectureGuardrailTests.SceneTransformDragServiceOwnsTransformPreviewTransactions`.

- [x] A09 - `host_viewport_contract_adapter.hpp` is an oversized inline
  cross-layer catalog adapter.
  - Evidence: the header includes API commands, public command targets,
    modeling command catalog, selection adapters, viewport tools, and host
    metadata while defining large inline catalog functions.
  - Risk: broad rebuild/coupling surface and hard-to-guard implementation logic
    in a widely included header.
  - Owner/source of truth: host viewport contract adapter implementation in a
    `.cpp` file with a narrow declaration header.
  - Fix checklist: move implementation bodies to a `.cpp`, keep declarations in
    the header, and add a guardrail against large inline catalog bodies.
  - Fix: moved host viewport contract adapter implementation to
    `src/viewport/host_viewport_contract_adapter.cpp`, added it to `quader_core`,
    and reduced the header to type declarations/function declarations only.
  - Verification: `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.HostViewportContractAdapterKeepsImplementationOutOfHeader`,
    `ProgrammaticApiGuardrailTests.PublicCommandTargetsUseSharedPrefixOwner`, and
    `OopArchitectureGuardrailTests.EverySourceCppIsPartOfCMakeSourceTruth`;
    `quader_tests`
    `EditorApiTests.ViewportHostShortcutCatalogDrivesCommandKeybindings`,
    `SelectionModeAdapterTests.SelectionModeIdsHaveOneHostCatalogAdapter`, and
    `ViewportToolGuardrailTests.HostViewportAdapterUsesViewportToolDescriptors`.

### UI, Panels, Localization

- [x] U01 - Stable panel IDs are English display labels.
  - Evidence: `uiPanelKey` returns `Viewport`, `Outliner`, `Inspector`, and
    `Console`; settings and menu/API calls persist/route those strings.
  - Risk: panel routing and settings are coupled to display English instead of
    backend-neutral IDs.
  - Owner/source of truth: `UIPanel`/`UIPanelManager` stable panel ID helper,
    separate from localized labels.
  - Fix checklist: add lowercase stable IDs, migrate old cased settings keys,
    save lowercase keys only, update menu/API callers, and add guardrails/tests.
  - Fix: added lowercase `uiPanelStableId`, migrated runtime/menu/settings
    routing to stable IDs, and kept legacy cased panel keys accepted on load.
  - Verification: `quader_tests`
    `UISettingsTests.RoundTripsPanelVisibilityLocaleAndNavigationGizmo` and
    `UISettingsTests.LoadsLegacyDisplayPanelKeysAndSavesStableKeys`;
    `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.UiPanelRoutingUsesStableIdsAndNoDeadShowMenu`.

- [x] U02 - Panel visibility defaults are duplicated.
  - Evidence: `ConsolePanel::defaultVisible`, `UIPanelManager::defaultVisibility`,
    and `UISettings` each encode defaults.
  - Risk: new or changed panels can desync runtime defaults and persisted
    fallback behavior.
  - Owner/source of truth: `UIPanelManager` registration metadata.
  - Fix checklist: store defaults once in panel registration metadata and let
    `UISettings` store overrides only.
  - Fix: added `uiPanelDefaultVisible` as the shared panel default owner,
    removed the console override, and routed manager/settings defaults through it.
  - Verification: `quader_tests`
    `UISettingsTests.DefaultsKeepPanelVisibilityLocaleAndNavigationGizmo`;
    `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.UiPanelRoutingUsesStableIdsAndNoDeadShowMenu`.

- [x] U03 - UI runtime/status text bypasses localization resources.
  - Evidence: `UISystem` draws raw `Ready` and returns raw runtime hook messages;
    `ViewportShortcutController` uses raw `Apply Tool Preview`.
  - Risk: UI/API-facing messages leak English and history labels diverge by
    activation path.
  - Owner/source of truth: `LocalizationService` through `UISystem`/runtime
    hooks; shared preview-apply history label owner.
  - Fix checklist: route visible/status/runtime messages and preview apply labels
    through localization.
  - Fix: status bar ready text and runtime hook messages now use locale keys;
    operation-preview shortcuts now execute the command target instead of
    committing a raw UI label from `ViewportShortcutController`.
  - Verification: `quader_tests`
    `LocalizationResourceTests.ShellRuntimeStringsRouteThroughLocaleKeys` and
    `LocalizationResourceTests.UISourceLiteralLocalizationKeysExistInEveryCatalog`.

- [x] U04 - Localization guardrail misses common UI text paths.
  - Evidence: localization tests scan only `localization.text|format`, not
    `imguiLabel`, and do not catch representative raw visible literals.
  - Risk: new localized labels or raw visible strings can bypass catalogs.
  - Owner/source of truth: localization guardrail tests.
  - Fix checklist: scan `text`, `format`, and `imguiLabel`; add focused raw text
    checks/allowlist.
  - Fix: localization key scan now covers `imguiLabel` in addition to
    `text`/`format`, and a shell-runtime string guardrail covers the removed raw
    literals.
  - Verification: `quader_tests`
    `LocalizationResourceTests.UISourceLiteralLocalizationKeysExistInEveryCatalog`
    and `LocalizationResourceTests.ShellRuntimeStringsRouteThroughLocaleKeys`.

- [x] U05 - Main menu has dead duplicate menu logic.
  - Evidence: `ShowMenu` duplicates navigation gizmo toggle behavior implemented
    in `ViewMenu`, but `MainMenuBar::draw*` does not call `showMenu_`; related
    context fields are unused.
  - Risk: future menu edits can update dead code and not affect runtime.
  - Owner/source of truth: `MainMenuBar`.
  - Fix checklist: remove dead `ShowMenu` and unused context fields, or wire it
    intentionally with tests.
  - Fix: removed `ShowMenu`, `showMenu_`, and unused menu context fields; live
    navigation gizmo/menu behavior remains in `ViewMenu`.
  - Verification: `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.UiPanelRoutingUsesStableIdsAndNoDeadShowMenu`.

- [x] U06 - Outliner duplicates document-command hierarchy root policy.
  - Evidence: `selectedRootEntities()` repeats
    `documentCommandSelectedRootEntities()` before group/delete commands.
  - Risk: UI and document-command root filtering can drift for parent/child
    selection behavior.
  - Owner/source of truth: document command/service helper or public scene query.
  - Fix checklist: expose/use the owner helper or remove UI prefiltering and rely
    on command results.
  - Fix: `OutlinerPanel` now calls
    `documentCommandSelectedRootEntities` and no longer keeps a local selected-root
    filtering copy.
  - Verification: `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.OutlinerUsesDocumentCommandRootSelectionOwner`.

- [x] U07 - Inspector owns one-shot multi-transform policy parallel to transform
  services.
  - Evidence: `InspectorPanel` filters locked entities, computes selected
    pivots/bounds, applies rotation/scale around pivot, builds transform edits,
    and calls `scene().setTransforms`.
  - Risk: inspector and gizmo/service transform semantics can diverge for locked,
    parented, and multi-selected entities.
  - Owner/source of truth: scene transform edit service/document command service
    exposed through public scene/editor API.
  - Fix: `SceneTransformDragService` now owns inspector-style one-shot move,
    rotate, and scale commits, including selected-target filtering, selected
    bounds pivot resolution, scale ratio/clamp policy, parent-aware local-pivot
    pinning, and public API batch commit. `InspectorPanel` now only stages UI
    field values and calls that owner.
  - Verification:
    - `cmake --build --preset vs2026-debug --target quader_tests`
    - `quader_tests`
      `DocumentCommandTests.SceneTransformDragServiceInspectorMoveUsesSharedTargetPolicy`,
      `DocumentCommandTests.SceneTransformDragServiceInspectorRotateAndScalePinParentedSelectionPivot`,
      `DocumentCommandTests.SceneTransformDragServiceFiltersTargetsAndRecordsScenePreviewThroughApi`
    - `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
    - `quader_guardrail_tests`
      `OopArchitectureGuardrailTests.SceneTransformDragServiceOwnsTransformPreviewTransactions`

### Modeling, Mesh, Projection

- [x] M01 - Pick-selection apply policy lives in `ModelingSelectionService`.
  - Evidence: `ModelingSelectionService::applyPickSelection` owns preserve-hit
    checks and applies picked snapshot state back into live runtime session.
  - Risk: selection revision, active object, dirty marking, preserve-on-hit, and
    source-wire selection policy can drift from runtime picking owners.
  - Owner/source of truth: runtime selection/picking owner
    (`EditorSession`/`session_picking.cpp`).
  - Fix checklist: add runtime apply-pick request/result, move preserve/live
    application policy there, and keep the service as cache coordinator.
  - Fix: added runtime `ApplyPickSelectionRequest`/`ApplyPickSelectionResult`
    and `quader_editor_core::apply_pick_selection`; `ModelingSelectionService`
    now gathers cached picking inputs and delegates preserve-on-hit/miss,
    multi-click, paint-stroke, live session application, dirty marking, active
    object normalization, and selection revision updates to the runtime owner.
  - Verification: `cmake --build --preset vs2026-debug --target
    quader_tests`; `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=ModelingSessionServiceSplitTests.ClickPickSelectionUsesSelectionServiceCachedInputs:ModelingSessionServiceTests.SelectedHoverAndModifierRemovePreviewStayDistinct:ProgrammaticBehaviorScenarioTests.RayPickingAndContextClickSemanticsPreserveComponentAuthority`;
    `cmake --build --preset vs2026-debug --target
    quader_modeling_domain_runtime_loop_tests`;
    `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe
    --gtest_filter=QdrModelingRuntimePickingTests.*:QdrModelingRuntimeToolSelectTests.*`;
    `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=ModelingSessionServiceTests.CachesSelectionOverlayFrameByRevisionAndHoverState`.

- [x] M02 - Cut and mirror drawn-plane tools duplicate modal lifecycle.
  - Evidence: `drawn_plane_tool.cpp` has near-parallel cut and mirror branches
    for hover, point/line drag, click, validation, drag end, and commit.
  - Risk: pointer ordering, hover claiming, drag release, validation, and
    active-tool sync can diverge between sibling tools.
  - Owner/source of truth: shared `DrawnPlaneTool` lifecycle parameterized by
    tool-specific strategy callbacks.
  - Fix checklist: extract one drawn-plane lifecycle helper and preserve current
    event ordering for cut/mirror modes.
  - Fix: extracted a single `handleDrawnPlaneLifecycle` path in
    `DrawnPlaneTool` with cut/mirror-specific accessors for drag state, points,
    hover, line/point drag, click, commit, and mode cycling.
  - Verification: `cmake --build --preset vs2026-debug --target
    quader_tests`; `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=ViewportToolGuardrailTests.DrawnPlaneToolUsesSharedCutMirrorLifecycle:ViewportToolGuardrailTests.PointToolsUseFocusedContextInsteadOfBroadToolBag:ModelingSessionServiceTests.CutAndMirrorEmitDrawnPlaneOverlay:ModelingSessionServiceTests.CutToolDragEndAndFinalizeSplitMesh:ModelingSessionServiceTests.MirrorToolDrawnPlaneFinalizeDuplicatesSelection`.

- [x] M03 - Host preview-kind mapping is duplicated.
  - Evidence: `modeling_command_tool_catalog.cpp`,
    `modeling_action_catalog.cpp`, and `modeling_tool_catalog_tests.cpp` each map
    `HostPreviewKind`.
  - Risk: new preview kinds can split API/UI/test behavior while mirrored tests
    still pass.
  - Owner/source of truth: shared conversion helper near modeling action/catalog
    metadata owner.
  - Fix checklist: extract helper, replace production switches, and update tests
    to use owner data instead of a mirrored switch.
  - Fix: `ModelingActionCatalog::previewKindFromHost` now owns the
    `HostPreviewKind` to `ModelingActionPreviewKind` conversion. The command
    tool catalog and modeling tool catalog tests consume that owner instead of
    local switches, and the catalog guardrail blocks command/test mirrored
    preview-kind switch cases.
  - Verification: `quader_tests` `ModelingToolCatalogTests.*`;
    `quader_modeling_domain_metadata_loop_tests`
    `QdrEditorMetadataActionCatalogTests.HostContractActionCatalogPreservesOperationPanelVisibilityAndPreviewKinds`.

- [x] M04 - Public tool setting mutation hand-maps schema option names.
  - Evidence: `modeling_operation_settings.cpp` manually maps option IDs/aliases
    into `ModelingInteractionSettings` while schema descriptors live in
    `operation_schema.hpp`.
  - Risk: descriptors, public aliases, UI preference IDs, and settings mutation
    can drift.
  - Owner/source of truth: operation schema/option descriptor owner.
  - Fix checklist: add schema-owned option resolution with compatibility aliases
    and route `applyModelingInteractionSetting` through it.
  - Fix: added `operation_option_for_setting` and
    `operation_option_matches_setting_name` to the schema owner, covering option
    IDs, preference IDs, and compatibility aliases. `applyModelingInteractionSetting`
    now resolves through that owner before applying the setting field.
  - Verification: `quader_tests`
    `ModelingSessionServiceSplitTests.ToolSettingMutationComesFromOperationSettingsOwner`,
    `ModelingSessionServiceSplitTests.InstantOperationSettingsMappingHasSingleOwner`,
    and
    `ModelingSessionServiceSplitTests.HostOperationMetadataUsesSchemaOptionDescriptorCatalog`;
    `quader_modeling_domain_metadata_loop_tests`
    `QdrEditorMetadataSchemaMirrorTests.*` and
    `QdrEditorMetadataPreferenceCatalogTests.*`.

- [x] M05 - `ModelingOverlayService` has an oversized projection boundary.
  - Evidence: `modeling_session_services.cpp` contains drawn-plane frame helpers,
    preview mesh compilation, cut-preview edge/seam extraction, selection overlay
    cache, and frame composition.
  - Risk: tool-specific projection, cache state, and mesh compilation are fused
    into one broad service, making ownership drift likely.
  - Owner/source of truth: focused projection classes such as
    `CutPreviewOverlayProjection`, `DrawnPlaneOverlayProjection`, and
    `ToolPreviewMeshProjection`.
  - Fix checklist: move tool-specific projection helpers behavior-preservingly
    and leave `ModelingOverlayService` as composer/cache coordinator.
  - Fix: extracted `DrawnPlaneOverlayProjection`,
    `CutPreviewOverlayProjection`, and `ToolPreviewMeshProjection`; left
    `ModelingOverlayService` as the cache/composition coordinator for selection,
    drawn-plane, cut-preview, and preview-mesh outputs.
  - Verification: `cmake --build --preset vs2026-debug --target
    quader_tests`; `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=ModelingSessionServiceTests.CachesSelectionOverlayFrameByRevisionAndHoverState:ModelingSessionServiceTests.CutAndMirrorEmitDrawnPlaneOverlay:ModelingSessionServiceTests.CutToolDragEndAndFinalizeSplitMesh:ModelingSessionServiceTests.MirrorToolDrawnPlaneFinalizeDuplicatesSelection`;
    `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=ModelingSessionServiceTests.SupportsRuntimeBoxPreviewLifecycle`.

### Scene, Render, Assets, IO

- [x] R01 - Overlay color policy has a second source of truth in modeling
  runtime defaults.
  - Evidence: render policy owns overlay constants, while
    `editor/modeling/runtime/session.hpp` hard-codes matching or near-matching
    ABGR defaults.
  - Risk: render-side role detection and style resolver policy can drift from
    runtime-authored colors.
  - Owner/source of truth: `src/render/viewport_overlay_policy.*` and
    `ViewportOverlayStyleResolver`.
  - Fix checklist: replace runtime color-default reliance with semantic overlay
    roles and keep user settings applied only in the resolver.
  - Fix: modeling runtime `SelectionOverlayStyle` defaults now emit semantic
    style-role tokens instead of render ABGR colors. `SelectionOverlayProjection`
    translates those tokens to `ViewportOverlayStyleRole` and render-owned
    default colors, leaving user color/size settings in
    `ViewportOverlayStyleResolver`. Stale move-tool runtime assertions now check
    semantic role tokens.
  - Verification: `quader_tests`
    `ViewportOverlayPolicyTests.RuntimeSelectionOverlayDefaultsAreSemanticRoleTokens`,
    `ViewportOverlayStyleResolverTests.AppliesUserSettingsToRuntimeOverlayFrame`,
    and focused `ModelingSessionServiceTests` hover/remove overlay cases;
    `quader_modeling_domain_runtime_loop_tests`
    `QdrModelingRuntimeOverlayTests.*` and `QdrModelingRuntimeToolMoveTests.*`.

- [x] R02 - Asset validation is duplicated between public API and owners.
  - Evidence: `QuaderAssetApi::setMaterialTexture` repeats slot/texture/material
    checks owned by `AssetRegistry::validateMaterial`; material assignment
    prechecks repeat `DocumentCommandService::setMaterial`.
  - Risk: material slot and asset-reference rules can diverge across API,
    commands, and registry.
  - Owner/source of truth: `AssetRegistry` for material validity and
    `DocumentCommandService` for scene asset assignment.
  - Fix checklist: add/use owner-level validation or mutation helpers and remove
    API precheck duplication while preserving public adapter errors.
  - Fix: added `AssetRegistry::setMaterialTexture` with owner-level slot,
    texture, replacement, and unchanged handling; `QuaderAssetApi` now delegates
    material texture edits to the registry and material assignment to
    `DocumentCommandService`.
  - Verification: `cmake --build --preset vs2026-debug --target
    quader_tests`; `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=AssetRegistryTests.MaterialTextureMutationOwnsSlotTextureAndReplacementValidation:AssetRegistryTests.RejectsInvalidMaterialTextureReferencesAtRegistryBoundary:EditorApiTests.PublicAssetMutationsRejectDanglingReferences:MaterialDocumentTests.MaterialTextureSlotPolicyOwnsApiQdrAndRenderSlotNormalization:DocumentCommandServiceTests.AssetAwareServiceRejectsDanglingAssetReferences`.

- [x] R03 - Scene batch reparent simulation duplicates hierarchy invariants and
  transform math.
  - Evidence: `SceneDocument::setParent` owns cycle prevention and
    preserve-world transform math, while `DocumentCommandService::applySceneBatch`
    reimplements simulation, descendant checks, and world/local matrix math.
  - Risk: direct reparent and batch reparent can diverge for hierarchy
    constraints or transform preservation.
  - Owner/source of truth: scene-owned pure hierarchy policy/simulation helper.
  - Fix checklist: extract validation/local-transform calculation and use it from
    both `SceneDocument` and batch command planning.
  - Fix: removed batch-owned descendant and world-matrix reparent simulation;
    scene batches now use `ReparentEntityCommand`, which defers reparent
    validity and preserve-world transform math to `SceneDocument::setParent`
    during redo.
  - Verification: `cmake --build --preset vs2026-debug --target
    quader_tests`; `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=EditorApiTests.SceneBatchUsesPendingStateForRepeatedEntityEdits:EditorApiTests.SceneBatchReparentPreservesWorldTransformAgainstPendingParentTransform:EditorApiTests.SceneBatchReparentCanKeepLocalTransformAgainstPendingParentTransform:DocumentCommandServiceTests.SceneBatchReparentUsesSceneOwnedReparentCommand:DocumentCommandTests.TypedEntityCommandsRoundTripHierarchySelectionAndTransforms`.

- [x] R04 - Render sync repeats extraction/change-detection logic.
  - Evidence: `SceneRenderSyncService::renderAssetRevisionsChanged` repeats
    visible entity, mesh lookup, missing/empty asset, material revision, and
    rebuild-decision logic from `rebuild`.
  - Risk: cache invalidation can drift from actual rebuild behavior.
  - Owner/source of truth: render extraction plan/signature builder under
    `src/render`.
  - Fix checklist: introduce a shared render extraction/signature helper consumed
    by sync and rebuild paths.
  - Fix: added `SceneRenderExtractionPlan` under `src/render` and routed both
    `renderAssetRevisionsChanged` and `rebuild` through the shared extraction
    plan for visible mesh/light entries, material revisions, world transforms,
    and live entity sets.
  - Verification: `cmake --build --preset vs2026-debug --target
    quader_tests`; `build\vs2026\Debug\quader_tests.exe
    --gtest_filter=SceneRenderExtractionTests.BuildsSharedVisibleMeshAndLightPlan:SceneRenderBridgeTests.UsesAssetRevisionsForIncrementalMeshSync:RenderSignatureBuilderTests.CentralizesMaterialMeshAndOverlaySignatures:LightRenderableCacheTests.OwnsIncrementalLightSyncPolicy`.

- [x] R05 - QDR format constants and enum/string conversions are split across
  legacy DTO and project IO.
  - Evidence: format constants live in `qdr_format.hpp`, legacy DTO constants
    duplicate format/payload/min-version values, and reader/writer enum codecs
    are local separate switch pairs.
  - Risk: reader/writer values and version constants can drift.
  - Owner/source of truth: shared QDR format/codec helpers under `src/io/qdr`.
  - Fix checklist: alias legacy constants to shared constants, extract
    bidirectional enum/string codecs, and preserve golden output.
  - Fix: added `kQdrLegacySessionFormatVersion` to the shared QDR format
    header and aliased legacy DTO constants to the shared format constants.
    Added `qdr_project_codecs` for canonical project entity, light, mesh,
    texture, and color-space string codecs, and routed project reader/writer
    through it.
  - Verification: `quader_tests`
    `QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings`,
    `QdrDocumentIoTests.ImportsLegacySessionPayloadButWritesProjectDocumentPayload`,
    `QdrDocumentIoTests.SharedPolygonDocumentCodecPreservesAuthoredNormalShadingAcrossProjectAndLegacyQdr`,
    `QdrDocumentIoTests.QdrPolygonDocumentCodecAndAtomicWriteHaveSingleOwners`,
    and
    `QdrDocumentIoTests.QdrFormatConstantsAndProjectCodecsHaveSingleOwners`;
    `quader_guardrail_tests`
    `OopArchitectureGuardrailTests.EverySourceCppIsPartOfCMakeSourceTruth`.

### Tests, Docs, Build, Dependencies

- [x] T01 - the core logger source had a tracked PascalCase basename while
  CMake registers `src/core/logger.cpp`.
  - Risk: case-sensitive tooling/CI and filename guardrails can fail or drift.
  - Owner/source of truth: CMake/source registration plus filename guardrails.
  - Fix checklist: rename through git as a case-only move and keep CMake spelling.
  - Fix: renamed the tracked file to `src/core/logger.cpp` through git while
    keeping the existing CMake spelling.
  - Verification: `quader_guardrail_tests`
    `BuildGuardrailTests.ProjectOwnedCppFilesUseSnakeCaseCppAndHppNames`.

- [x] T02 - `BehaviorScenario` exposes `ProjectSession&` and public tests use
  `scenario.session().modeling()`.
  - Risk: public behavior tests can bypass `QuaderEditorApi`/scenario DSL, and
    the guardrail misses the `session().modeling()` spelling.
  - Owner/source of truth: `BehaviorScenario` DSL.
  - Fix checklist: add a DSL/helper for viewport shortcut dispatch, migrate
    public tests, and expand `PublicBehaviorTestsUseApiOrScenarioDsl`.
  - Fix: removed public `BehaviorScenario::session()` access, moved internal
    world-position and overlay-frame reads behind private scenario helpers, added
    `BehaviorWhen::dispatchViewportShortcut`, and migrated the public grouping
    shortcut test to the DSL.
  - Verification: `quader_tests`
    `GroupingPublicApiTests.ViewportDeleteShortcutDeletesSelectedGroup` and
    `ProgrammaticBehaviorScenarioTests.*`; `quader_guardrail_tests`
    `ProgrammaticApiGuardrailTests.PublicBehaviorTestsUseApiOrScenarioDsl`.

- [x] T03 - `tools/affected_check.ps1` hard-codes guardrail test membership
  separately from CMake.
  - Risk: a new guardrail source can be registered in CMake but routed as
    `quader_tests`.
  - Owner/source of truth: CMake test target source list or shared manifest.
  - Fix checklist: derive/verify guardrail membership from CMake and add a
    guardrail comparing affected-check routing to target sources.
  - Fix: added `Get-CMakeTargetSourcePaths` to `affected_check.ps1` and routed
    `Test-IsGuardrailTestPath` through the `quader_guardrail_tests` source list
    from `CMakeLists.txt`; removed the duplicated hard-coded guardrail file list.
  - Verification: `powershell -NoProfile -ExecutionPolicy Bypass -File
    .\tools\affected_check.ps1 -PlanOnly -NoConfigure -ChangedPath
    tests/build_guardrail_tests.cpp`; `quader_guardrail_tests`
    `BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns`.

- [x] T04 - Tracy optional dependency lacks local license evidence.
  - Evidence: CMake configures Tracy, manifest points to external checkout
    license instead of `thirdparty/licenses`.
  - Risk: optional dependency evidence policy is bypassed.
  - Owner/source of truth: `thirdparty/` manifest/license evidence.
  - Fix checklist: add Tracy license evidence under `thirdparty/licenses` and
    include it in dependency expectations.
  - Fix: added `thirdparty/licenses/tracy-BSD-3-Clause.txt`, updated the Tracy
    manifest row to point to local evidence, and added Tracy to the dependency
    license expectation table.
  - Verification: `quader_guardrail_tests`
    `DependencyLicenseGuardrailTests.AppThirdPartyManifestHasLicenseEvidence`.

- [x] T05 - Default material texture paths disagree between runtime packaging and
  metadata/tests.
  - Evidence: app/CMake use `materials/default/...`, while host metadata/tests use
    `assets/materials/default/...`.
  - Risk: custom hosts or packaging metadata consume stale paths.
  - Owner/source of truth: app resource packaging contract.
  - Fix checklist: choose one resource-relative path, update metadata/tests, and
    add a guardrail comparing metadata paths to packaging paths.
  - Fix: aligned host metadata and presentation-material tests to
    `materials/default/...` and added a packaging/metadata comparison guardrail.
  - Verification: `quader_guardrail_tests`
    `BuildGuardrailTests.DefaultMaterialMetadataMatchesRuntimePackagePath`;
    `quader_modeling_domain_metadata_loop_tests`
    `QdrEditorMetadataPresentationMaterialTests.HostContractDefaultMaterialRecipeIsSemanticAndEngineNeutral`.

- [x] T06 - Public docs still mention `SDK` internals.
  - Evidence: `docs/api-overview.md` and `examples/lua/README.md` refer to SDK
    internals after the SDK boundary was absorbed.
  - Risk: public docs reintroduce a removed boundary concept.
  - Owner/source of truth: docs/public examples guidance.
  - Fix checklist: replace stale SDK-boundary wording and add a public-doc
    guardrail.
  - Fix: replaced stale SDK-boundary wording in public docs/examples and added a
    public-doc guardrail preventing the absorbed boundary language from
    returning.
  - Verification: `quader_guardrail_tests`
    `BuildGuardrailTests.PublicDocsDoNotExposeStaleSdkBoundaryLanguage`.

- [x] T07 - Positive command assertions still use raw `edit.undo` strings.
  - Evidence: `tests/editor_api_tests.cpp` asserts `edit.undo` and serialized
    `-edit.undo` literals where typed `QuaderCommand` should drive expectations.
  - Risk: typed `QuaderCommand` stops being the test source of truth.
  - Owner/source of truth: `QuaderCommand`.
  - Fix checklist: derive positive expectations from
    `QuaderCommand::EditUndo.string()` while keeping raw strings only for retired
    aliases or external serialized fixtures.
  - Fix: positive command lookup and serialized removal expectations now derive
    from `QuaderCommand::EditUndo.string()`.
  - Verification: `quader_tests`
    `EditorApiTests.AggregatesDocumentApisAndCommandRouting` and
    `EditorApiTests.KeybindingConflictsAndRemovalSupportRebindingUi`.

## Reclassified During This Run

None yet.
