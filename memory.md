# Quader Agent Memory

This file records hard-resolved tasks whose context is easy to lose and costly to rediscover.
Read it after `AGENTS.md` for non-trivial work, but treat current code, tests, AGENTS guidance, and
the user's request as authoritative.

When adding an entry, keep it concise and useful: date, task/context, root cause or invariant,
affected owners/files, resolved approach, and verification. Do not record routine edits, secrets,
private user data, guesses, or unresolved issues. Prefer updating a related existing entry over
adding a duplicate.

## 2026-06-30 - ImGui Combo Icons Must Preserve Popup Stack Ownership

Task/context: replacing native combo/collapsing arrows with Phosphor glyphs regressed the top menu
and left/right sidebar visibility. The custom combo helper had recreated ImGui combo popups by hand,
which could desynchronize global popup/menu state used by `BeginMenu`; the top menu helper also
contained a manual "click an already-open menu to close it" path that could consume menu-bar popup
opens. A later viewport pass-through helper also closed any open popup after the menu bar drew, so
top-level menus could open and then immediately disappear in the same frame.
The rightmost `Layout` menu could also fall past the native borderless chrome's left interactive
strip and be treated as title-bar drag.

Invariant:

- Custom combo drawing may own the frame, preview text, caret glyph, and toggle decision, but popup
  creation must go through ImGui's combo popup owner (`BeginComboPopup`) so callers can keep using
  `ImGui::EndCombo()`.
- Main menu bars must use ImGui's normal `BeginMenu`/`EndMenu` popup lifecycle. Do not add manual
  `IsPopupOpen`/`CloseCurrentPopup` toggle logic to top-level menus unless the behavior is verified
  against real menu-bar clicks.
- Viewport navigation pass-through may auto-close viewport combo popups, but it must be scoped to
  combo popup windows (`##Combo_`) and to clicks inside the viewport pane. It must not close menu
  popups created by the top chrome, sidebars, or other non-viewport surfaces.
- Borderless chrome hit testing must leave the whole top menu row in the left interactive strip. If
  menu labels change, update `PlatformWindowChromeHitTest::leftInteractiveWidth` and the chrome
  hit-test regression so rightmost menu entries such as `Layout` are not draggable title bar.
- Strip small combo height flags for Quader icon combos when the current option lists should auto-fit
  without scrollbars.
- Local panel visibility lives in `%LOCALAPPDATA%\Quader\ui_settings.ini`; if outliner/inspector are
  false, source defaults will not make them appear until settings are restored or changed through UI.

Affected owners/files:

- `src/ui/imgui_quader_widgets.cpp`
- `src/ui/imgui_popup_utils.cpp`
- `src/ui/imgui_popup_utils.hpp`
- `src/ui/main_menu_bar.cpp`
- `src/ui/viewport_panel.cpp`
- `src/platform/platform_window.hpp`
- `tests/core_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`
- local user config `%LOCALAPPDATA%\Quader\ui_settings.ini`

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:UiLayoutGuardrailTests.ViewportChromeCombosSharePopupPassThroughClickHandling:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.AppVersionMetadataUsesRootVersionSource`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Vertex Snap Must Not Steal Plain Gizmo-Overlapped Selection Clicks

Task/context: with vertex snap enabled, clicking a vertex that was overlapped by the move gizmo could
start transform-drag preview on mouse-down and show the snap target indicator instead of behaving like
a normal component selection click.

Invariant:

- Transform gizmo press is only a pending grab until the pointer crosses the shared selection-box drag
  threshold. Release before that threshold must route back through viewport selection fallback.
- Vertex snap target resolution belongs to active transform-drag update frames only. The press/begin
  preview must use a zero snap radius so snap indicators cannot appear on ordinary clicks.
- Selection fallback owns the actual pick; gizmo code only reports a deferred click through
  `ViewportBeforeToolInteractionResult`.

Affected owners/files:

- `src/editor/viewport/transform_gizmo_interaction.*`
- `src/editor/viewport/viewport_selection_interaction.*`
- `src/editor/viewport/viewport_interaction_frame.hpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.VertexSnapDoesNotStealPendingMoveGizmoVertexClick:ViewportInteractionControllerTests.MoveGizmoComponentAxisDragKeepsPickedAxisUnderMouse:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames:ViewportInteractionControllerTests.RotateGizmoDragEmitsPieAngleIndicator`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.*`
- `build\vs2026\Debug\quader_tests.exe`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Component Move Gizmo Axis Drags Must Use Axis-Ray Distance

Task/context: Godot-style gizmo visual refactoring exposed bad move-drag behavior in vertex/edge/face
component modes: Z-axis drags could visibly drift away from the mouse in perspective, and axis/plane
hit overlap made it too easy for a visually axis-like click to use another constraint.

Root cause/invariant:

- Single-axis move drags must derive distance from the current mouse ray projected against the picked
  world axis, relative to the press-time axis distance. Pixel-to-world fallback is only for synthetic
  callers whose rays do not change with screen position.
- Vertex snap targets on a constrained axis must project onto `state.world_axis`, not recompute by
  enum component, so visual/world direction stays authoritative.
- Transform gizmo picking must give visible axes priority before plane handles when the cursor lies on
  an axis shaft or tip.

Affected owners/files:

- `src/editor/interactions/move_tool.*`
- `src/editor/modeling/runtime/transform_gizmo.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.MoveGizmoComponentAxisDragKeepsPickedAxisUnderMouse`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=*TransformDrag*:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.*:ViewportTransformGizmoOverlayBuilderTests.*`

## 2026-06-29 - Undo Must Not Be Swallowed By Idle Modal Tools

Task/context: after moving entities with the viewport transform gizmo, Ctrl+Z could appear to do
nothing because undo was allowed to route through the broad "cancel active tool or preview" path.

Root cause/invariant:

- `QuaderApiMutationRunner` owns editor-history undo routing.
- Ctrl+Z may cancel live operation previews and transform drags because those mutate temporary
  editor state before a document history item exists.
- Idle modal tools such as Box/Cut/Mirror owning the viewport must not consume document undo. Escape
  and the explicit cancel command own modal-tool cancellation.
- If a pre-undo cancel attempt reports `changed=false`, the undo route must fall through to the
  shared document history stack when one is available.

Affected owners/files:

- `src/api/quader_api_mutation_runner.cpp`
- `src/editor/modeling/services/modeling_session_service.*`
- `tests/editor_api_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Resolved approach:

- Added `ModelingSessionService::canCancelActivePreviewForUndo()` for the narrower preview/drag
  predicate used by history undo.
- `QuaderApiMutationRunner::undo()` now uses that predicate and falls through to document history
  when cancel returns unchanged.
- Added regression coverage for idle modal tool + document undo, and for viewport gizmo drags on
  both mesh objects and point lights restoring through `HistoryUndo`.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.HistoryUndoUsesDocumentHistoryWhenOnlyIdleModalToolIsActive:EditorApiTests.HistoryUndoCancelsActiveBevelPreviewBeforeDocumentHistory:EditorApiTests.MixedUndoRedoPreviewStackPreservesTexturedMaterial:EditorApiTests.PublicHistoryUndoRedoMatchesCommandApiStatus:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames:ModelingSessionServiceTests.PublicTransformDragObjectSelectionRecordsSceneTransformHistory`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - Scene Settings Fields Must Stay Undoable

Task/context: scene-root Environment/IBL, Camera, and Post Process settings are edited from the
inspector and must each participate in normal undo/redo.

Root cause/invariant:

- Scene-global render settings live on the real scene root as one `SceneSettingsComponent`.
- UI edits must copy current settings, change the intended field, and route through
  `context.editor.scene().setSceneSettings(...)`; UI must not call `SceneDocument::setSceneSettings`
  directly.
- `SetSceneSettingsCommand` must compare and restore the full settings struct, so adding a new
  environment, camera, or post-process field requires updating API conversion, equality, QDR IO, and
  `EditorApiTests.SceneSettingsPropertiesUndoAndRedoIndividually`.

Affected owners/files:

- `src/scene/components.hpp`
- `src/api/quader_api_type_conversions.hpp`
- `src/editor/commands/document/set_scene_settings_command.hpp`
- `src/ui/inspector_panel.cpp`
- `tests/editor_api_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.SceneSettingsPropertiesUndoAndRedoIndividually`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorSceneSettingsEditThroughPublicSceneApi:UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi`

## 2026-06-29 - Undo/Redo Shortcuts Are Editor-Global

Task/context: Ctrl+Z/Ctrl+Y worked only after focusing the viewport. Inspector/property edits were
undoable through history, but keyboard dispatch never reached the history command while a panel
owned focus.

Root cause/invariant:

- `QuaderCommandsApi` owns default shortcut contexts. Editor-global command shortcuts must use a
  global `!textInputActive` context, not `viewportFocus`.
- Viewport-only commands and router shortcuts still require `viewportFocus && !textInputActive &&
  !navigation.active`.
- `UISystem` owns top-level shell input dispatch. When a non-viewport panel owns focus, it must
  route pressed chords through the command API and execute only command matches. Do not hard-code
  Ctrl+Z/Ctrl+Y in UI code.
- Active text input must keep blocking editor-global shortcut dispatch so text widgets keep normal
  editing behavior.

Affected owners/files:

- `src/api/quader_commands_api.cpp`
- `src/ui/ui_context.hpp`
- `src/ui/ui_system.cpp`
- `src/ui/viewport_panel.cpp`
- `src/ui/viewport_input_frame_builder.*`
- `tests/editor_api_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.GlobalCommandKeybindingsResolveWithoutViewportFocus:EditorApiTests.GlobalCommandKeybindingsDoNotStealTextInputOrViewportOnlyShortcuts:EditorApiTests.ViewportKeybindingsResolveContextAndUserOverrides:EditorApiTests.InputBindingsSupportPunctuationAndMouseChords:EditorApiTests.ViewportHostShortcutCatalogDrivesCommandKeybindings:EditorApiTests.DefaultKeybindingProjectionDoesNotDuplicateRuleTuples`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - Continuous UI Edits Are One Undo Gesture

Task/context: dragging inspector values such as IBL intensity created one undo command per ImGui
change frame, so Ctrl+Z walked intermediate values instead of restoring the pre-drag state.

Root cause/invariant:

- Public API/document commands intentionally record one history item per call unless an owner
  explicitly opts into coalescing. Do not add blind history merging that collapses separate scripts,
  commands, or user intents.
- Continuous inspector controls (`Drag*`, `Slider*`, `ColorEdit*`, live text-style inputs) must apply
  live changes through the authoritative public API path and pass one stable merge key for the
  active gesture. History keeps the first `before` value and replaces the final `after` value, so one
  undo restores the pre-drag state without delaying viewport/render updates.
- `IsItemDeactivatedAfterEdit()` is a lifecycle/reset/final-value signal for the UI helper; it must
  not be the only moment when document/render state changes.
- Discrete controls such as checkboxes, combos, buttons, and menu items can commit immediately.
- Merge keys are gesture-scoped and are stored with undo stack entries. Commands of the same
  type/entity must not merge when the previous stack entry was created with a different key.
- Query history snapshots must use the same public history availability owner as keyboard undo, so
  active previews that Ctrl+Z can cancel are reported as undoable.

Affected owners/files:

- `src/ui/ui_continuous_edit_tracker.hpp`
- `src/ui/inspector_panel.*`
- `src/core/i_undoable_command.hpp`
- `src/core/undo_redo_execute_options.hpp`
- `src/core/undo_redo_manager.*`
- `src/editor/history/editor_history_service.*`
- `src/api/quader_commands_api.cpp`
- `src/api/quader_query_api.*`
- `tests/core_tests.cpp`
- `tests/editor_api_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Resolved approach:

- Added `UIContinuousEditTracker` and converted scene-root Environment/Camera/Post Process drags and
  light color/intensity/range controls to apply live via `setSceneSettings` or `setLightComponent`
  while passing the active gesture merge key.
- Added opt-in history coalescing with `UndoRedoExecuteOptions::mergeKey`; transform, light, and
  scene-settings before/after commands can merge only with the same command type/entity when the
  previous stack entry and the new call use the same non-empty key.
- Default undo/redo keybinding contexts now include `canUndo`/`canRedo`, including host-provided
  `Ctrl+Shift+Z`, so disabled shortcuts do not dispatch failed commands.
- `QuaderQueryApi::history()` now uses `QuaderApiMutationRunner::canUndo()` for public undo
  availability.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=DocumentCommandTests.UndoRedoManagerCoalescesOnlyExplicitMergeKeys:DocumentCommandTests.EditorTransactionsCoalesceExplicitMergeKeysBeforeCommit:DocumentCommandTests.SceneBeforeAfterCommandsSupportExplicitMergeKeys:EditorApiTests.ScenePointLightLiveEditsApplyImmediatelyAndCoalesceByMergeKey:EditorApiTests.SceneSettingsLiveEditsApplyImmediatelyAndCoalesceByMergeKey:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneSettingsPropertiesUndoAndRedoIndividually`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorContinuousInputsLiveApplyWithMergeKeys:UiLayoutGuardrailTests.InspectorSceneSettingsEditThroughPublicSceneApi:UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi:ProgrammaticApiGuardrailTests.EditorHistoryBoundaryHidesCoreUndoRedoManager:ProgrammaticApiGuardrailTests.PublicHistoryDoesNotFallbackToModelingHistory`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.DefaultKeybindingProjectionDoesNotDuplicateRuleTuples:EditorApiTests.PublicHistoryUndoRedoMatchesCommandApiStatus:EditorApiTests.HistoryUndoUsesDocumentHistoryWhenOnlyIdleModalToolIsActive`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - Point Lights Are Scene Entities With Viewport Gizmos

Task/context: the first Filament point-light implementation rendered a selected light in the
inspector, but W/R/S activation could clear the scene-only selection, the transform gizmo did not
appear, and the selected light had no viewport marker. User-facing intensity also needed values in
the thousands to affect the viewport.

Root cause/invariants:

- Point lights are scene entities, not modeling runtime objects. Selection/query/gizmo paths must
  route through the scene-aware editor selection service for scene-only entities while preserving
  component selection summaries for modeling mesh selections.
- Activating transform tools must not sync away scene-only transform selections. Preserve selected
  transformable scene entities through runtime tool activation when the modeling runtime has no
  active selection.
- Transform drag begin must accept scene-only transform selections and use scene bounds when the
  modeling runtime has no selection bounds.
- Selected point lights need an editor overlay marker even when they have no mesh renderable.
- User-facing punctual light intensity is Godot-style energy; viewport Filament lumens use the
  Godot base conversion plus a viewport exposure compensation factor until engine-specific export
  mappings are introduced.

Affected owners/files:

- `src/editor/selection/editor_selection_service.cpp`
- `src/api/quader_query_api.cpp`
- `src/editor/modeling/services/modeling_session_service.cpp`
- `src/render/overlay/viewport_scene_overlay_builder.cpp`
- `src/render/light_renderable_cache.cpp`
- `tests/editor_api_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`
- `tests/render_extraction_tests.cpp`

Resolved approach:

- `QuaderQueryApi` now asks `ProjectSession::selection()` for summaries/centers, and the editor
  selection service reports `Light` only for scene-only selections so component selections keep
  their modeling summaries.
- Runtime tool activation preserves scene-only transform selections; transform drag begin routes
  scene-only drags as object-mode scene transform previews and falls back to selected scene bounds.
- The scene overlay builder adds a selected point-light marker and small axis ticks at the light
  transform origin.
- Point/spot intensity `1.0` maps to `10,000,000` Filament lumens in the viewport:
  `1000` Godot-equivalent lumens times `10000` viewport exposure compensation.

Verification:

- `cmake --build --preset vs2026-debug --target quader quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.ScenePointLightSelectionFeedsQuerySummaryAndCenterInComponentMode:ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames:ViewportSceneOverlayBuilderTests.AddsSelectedLightMarker:LightRenderableCacheTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportInteractionControllerTests.MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames:ViewportSceneOverlayBuilderTests.*:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportToolGuardrailTests.TransformGizmoEligibilityLivesInViewportToolMetadata:ModelingSessionServiceSplitTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`

## 2026-06-30 - Transform Gizmo Visuals Follow Godot Proportions

Task/context: Quader transform gizmo visuals looked heavier and less like Godot's editor gizmos.
The reference source was `C:\Users\Drako\Desktop\_QUADER_DEV\resources\_SOURCE_CODE\godot-master`.

Root cause/invariants:

- Runtime gizmo pick geometry and render overlay geometry must share the same proportions, otherwise
  visual handles drift from interactive handles.
- Godot-style constants are the source of truth for visual proportions: circle size `1.1`, plane
  size `0.2`, plane distance `0.3`, arrow offset `1.4`, arrow tip `1.75`, scale tip `1.554`,
  view rotation scale `1.14`, and viewport gizmo size `80` pixels.
- Plane handle colors follow the omitted normal axis, not the visible two-axis label: XY is Z/blue,
  YZ is X/red, XZ is Y/green.
- Active rotate drags must carry a rotate angle indicator through `TransformGizmoFrame`: the
  overlay draws the restored pie/wedge fill, the radial line from the center to the original grab
  direction, and the Godot-style center-to-current-mouse line.

Affected owners/files:

- `src/editor/modeling/runtime/transform_gizmo.cpp`
- `src/render/overlay/viewport_transform_gizmo_overlay_builder.cpp`
- `src/render/viewport_overlay_policy.hpp`
- `tests/modeling_domain/tool/qdr_modeling_runtime_tool_transform_gizmo_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Resolved approach:

- Updated runtime gizmo frame generation to use Godot-style pixel proportions for move, scale,
  plane, and rotate primitives.
- Reworked overlay rendering to use slim arrow cones, square-prism scale handles, full rotate rings
  for the neutral trackball/view ring, front-only colored rotate axis arcs, Godot axis colors,
  normal-axis plane colors, and no move center dot or black axis underpaint.
- Restored the previous rotate drag pie indicator as overlay geometry owned by the transform gizmo
  frame/builder instead of UI-only immediate drawing.
- Added guardrail coverage for the Godot-style dimensions, colors, and emitted overlay primitives.

Verification:

- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntime*:QdrModelingRuntimeToolTransformGizmoTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=GroundGridMaterialTests.*:LightRenderableCacheTests.*:ModelingSessionService*Hover*:ModelingSessionService*Overlay*:ModelingSessionService*Pick*:Overlay*:QuaderMeshMaterialTests.*:Render*:RenderExtractionTests.*:SceneRenderBridge*:TextureRegistryTests.*:Viewport*:ViewportBoxOverlayBuilderTests.*:ViewportInteractionControllerTests.*:ViewportSelectionMarqueeOverlayBuilderTests.*:ViewportTransformGizmoOverlayBuilderTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime`
- `build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadata*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-29 - ImGui Viewport Layouts Require MultiPane Public Workspace

Task/context: the ImGui shell layout menu and viewport panel still contained the restored multi-pane
draw path, but choosing any layout other than Single stayed single-pane.

Root cause/invariant:

- The earlier ImGui path owned `ViewportPanel::setViewportLayout()` directly and rendered
  placements from `ViewportLayoutState`.
- After `ViewportWorkspaceService` became the viewport owner, `QuaderViewportApi` was left
  constructing its workspace with `ViewportWorkspaceLayoutPolicy::SinglePaneOnly`, a policy from the
  temporary single-pane host. The current ImGui menu routes through
  `editor.viewports().setLayout(...)`, so the API rejected multi-pane before `ViewportPanel` could
  draw the restored placements.
- Keep `SinglePaneOnly` available only for hosts that actually expose one embedded viewport surface;
  the active native ImGui editor API workspace must be `MultiPane`.

Affected owners/files:

- `src/api/quader_viewport_api.cpp`
- `src/viewport/viewport_workspace_service.hpp`
- `src/ui/main_menu_bar.cpp`
- `src/ui/viewport_panel.cpp`
- `tests/editor_api_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Changed the public/editor viewport API workspace default back to `ViewportWorkspaceLayoutPolicy::MultiPane`.
- Kept layout state in `ViewportWorkspaceService`; `MainMenuBar` continues to call the public API,
  and `ViewportPanel` continues to consume `activePanePlacements()` and `activeSplitHandles()` from
  the workspace.
- Updated the public API viewport test expectation from rejected FourQuad/single-pane to accepted
  FourQuad/four active panes, and added a focused build guardrail for the active ImGui layout route.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiViewportApiKeepsMultiPaneLayoutWorkspace`
- Older affected `quader_tests` verification was previously blocked by stale test sources; the
  native UI cleanup removed those stale backend references.

## 2026-06-29 - Win32 Resize State And Window Placement Persistence

Task/context: nightly window slice B tightened borderless Win32 resize behavior and added persisted
native window placement.

Root cause/invariants:

- Borderless Win32 resize hit testing must use the current DPI-scaled system frame and padded-border
  metrics, not only the fixed fallback hit band.
- Native resize state must clear on normal exit and on cancellation paths such as `WM_CANCELMODE`,
  `WM_CAPTURECHANGED`, focus loss, and destruction; otherwise viewport resize suspension can stay
  active after Windows aborts the modal resize.
- Persist the normal window bounds separately from maximized state. When restoring, recover the
  normal frame into a visible display work area before applying maximize.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app.*`
- `src/app/app_window_settings.*`
- `src/core/user_config_paths.*`
- `src/ui/ui_settings.cpp`
- `tests/build_guardrail_tests.cpp`
- `tests/core_tests.cpp`

Resolved approach:

- Added `PlatformWindowPlacement`, native `GetWindowPlacement`/`SetWindowPlacement` use on Win32,
  display-work-area recovery, DPI/system-metric resize borders, duplicate live-resize frame
  coalescing, and forced final redraws on resize end/cancel/maximize/restore.
- Added app-owned `window_settings.ini` serialization for normal bounds and maximized state, while
  moving the shared product config directory lookup into `quaderUserConfigDirectory`.
- Added focused guardrail coverage plus behavior tests for settings round-trip, resize border
  metrics, and offscreen recovery.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- Older `quader_tests` verification was previously blocked by stale test sources; the native UI
  cleanup removed those stale backend references.

## 2026-06-28 - ImGui Borderless Resize Must Redraw During Native Live Resize

Task/context: the `feature/imgui-shell` branch restored custom borderless chrome with ImGui. Window
resize had two regressions: growing from a small window lagged or snapped on mouse-up, while
shrinking later clipped the UI until mouse-up.

Root cause/invariants:

- On Windows, native move/resize runs a modal loop where normal `SDL_PollEvent()` rendering can
  stall. Growing needs redraws for newly exposed pixels; shrinking needs redraws from native size
  messages even when no expose event is generated.
- Do not drive live borderless resize with per-motion `SDL_SetWindowPosition` plus
  `SDL_SetWindowSize` on Windows. Use native hit testing (`WM_NCHITTEST`) so Windows owns resize
  geometry, and keep the SDL size path only as fallback.
- `SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION)` is wrong for this path: it drops live resize input and
  couples resize progress to render cadence.
- During live resize, avoid exact viewport render-target reallocation on every pixel; keep drawing
  the current viewport texture and exact-resize again when the resize loop ends.

Affected owners/files:

- `src/app/app.cpp`
- `src/app/app.hpp`
- `src/platform/platform_window.cpp`
- `src/platform/platform_window.hpp`
- `src/ui/ui_context.hpp`
- `src/ui/ui_system.cpp`
- `src/ui/ui_system.hpp`
- `src/ui/viewport_panel.cpp`

Resolved approach:

- Added one guarded `App::renderFrame()` path used by both the normal loop and live resize redraws.
- Added `SDL_AddEventWatch` handling for `SDL_EVENT_WINDOW_EXPOSED` with `data1 == 1` so growing
  windows repaint during SDL/Windows live resize.
- Enabled Win32 `WM_NCHITTEST` for resize edges and the draggable titlebar (`HT*` and `HTCAPTION`)
  so native Windows resize owns live geometry.
- Added a platform live-resize callback from `WM_SIZE`, `WM_WINDOWPOSCHANGED`, and
  `WM_EXITSIZEMOVE` so shrinking windows repaint during the native modal resize loop.
- Added `PlatformWindow::chromeResizeActive()` and UI render-resize suspension so viewport render
  targets are not reallocated every pixel while the chrome resize is active.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- Manual smoke in `build\vs2026\Debug\quader.exe`: grow from small window and shrink from large
  window both repaint during drag instead of lagging, clipping, or snapping only on mouse-up.

## 2026-06-28 - ImGui Shell Flush Viewport, Visible Checkboxes, And Work-Area Maximize

Task/context: the ImGui shell had visible gutters around docked viewport/panels, clipped viewport
combo labels, invisible unchecked checkboxes, mismatched checked checkbox colors, and a black strip
near the Windows 11 taskbar when maximized.

Root cause/invariants:

- The ImGui dockspace host must push zero `WindowPadding` before `DockSpace`; otherwise the dock node
  inherits the global 8px window padding and leaves viewport/panel gutters, including a bottom strip.
- Viewport chrome combo widths must be measured from localized labels with the active frame padding,
  not fixed constants such as 112/104px.
- Global ImGui checkbox/input frames need a visible 1px border and a frame background distinct from
  the panel background; checked ticks should be neutral/light on a coherent dark frame, not a purple
  tick on a blue/purple active frame.
- Borderless Win32 maximize must handle `WM_GETMINMAXINFO` and `WM_NCCALCSIZE`: the maximize bounds
  come from the monitor `rcWork`, and the maximized client area must be computed during non-client
  sizing so drawable content never extends behind the taskbar.
- Do not fix borderless maximize by calling `SetWindowPos` after `ShowWindow(SW_MAXIMIZE)` or from
  `WM_WINDOWPOSCHANGED`; it fights the native maximize transaction, can flicker, and can fail on
  later maximize/restore cycles.
- A fixed shell bottom bar must be a separate viewport-aligned ImGui window and the dockspace host
  height must reserve its height. Do not draw it as content inside the dockspace host with `Dummy`,
  because that creates whole-app scrolling and gaps between panels and the bar.

Affected owners/files:

- `src/ui/ui_system.cpp`
- `src/ui/viewport_panel.cpp`
- `src/ui/ui_style_tokens.cpp`
- `src/platform/platform_window.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added local zero-padding style scope around the main ImGui dockspace host.
- Replaced fixed viewport combo widths with measured max widths across localized view/shading labels.
- Set `FrameBorderSize = 1`, raised frame border/background contrast, and changed checkmarks to a
  near-white color.
- Added Win32 `WM_GETMINMAXINFO` handling for borderless chrome maximized work-area sizing.
- Added Win32 `WM_NCCALCSIZE` handling, matching the Electron/Chromium pattern, so the maximized
  frameless client area is the monitor work area instead of a later-corrected outer frame.
- Added a fixed static ImGui bottom bar for visual maximize/taskbar verification, with the dockspace
  host reserving the bar height and disabling host scrolling.
- Added focused guardrails for the active ImGui shell/style invariants and work-area maximize path.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:WindowGuardrailTests.BorderlessMaximizeUsesMonitorWorkArea`
- Native Win32 smoke: launch `build\vs2026\Debug\quader.exe --borderless-window`, run four
  `ShowWindow(SW_MAXIMIZE)` / `ShowWindow(SW_RESTORE)` cycles, and verify the screen-space client
  rect stays inside `MONITORINFO.rcWork` on every maximize.
- Screenshot/pixel smoke: maximized client rect equals `MONITORINFO.rcWork`, the bottom bar accent
  is visible at the client bottom, and no whole-app scrollbar or gap appears above the bar.

## 2026-06-28 - Undo Cancels Active Tool Previews Before Document History

Task/context: Ctrl+Z during an active modeling preview could consume document history first, making
the object disappear; finalized preview operations such as Slice Quad could undo only to the
post-preview selected faces instead of the pre-operation mesh and selection.

Root cause/invariants:

- Public undo is a command/history intent, but the API mutation boundary must first cancel active
  runtime ownership: operation previews, transform previews/drags, scene transform previews, and
  modal viewport tools. Command registration should still ask the history API for availability.
- Preview commit history must capture the preview transaction base state as the undo "before"
  memento. Capturing after the preview has already mutated the live session records the wrong
  boundary and makes undo restore the committed preview state.
- Texture/material identity must survive preview cancel, finalized topology undo/redo, and
  delete/restore stacks.

Affected owners/files:

- `src/api/quader_api_mutation_runner.*`
- `src/api/quader_history_api.cpp`
- `src/api/quader_commands_api.cpp`
- `src/editor/history/modeling_memento_command.hpp`
- `src/editor/history/modeling_preview_command_service.hpp`
- `src/editor/modeling/runtime/session.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `tests/editor_api_tests.cpp`

Resolved approach:

- Added active-preview/tool cancellation to `QuaderApiMutationRunner::undo()` and surfaced the same
  decision through `QuaderApiMutationRunner::canUndo()` so Ctrl+Z is enabled while a runtime cancel
  is possible without adding split modeling-history routes in command registration.
- Exposed the active preview transaction history state through the modeling session service and let
  preview/transform commit commands override their before-memento capture with that base state.
- Added public query accessors for material and texture assets so behavior tests can assert texture
  preservation through history stacks.
- Added focused editor API regressions for bevel-preview undo cancel, Slice Quad finalized undo, and
  a mixed undo/redo stack covering texture assignment, preview cancel, slice undo/redo, delete,
  redo, and restore.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.HistoryUndoCancelsActiveBevelPreviewBeforeDocumentHistory:EditorApiTests.HistoryUndoAfterSliceQuadFinalizeRestoresPreOperationMeshAndSelection:EditorApiTests.MixedUndoRedoPreviewStackPreservesTexturedMaterial`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=*:ProgrammaticApiGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*:EditorApiTests.*:EditorApiTests.*History*:EditorApiTests.*Modeling*:EditorApiTests.*Selection*:GroupingPublicApiTests.*:ModelingSessionService*:ModelingToolCatalogTests.*:ProgrammaticApiTests.*:ProgrammaticBehaviorScenarioTests.*:SelectionModeAdapterTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=*`
- `build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadata*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntime*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Modeling Tests Are GoogleTest-Only

Task/context: the modeling-domain test harness was migrated permanently to GoogleTest and the old
test framework was removed as a CMake and third-party dependency.

Root cause/invariant:

- Native tests must use GoogleTest only. Do not add parallel test frameworks or fallback source-fetch
  policy for modeling-domain tests.
- App/runtime behavior tests live in `quader_tests`, source-policy scans live in
  `quader_guardrail_tests`, and modeling-domain tests are one aggregate executable,
  `quader_modeling_domain_tests`, registered through explicit categorized source lists in
  `cmake/QuaderModeling.cmake`.
- Project-owned files must not reintroduce retired test-framework include paths, macros, dependency
  options, source-policy docs, or license evidence.

Affected owners/files:

- `cmake/QuaderBuildOptions.cmake`
- `cmake/QuaderModeling.cmake`
- `tests/modeling_domain/**`
- `tests/build_guardrail_tests.cpp`
- `agents/testing-build.md`
- `agents/dependencies.md`
- `docs/absorbed_modeling/README.md`
- `thirdparty/THIRD_PARTY_MANIFEST.md`

Resolved approach:

- Removed the old dependency option, commit pin, source-dir cache variable, FetchContent path, and
  test-support compile definition.
- Rewired `quader_modeling_domain_tests` to `GTest::gtest` and `GTest::gtest_main`.
- Converted modeling-domain test sources to GoogleTest syntax and moved shared approximate
  comparison support into `tests/modeling_domain/test_support.hpp`.
- Removed old third-party manifest/license entries and added guardrails that scan project-owned
  files for retired test-framework tokens.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:DependencyLicenseGuardrailTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Source-Wire Overlays, Flipped Mesh Picking, And Inverted Face Fill

Task/context: source-wire and selection overlays had a long-running set of regressions: large wire
gaps, fading or missing edges at distance/angles, hidden internal source-wire edges on inverted
meshes, hover/deselect colors failing for inverted internal edges, occluded internal handles being
pickable from outside, and face-fill hover/selection not appearing on inverted faces.

Root cause/invariants:

- Source-wire topology is editor interaction geometry and should render as crisp draw-on-top lines.
  `SourceWireDepthStamp` is metadata only and must not render or delete source-wire line topology.
- Picking and rendering are separate concerns. Render extraction should not CPU-filter source-wire
  edges to solve pickability; source-wire interaction is accepted or rejected by picking/hover
  occlusion policy.
- Editor picking needs nearest visible two-sided surface behavior for flipped/backfacing meshes:
  backfaces are pickable, but authored front-facing faces must not beat nearer visible backfaces.
- Selected/hover face-fill overlays need two-sided triangle render state so inverted faces show fill;
  duplicating reversed geometry is not the owner fix.

Affected owners/files:

- `src/editor/modeling/runtime/session_picking.cpp`
- `src/mesh/polygon/document.hpp`
- `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- `src/editor/projection/picking_cache_projection.cpp`
- `src/editor/modeling/runtime/session_selection_overlay.cpp`
- `src/render/viewport_overlay_policy.cpp`
- `src/render/overlay/overlay_renderable_builder.cpp`
- `src/render/overlay/overlay_renderable_builder.hpp`
- `src/render/overlay/overlay_face_fill_builder.cpp`
- `tests/modeling_domain/qdr_modeling_runtime_picking_tests.cpp`
- `tests/modeling_domain/qdr_poly_geometry_characterization_tests.cpp`
- `tests/render_extraction_tests.cpp`
- `tests/core_tests.cpp`
- `docs/overlay-handling.md`
- `agents/rendering-overlays.md`

Resolved approach:

- Kept `SourceWire` draw-on-top and kept `SourceWireDepthStamp` non-rendered.
- Added `PickOptions::prefer_front_facing_faces`, preserving the old low-level default while opting
  editor picking into nearest-surface behavior.
- Included the new pick option in face-geometry cache signatures.
- Routed editor hover/pick paths through one shared editor surface pick policy.
- Added modeling-domain tests for outside-vs-inside picking on flipped geometry and source-wire
  occlusion.
- Made selected and hover face-fill overlay layers two-sided in `ViewportOverlayPolicy`, preserving
  the existing depth-stamped two-pass fill pipeline.
- Added render extraction and policy tests asserting selected/hover face fills use no triangle
  culling and two-sided transparency.
- Added `docs/overlay-handling.md` and explicit overlay status tests for the final object-mode
  x-ray, component-mode source-wire, source-wire depth-stamp, inside-out visibility, and
  outside-vs-inside picking contracts.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-27 - Project-Owned C++ File Naming Invariant

Task/context: the native app source tree had mixed PascalCase and snake_case C++ filenames, plus
project headers using mixed extensions. The repository now standardizes project-owned C++ filenames
so future refactors, CMake entries, includes, and tests have one naming convention.

Invariant:

- Project-owned C++ files under `src`, `tests`, `examples`, `benchmarks`, and `tools` use
  snake_case basenames only.
- Project-owned C++ source/header extensions are only `.cpp` and `.hpp`.
- Third-party/vendor headers keep their upstream names and extensions.

Affected guardrails:

- `tests/build_guardrail_tests.cpp` enforces snake_case `.cpp`/`.hpp` project filenames, rejects
  stale PascalCase or old-extension project path references, rejects broken extension rewrite
  artifacts, and rejects temporary rename files.
- `tests/support/guardrail_file_scan.cpp`, `tests/programmatic_api_guardrail_tests.cpp`, and
  `tests/oop_architecture_guardrail_tests.cpp` scan `.hpp` project headers instead of old header
  extensions.

Resolved approach:

- Renamed project-owned C++ files to snake_case and changed project headers to `.hpp`.
- Updated CMake, docs, examples, source includes, and tests to the new paths.
- Repaired mechanical rewrite fallout in member names such as `history_status`, `has_selection`,
  `has_projection`, and `hard_normal_edges`.
- Restored a mechanically rewritten external vendor include path during the header-extension rename.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `cmake --build --preset vs2026-debug --target quader`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:OopArchitectureGuardrailTests.*:ProgrammaticApiGuardrailTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Affected Build/Test Workflow

Task/context: prototype loops were paying the full app/test cost for narrow source changes. The
fast path now uses an affected workflow before full verification, while still printing a concrete
before -> after timing when requested.

Invariant:

- Use `tools/affected_check.ps1` for scoped build/test checks during prototyping and agent loops.
- Pass explicit comma-separated `-ChangedPath` values in dirty worktrees so the plan reflects the
  intended edit, not unrelated local changes.
- Route pure source-policy scans through `quader_guardrail_tests`; do not force `quader_tests` to
  compile/link for docs, tooling, CMake policy, or API/source-boundary guardrails.
- Modeling-domain loop targets are developer-only `EXCLUDE_FROM_ALL` executables used by the
  affected workflow. Do not register them with CTest; full verification stays on the aggregate
  `quader_modeling_domain_tests` entry.
- Use `-MeasureFull` when a task needs a concrete before -> after comparison.
- Use `-IncludeApp` when manual in-app verification or runtime resource copying is part of the
  change.

Affected owners/files:

- `tools/affected_check.ps1`
- `CMakeLists.txt`
- `cmake/QuaderModeling.cmake`
- `agents/testing-build.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added path-based target and GoogleTest filter planning for render, UI, viewport, modeling,
  polygon/geometry, QDR, resources, docs, tools, CMake, and direct test-source edits.
- Split pure source-policy scans into one lightweight `quader_guardrail_tests` executable so docs
  and tooling changes do not pay the app/runtime test link cost.
- Kept the workflow in the existing `build/vs2026` tree and added one lightweight guardrail
  executable plus three modeling-domain loop executables instead of per-test artifacts or extra
  artifact folders.
- Split modeling-domain test sources into shared category object buckets and assembled developer
  loops for polygon/document, runtime/QDR runtime, and editor metadata checks while preserving the
  aggregate `quader_modeling_domain_tests` CTest target.
- Added a guardrail so the tool keeps printing the full baseline, affected workflow, filtered
  GoogleTest commands, and before -> after comparison support.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked -ChangedPath src/render/overlay/overlay_renderable_builder.cpp -MeasureFull`
- Render-overlay sample timing: full baseline 51.36s -> affected workflow 4.22s, 47.14s saved.
- Tooling-only sample after the guardrail split and filter tightening: full baseline 54.03s ->
  affected workflow 2.43s, 51.60s saved.
- Runtime modeling test-source sample after loop targets:
  `tests/modeling_domain/operation/qdr_modeling_runtime_operation_command_tests.cpp`, full baseline
  52.13s -> affected workflow 3.24s, 48.89s saved.
- Polygon operation source sample after operation-family filters:
  `src/mesh/polygon/operations/bevel_edges_operation.cpp`, full baseline 46.86s -> affected
  workflow 3.08s, 43.78s saved.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked -ChangedPath tools/affected_check.ps1,agents/testing-build.md,tests/build_guardrail_tests.cpp`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Target-Local Standard-Library PCH

Task/context: manual in-app prototype builds still paid repeated C++ standard-library parse cost in
large runtime/modeling targets after affected checks selected the right target.

Invariant:

- `QUADER_ENABLE_PCH` owns the opt-out switch in `cmake/QuaderBuildOptions.cmake`.
- Common PCH is private and target-local. Do not publish PCH through target interfaces.
- Keep the common PCH list to stable C++ standard-library headers. Do not add project owner
  headers, Filament/SDL SDK headers, or GoogleTest without a separate measured change.

Affected owners/files:

- `CMakeLists.txt`
- `cmake/QuaderBuildOptions.cmake`
- `cmake/QuaderModeling.cmake`
- `agents/testing-build.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added `quader_enable_common_pch(target)` and applied it to app/runtime/modeling libraries, app
  tests, guardrail tests, and modeling-domain test object libraries.
- Kept PCH behind `QUADER_ENABLE_PCH=ON` so a developer can disable it for local diagnosis.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader`
- Runtime app incremental sample after PCH warm-up: touching
  `src/render/viewport_renderer.cpp`, `quader` build 7.87s -> 5.72s, 2.15s saved.

## 2026-06-27 - Optional Ninja Multi-Config Prototype Build

Task/context: after target-local PCH, Visual Studio/MSBuild still spent avoidable time in warm
incremental app rebuilds used for manual in-app prototyping.

Invariant:

- Keep `vs2026` and `vs2026-debug` as the default expected presets.
- Optional Ninja/MSVC acceleration must use a single contained tree under `build/ninja-msvc`.
- Do not hardcode local Visual Studio or compiler paths in presets. Ninja/MSVC runs from a Visual
  Studio Developer PowerShell/CMD, or an equivalent environment where `cl.exe` is already on `PATH`.
- `tools/affected_check.ps1` derives focused test executable paths from the selected configure
  preset `binaryDir`; it must not assume `build/vs2026`.

Affected owners/files:

- `CMakePresets.json`
- `tools/affected_check.ps1`
- `agents/testing-build.md`
- `cmake/AGENTS.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added optional `ninja-msvc`, `ninja-msvc-debug`, and `ninja-msvc-release` presets.
- Updated affected-check preset resolution so passing `-BuildPreset ninja-msvc-debug` also derives
  `ninja-msvc` and `Debug` when those parameters are not supplied explicitly.
- Added guardrails for Ninja preset presence, no local compiler paths, and no hardcoded focused
  test executable dir.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.BuildOptionsAndDependencyPinsHaveSingleCMakeOwner:BuildGuardrailTests.FormerSdkBoundaryIsAbsorbedIntoAppBuild:BuildGuardrailTests.LocalMachinePathsUseEnvironmentBackedDefaults:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns`
- Through `VsDevCmd.bat`: `cmake --preset ninja-msvc`
- Through `VsDevCmd.bat`: `cmake --build --preset ninja-msvc-debug --target quader_guardrail_tests`
- Through `VsDevCmd.bat`: `build\ninja-msvc\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FormerSdkBoundaryIsAbsorbedIntoAppBuild:BuildGuardrailTests.LocalMachinePathsUseEnvironmentBackedDefaults:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns`
- Runtime app warm incremental sample after the Ninja tree was populated: touching
  `src/render/viewport_renderer.cpp`, `vs2026-debug` `quader` build 7.90s ->
  `ninja-msvc-debug` `quader` build 2.98s, 4.92s saved. The first-time Ninja tree build is not
  included in this warm prototyping comparison.

## 2026-06-29 - ImGui Shell Interaction And Startup Fill Regressions

Task/context: native ImGui shell polish exposed regressions where dragging the sidebar dock splitter
also started viewport box selection, a top-menu popup swallowed the first click on panel dropdowns,
and restoring a wider saved window could show a transparent right strip during boot.

Invariant:

- Viewport pane hover/input must respect active ImGui items. Do not use
  `ImGuiHoveredFlags_AllowWhenBlockedByActiveItem` for normal viewport pane input hit-testing, or
  dock splitters/panel drags can leak into viewport selection.
- Main-menu popups should close for pass-through panel clicks before panels draw, while preserving
  clicks inside the menu popup itself.
- App startup must create the SDL window at the saved normal placement size before applying the full
  placement, so the first rendered frame covers the restored window bounds instead of defaulting to
  1600 px.

Affected owners/files:

- `src/ui/viewport_panel.cpp`
- `src/ui/main_menu_bar.*`
- `src/ui/ui_system.*`
- `src/app/app.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Switched viewport pane hover collection to plain `ImGui::IsItemHovered()`.
- Tracked whether main-menu popups are open and call `ImGui::ClosePopupsExceptModals()` on a
  click-through target outside the menu bar and outside existing popup rects before panel widgets
  are drawn.
- Reused saved window placement to choose the initial `PlatformWindow::create` size, then applied
  the recovered placement as before.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=UiLayoutGuardrailTests.*:WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-29 - Native Window Shell, Layout Restore, And Stable Entity Identity

Task/context: native standalone app needed Windows 11 borderless maximize fixes, persisted window
placement, restored multi-pane viewport layouts, UI chrome/sidebar polish, RMB context selection
preservation, literal default group naming, stable entity UUIDs, and removal of the retired retained
UI stack references.

Invariant:

- Borderless maximize is owned by `PlatformWindow` on Windows. Keep `WM_GETMINMAXINFO` clamped to
  the monitor work area and `WM_NCCALCSIZE` constrained for zoomed client bounds so the app does not
  cover the Windows taskbar.
- Persisted window placement is owned by `AppWindowSettings`; recover saved normal bounds to a
  visible display work area before applying them.
- The native editor uses the ImGui shell only. Do not reintroduce generated UI controller/resource
  paths or stale retained-UI documentation.
- Viewport layout restoration belongs to `ViewportWorkspaceService` with the standalone app using
  `ViewportWorkspaceLayoutPolicy::MultiPane`.
- RMB context picking with a valid component selection must preserve the existing component
  selection when the hit is on another unselected component, then open the context menu against that
  preserved selection.
- New groups default to the literal name `Group`.
- Every scene entity has a stable UUID component; QDR I/O and public snapshots preserve and expose
  it, while duplicated entities receive distinct UUIDs and undo/redo restores created identities.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app_window_settings.*`
- `src/core/user_config_paths.*`
- `src/ui/ui_system.*`, `src/ui/main_menu_bar.*`, `src/ui/viewport_panel.cpp`,
  `src/ui/inspector_panel.*`, `src/ui/outliner_panel.*`
- `src/ui/viewport_selection_controller.*`, `src/ui/viewport_shortcut_controller.cpp`
- `src/viewport/viewport_workspace_service.hpp`, `src/api/quader_viewport_api.cpp`
- `src/scene/scene_document.*`, `src/scene/components.hpp`, `src/io/qdr/qdr_document_*`
- `src/editor/commands/document/*`
- UI/docs guardrails and AGENTS/docs files.

Resolved approach:

- Matched Electron's Windows frameless-window lesson locally: work-area max bounds plus client-area
  insets/constraints for maximized borderless windows, without relying on one-time resize hacks.
- Added placement persistence under the user config directory.
- Restored multi-pane workspace policy for the standalone app.
- Made the custom chrome 35 px, moved the menu beside the logo, removed the old menu strip, kept a
  static bottom bar for maximize verification, and tightened sidebar/dropdown/checkbox sizing and
  colors.
- Added `stduuid` through vcpkg/CMake/license manifest and stored UUIDs on scene entities and QDR.
- Replaced stale docs with current native ImGui/app guidance and removed explicit retired UI stack
  references.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=UISettingsTests.*:AppWindowSettingsTests.*:PlatformWindowChromeTests.*:SceneDocumentTests.CreatesStableEntitiesAndComponents:DocumentCommandTests.TypedLifecycleCommandsRestoreSceneSnapshots:DocumentCommandTests.DuplicateMintsEditorObjectIdsOnceAndRedoReusesThem:DocumentCommandTests.DestroyUndoRestoresEditorObjectIdsForSubtrees:DocumentCommandServiceTests.DefaultGroupNameStaysLiteralGroup:DocumentCommandServiceTests.CoversEntityHierarchySelectionAndLifecycleCommands:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings:EditorApiTests.ViewportHandlesUseWorkspaceWithoutRuntimeHooks:EditorApiTests.ViewportSelectionModeShortcutsApplyThroughPublicSelectionApi:EditorApiTests.RightClickPickWithExistingFaceSelectionPreservesComponentSelectionBeforeContextDelete:ModelingSessionServiceTests.RmbMeshPickWithExistingFaceSelectionPreservesSelectionAndAllowsContextMenu`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ProjectOwnedTextDoesNotReferenceOldCppPathNames:BuildGuardrailTests.SourceBuildAndTestsDoNotUseOldSdkIncludeOrPackagePaths:BuildGuardrailTests.TestingDocsExplainPlacementAndSelectionRules:BuildGuardrailTests.LocalizationResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:BuildGuardrailTests.ImGuiViewportApiKeepsMultiPaneLayoutWorkspace:BuildGuardrailTests.RuntimeUiUsesNativeImGuiStackOnly:WindowGuardrailTests.BorderlessWindowModeIsDefault:WindowGuardrailTests.BorderlessMaximizeUsesMonitorWorkArea:WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned:DependencyLicenseGuardrailTests.*:ProgrammaticApiGuardrailTests.PublicBehaviorTestsUseApiOrScenarioDsl:ProgrammaticApiGuardrailTests.PublicDocsExamplesManifestClassifiesEveryDocsAndExamplesFile:SelectionModeAdapterGuardrailTests.*:ViewportToolGuardrailTests.ViewportPanelProvidesPointToolContextToModalTools`
- Explicit retired UI stack string search over source/docs returned no matches outside ignored build
  and third-party/license folders.

## 2026-06-29 - Daily DRY/OOP Drift Audit Completion

Task/context: exhaustive daily DRY/OOP drift audit across API/commands, modeling/projection,
viewport/input/tools, UI/layout/localization, render/assets/scene, QDR I/O, docs/build/tests, with
six parallel audit subagents and main repo-wide inspection.

Invariant:

- `refactor.md` is the current-run source of truth and must not contain unchecked actionable items
  before completion.
- Inspector one-shot transform edits must route through `SceneTransformDragService`; `InspectorPanel`
  may stage UI field values but must not build `QuaderSceneTransformEdit` batches, compute selected
  transform pivots, filter locked/selected-descendant targets, or pin local pivots itself.
- Viewport shading conversion helpers that include viewport owners live under `src/viewport`, not
  public API headers.
- Modeling selection, drawn-plane lifecycle, overlay/projection, scene render extraction, material
  texture mutation, QDR root/header/project codecs, scene-batch reparenting, and docs/build
  guardrail policy each have one owner recorded in `refactor.md`.

Resolved approach:

- Completed all actionable `refactor.md` items A01-A09, U01-U07, M01-M05, R01-R05, and T01-T07.
- Final U07 fix moved inspector-style move/rotate/scale commits into `SceneTransformDragService`,
  including selected-target filtering, selected-bounds pivot resolution, scale ratio/clamp policy,
  parent-aware pivot pinning, and public API batch commit. `InspectorPanel` now only calls the
  transform owner.
- Moved `quaderApiToViewportShadingMode`/`quaderApiFromViewportShadingMode` to
  `src/viewport/viewport_shading_mode_conversions.hpp` after the public API guardrail caught the
  internal viewport include under `src/api`.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug`
- `ctest --preset vs2026-debug`
- `powershell -ExecutionPolicy Bypass -File tools\boot_smoke.ps1 -Exe build\vs2026\Debug\quader.exe -ScreenshotPath build\vs2026\Debug\daily-dry-oop-boot-smoke.png -LogCopyPath build\vs2026\Debug\daily-dry-oop-boot-smoke.log -TimeoutSeconds 30`

## 2026-06-29 - Native Point Light First Implementation

Task/context: first native Filament point light path with movable scene entity, inspector-editable
properties, shadow caster state, and rendered viewport material/shadow visibility.

Invariant:

- `SceneDocument` owns durable light state via `LightComponent`; UI/API must edit lights through
  `QuaderEditorApi` and document commands, not `SceneDocument::setLight` directly.
- Filament light entities and their position/falloff/shadow mapping stay owned by
  `LightRenderableCache`; public API/UI never cache Filament handles.
- Mesh material rendering uses separate compiled materials: `resources/materials/quader_mesh.mat`
  stays unlit for Shaded/tool-preview editor rendering, while
  `resources/materials/quader_mesh_lit.mat` is the lit/PBR material for Rendered mode lights and
  shadows.
- Shaded viewport mode must not depend on scene lighting. The shared Filament scene contains
  separate rendered and shaded mesh proxies; viewport panes select a scene layer instead of mutating
  shared material state.
- Current configured Filament SDK exposes point-light position/falloff and `castShadows`; its public
  docs list shadow casting support for directional/sun/spot/focused spot, so true point-shadow
  output depends on the SDK internals/version while Quader preserves the caster state and passes it
  through.
- Quader stores light intensity as a Godot-style user-facing energy scalar. For point/spot lights,
  `1.0` uses Godot's default `1000 lm` as the base, then applies a `10000x` viewport exposure
  compensation before calling Filament `Builder::intensity(float)`, for `10,000,000` Filament
  lumens. Do not apply `1 / (4*pi)` because Filament converts point-light lumens to candela
  internally. Directional `1.0` maps to Godot's default `100000 lx` at the same render boundary.

Affected owners/files:

- `src/scene/components.hpp`
- `src/editor/commands/document/*`, `src/editor/commands/document_command_service.*`
- `src/api/quader_api_types.hpp`, `src/api/quader_scene_api.*`, `src/api/quader_commands_api.cpp`,
  `src/api/quader_query_api.cpp`
- `src/render/light_renderable_cache.cpp`, `src/render/mesh_renderable_factory.cpp`,
  `src/render/mesh_renderable_cache.*`, `src/render/material_binding_service.*`,
  `src/render/filament_material_library.*`, `src/render/viewport_renderer.cpp`,
  `src/render/viewport_render_pass_coordinator.*`, `src/render/render_signature_builder.cpp`
- `src/ui/inspector_panel.cpp`, `src/ui/main_menu_bar.*`, `src/ui/viewport_panel.cpp`,
  `resources/locales/*.lang`
- `src/io/qdr/qdr_document_reader.cpp`, `src/io/qdr/qdr_document_writer.cpp`

Resolved approach:

- Added point-light creation and light-component edit commands with undo/redo and optional
  select-created behavior.
- Exposed `QuaderLightComponent`, `QuaderCreatePointLightArgs`, `scene.createPointLight`,
  `scene.setLightComponent`, light snapshots, and typed command `scene.create_point_light`.
- Added light range/falloff persistence in scene, render signatures, public snapshots, and QDR.
- Updated `LightRenderableCache` to set Filament point/spot position from entity world transform and
  range through `falloff`, while keeping rebuild/transform signatures separate.
- Added a separate lit mesh material, bound roughness/metallic only for Rendered proxies, enabled
  Rendered mesh shadow receiving, and made viewport shadowing explicit.
- Added dual mesh proxy layers so Shaded mode renders textured material color through a true unlit
  material while Rendered mode uses the lit/shadowing material.
- Added inspector controls for light type, color, intensity, range, and cast shadows through the
  public scene API.
- Added `LightRenderableCache::filamentIntensity` as the single Godot-style intensity to Filament
  lumen/lux conversion point, including the punctual-light viewport exposure compensation.
- Added the main-menu `Create > Point Light` entry, routed through
  `QuaderCommand::SceneCreatePointLight`.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -PlanOnly -ChangedPath src/render/light_renderable_cache.cpp,src/ui/inspector_panel.cpp,src/api/quader_scene_api.cpp,resources/materials/quader_mesh.mat,tests/editor_api_tests.cpp`
- `cmake --build --preset vs2026-debug --target quader quader_guardrail_tests quader_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:ProgrammaticApiGuardrailTests.*:UiLayoutGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=Console*:EditorApiTests.*:GroundGridMaterialTests.*:GroupingPublicApiTests.*:LightRenderableCacheTests.*:Localization*:ModelingSessionService*Hover*:ModelingSessionService*Overlay*:ModelingSessionService*Pick*:NavigationGizmo*:Overlay*:PlatformWindow*:ProgrammaticApiTests.*:ProgrammaticBehaviorScenarioTests.*:QuaderMeshMaterialTests.*:Render*:RenderExtractionTests.*:SceneRenderBridge*:Sdl*:TextureRegistryTests.*:UI*:Viewport*:Window*`
- `ctest --preset vs2026-debug --output-on-failure`
- Follow-up shaded/menu fix:
  `cmake --build --preset vs2026-debug --target quader quader_guardrail_tests quader_tests quader_modeling_domain_tests`
- Follow-up shaded/menu fix:
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.*`
- Follow-up shaded/menu fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRenderPassCoordinatorTests.*:ViewportRendererTests.*:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBinaryAssetLinks:DocumentCommandServiceTests.CreatePointLightAndSetLightComponentAreUndoable`
- Follow-up shaded/menu fix: `ctest --preset vs2026-debug --output-on-failure`
- Follow-up shaded runtime fix:
  `cmake --preset vs2026`
- Follow-up shaded runtime fix:
  `cmake --build --preset vs2026-debug --target quader quader_guardrail_tests quader_tests quader_modeling_domain_tests`
- Follow-up shaded runtime fix:
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- Follow-up shaded runtime fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRenderPassCoordinatorTests.*:ViewportRendererTests.*:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight`
- Follow-up shaded runtime fix: `ctest --preset vs2026-debug --output-on-failure`
- Follow-up shaded runtime fix:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\boot_smoke.ps1 -Exe build\vs2026\Debug\quader.exe -ScreenshotPath build\vs2026\Debug\shaded-rendered-material-split-boot-smoke.png -LogCopyPath build\vs2026\Debug\shaded-rendered-material-split-boot-smoke.log -TimeoutSeconds 30 -AllowMissingLog`
- Follow-up light intensity unit fix:
  `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- Follow-up light intensity unit fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=LightRenderableCacheTests.*:RenderExtractionTests.*:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight:QdrDocumentIoTests.*`
- Follow-up light intensity unit fix:
  `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.InspectorLightEditsRouteThroughPublicSceneApi:UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- Follow-up light intensity unit fix:
  `cmake --build --preset vs2026-debug --target quader`
- Follow-up light intensity unit fix:
  `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=DocumentCommandServiceTests.CreatePointLightAndSetLightComponentAreUndoable:LightRenderableCacheTests.*:EditorApiTests.ScenePointLightApiCreatesSnapshotsAndEditsThroughHistory:EditorApiTests.SceneCreatePointLightCommandCreatesSelectedPointLight`

## Real Scene Root, Scene Settings, IBL, And Directional Lights

Invariant:

- New projects and migrated QDR documents must contain exactly one real scene-root entity named
  `scene`, marked by `SceneSettingsComponent`. The root is selectable and transformable, cannot be
  destroyed/reparented/renamed by editor commands, and public API snapshots expose it with
  `isSceneRoot`.
- Public editor create paths parent new scene entities, modeling objects, point lights, and
  directional lights under the root when callers do not pass an explicit parent. Low-level
  `SceneDocument` creation stays owner-level and does not silently consult `ProjectDocument`.
- Root-level user entities are children of `scene`, so behavior-test `noParent()` means no
  user-authored parent, not `InvalidEntityId`.
- Scene-wide render settings live on the real root and are edited through the public scene API and
  undoable document commands. Rendered mode applies IBL, skybox background, camera exposure, and
  post-process settings per viewport; Shaded mode remains the editor's unlit material view.
- Directional lights use the same light component/API/persistence path as point lights and are
  represented in selected viewport overlays by a marker plus direction line.

Affected owners/files:

- Scene/data ownership: `src/scene/components.hpp`, `src/scene/scene_document.*`,
  `src/scene/document/project_document.*`, `src/scene/document/project_session.cpp`
- Command/API ownership: `src/editor/commands/document_command_service.*`,
  `src/editor/commands/document/set_scene_settings_command.hpp`, `src/api/quader_api_types.hpp`,
  `src/api/quader_scene_api.*`, `src/api/quader_commands_api.cpp`,
  `src/api/quader_query_api.cpp`
- Render ownership: `src/render/filament_environment_library.*`, `src/render/render_system.*`,
  `src/render/viewport_renderer.*`, `src/render/overlay/viewport_scene_overlay_builder.cpp`
- UI/persistence/resources: `src/ui/inspector_panel.cpp`, `src/ui/main_menu_bar.*`,
  `src/ui/outliner_panel.cpp`, `src/ui/viewport_panel.cpp`, `src/io/qdr/qdr_document_reader.cpp`,
  `src/io/qdr/qdr_document_writer.cpp`, `resources/ibl/*`, `resources/locales/*.lang`

Resolved approach:

- Added `SceneSettingsComponent` as the root marker plus Environment, Camera, and Post Process
  settings structs. `ProjectDocument::ensureSceneRoot()` creates/migrates the real root and
  reparents existing top-level entities during project/session/QDR hydration.
- Added undoable scene-settings command and inspector sections on the selected root for
  Environment, Camera, and Post Process.
- Added Filament environment library using bundled KTX IBL/skybox assets from the Filament material
  demo asset set, copied under runtime `ibl/`, and wired it to Rendered-mode viewports.
- Added public directional-light create args/API/command/menu item/localization and selected
  directional-light overlay representation.
- Updated QDR read/write so scene-root markers and settings persist, while legacy payloads migrate
  into a document with the new root.
- Updated tests and behavior helpers so API snapshots include the root and root-level entities
  parent under `scene`.

Verification:

- `cmake --build --preset vs2026-debug`
- `ctest --preset vs2026-debug`

## 2026-06-29 - Default Rendered Material Roughness And Lighting Notes

Task/context: Rendered viewport material looked like glass and an outward-normal, single-sided open
box still appeared lit inside.

Root cause/invariants:

- `resources/modeling_assets/materials/default/default_roughness.png` is an engine-neutral packed
  material asset whose red channel is black while useful default gloss/smoothness data lives outside
  the Filament roughness-red convention. Binding it directly to `quader_mesh_lit.mat`, which samples
  `roughnessTexture.r`, forces the default material to near-mirror roughness even though
  `MaterialDocument::roughness` defaults to `1.0`.
- The startup default Quader material should bind only the default albedo texture and rely on the
  render fallback white roughness texture plus scalar roughness `1.0`. Do not rewrite QDR material
  loading or saved material texture slots, because explicit persisted roughness data must remain
  authored data.
- Mesh material culling follows the authored `MeshComponent::doubleSided` state through a single
  Filament material culling mode. Do not reintroduce separate shadow-only no-culling unless there is
  a dedicated, tested engine-export reason for it.
- Filament 1.71 SDK documentation does not promise point-light shadow maps; direct shadow
  validation should use directional/sun/spot lights unless Quader adds a separate point-shadow
  strategy. IBL/environment lighting is not shadowed by mesh shadow maps, so enclosed interiors can
  still receive environment light unless environment intensity/AO is adjusted.

Affected owners/files:

- `src/app/app.cpp`
- `src/render/material_binding_service.cpp`
- `tests/core_tests.cpp`

Resolved approach:

- Startup still registers the default roughness texture asset for metadata/package consistency but
  no longer binds it to the startup default material. The default material binds the albedo texture
  only, leaving rendered roughness to the white fallback and scalar factor.
- `MaterialBindingService` keeps the previous single culling-mode behavior for mesh material
  instances.
- Added guardrail assertions that startup default material creation does not bind the packed
  roughness texture and that material instances avoid the reverted separate color/shadow culling.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRendererTests.*:LightRenderableCacheTests.*:RenderExtractionTests.*:RenderSystemTests.*:RenderLifecycleTests.*:RenderProxyLifecycleTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:WindowGuardrailTests.*`
- `cmake --build --preset vs2026-debug --target quader` was blocked by a running
  `quader.exe` holding the output executable open.

## 2026-06-29 - Viewport Overlays Are Post-Process-Free Editor Passes

Task/context: Rendered mode post-processing and FXAA degraded transform gizmos and editor overlays.

Invariants:

- Gizmos, selection wires/fills, source wires, light markers, marquee overlays, and the editor grid
  are editor presentation, not scene content. Scene lights, shadows, bloom, tone mapping, dithering,
  FXAA, and IBL intensity must not affect them.
- Viewport panes use a shared Filament scene, so per-pane overlays stay on the same per-pane
  editor/grid layer mask. Do not move them to the single global overlay layer unless a separate
  multi-viewport isolation strategy is added.
- Filament visibility layers are an 8-bit public/internal contract (`View::setVisibleLayers`,
  `RenderableManager::Builder::layerMask`, renderable/scene SoA `LAYERS`, skybox masks, and shadow
  visibility paths all use `uint8_t`). Do not fork Filament just to expose 32 editor/category bits;
  future overlay categories should use Quader-owned pass/scene/policy structure instead of more
  Filament layer bits.
- `ViewportRenderPassCoordinator` owns pass ordering and visible layer masks. Scene view renders
  scene/tool-preview layers first with the background clear. Overlay view renders the per-pane
  editor layer second with `clear=false`, then restores the renderer clear state. Filament clears
  depth/stencil at the start of every `Renderer::render(view)` call, so the overlay view cannot
  inherit scene depth through `Renderer::ClearOptions`. The overlay pass must instead receive
  invisible depth-only scene mesh proxies before drawing grid/editor geometry; otherwise the grid
  draws over opaque scene meshes and makes materials look transparent.
- `ViewportRenderer` owns the two Filament views. Scene settings are applied only to the scene view;
  the overlay view must keep post-processing, anti-aliasing, dithering, color grading, and shadowing
  disabled, and it must use translucent view blending when composed over the scene view.

Affected owners/files:

- `src/render/viewport_renderer.*`
- `src/render/viewport_render_pass_coordinator.*`
- `tests/core_tests.cpp`
- `tests/render_extraction_tests.cpp`

Resolved approach:

- Added a second Filament `View` per viewport for overlays, sharing the same scene, camera, viewport
  size, and render target as the scene view.
- Split scene and overlay visible layer masks. The scene view sees only rendered/shaded scene layers
  plus tool preview; the overlay view sees the pane's editor/grid layer when the grid or overlays are
  visible.
- Rendered scene pass runs first with normal clear/post-process. Overlay pass runs second with no
  clear, translucent view blending, and no post-process/shadowing, then restores renderer clear
  options. `MeshRenderableCache` creates overlay depth-only mesh proxies on the existing per-pane
  editor layer bits and early render channel 0; those proxies write depth with color writes disabled,
  receive no shadows, and cast no shadows. The grid remains in this editor pass so it is not lit or
  post-processed, but it depth-tests against reconstructed scene depth.
- Reverted the experimental separate color/shadow material culling behavior; mesh material culling
  again follows a single Filament material culling mode.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader`
  rebuilt runtime/tests; the final `quader.exe` link was blocked by a running user-launched
  `quader.exe` instance holding the output executable open.
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QuaderMeshMaterialTests.*:SceneRenderBridgeTests.*:ViewportRendererTests.*:ViewportRenderPassCoordinatorTests.*:OverlayRenderableCacheTests.*:RenderExtractionTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:WindowGuardrailTests.*:UiLayoutGuardrailTests.*`

## 2026-06-30 - Viewport Overlays Use Separate Display-Space Composite

Task/context: Rendered mode camera exposure and combined viewport texture presentation still made
editor overlays look dark/noisy. The first failure was reusing the scene Filament camera for the
overlay view; the remaining failure was compositing transparent overlays into the scene texture
before the UI presentation pass.

Invariants:

- Filament camera exposure is scene render state. Editor overlay colors must remain stable across
  scene camera aperture, shutter speed, and sensitivity changes.
- The overlay camera is not a separate user camera. It must mirror the scene camera look-at and
  projection every frame, then keep neutral exposure for editor-space compositing.
- Texture-backed viewports must keep scene color and editor overlay color in separate render targets.
  The scene target is sampled as linear sRGB by the UI. The overlay target is transparent,
  display-sRGB, premultiplied-alpha editor presentation sampled over the scene image.
- When overlays are hidden, the separate overlay target must still be cleared transparent so stale
  editor pixels cannot remain from a previous frame.

Affected owners/files:

- `src/render/viewport_renderer.*`
- `src/render/viewport_render_target.*`
- `src/render/viewport_render_pass_coordinator.*`
- `src/render/texture_registry.*`
- `src/render/imgui_filament_renderer.cpp`
- `resources/materials/imgui.mat`
- `src/ui/viewport_panel.cpp`
- `tests/core_tests.cpp`

Resolved approach:

- `ViewportRenderer` owns a second Filament camera for the overlay view. The overlay camera shares
  the scene camera transform/projection but resets to neutral exposure on initialization, camera
  updates, and scene-setting application.
- `ViewportRenderer` owns a second `ViewportRenderTarget` for overlays. The scene view renders into
  the linear viewport texture; the overlay view renders into a transparent premultiplied display
  texture. `ViewportPanel` draws the scene texture first, then the overlay texture with ImGui.
- `TextureRegistry` and the ImGui material distinguish straight-alpha display/linear textures from
  premultiplied display textures so the overlay target is not premultiplied a second time.
- Added a source guardrail test so future changes cannot route the overlay view back through the
  scene camera, scene exposure path, or combined viewport texture path.

## 2026-06-29 - Selection Locks, Scene Delete, And Environment Clear Color

Task/context: Editor selection, outliner delete, inspector defaults, and viewport background behavior
regressed across scene entities, lock state, and environment settings.

Invariants:

- `DeleteSelection` must route any selected non-scene-root root scene entity through the scene-aware
  document command path. Lights are scene entities, so keyboard delete, context delete, and outliner
  delete must not depend on modeling object/group predicates.
- User/editor selection filters must reject missing entities, effectively locked entities, and
  entities with `VisibilityComponent::selectable == false`. Viewport picking keeps the existing
  effective-visible filter in addition to this shared selectability rule.
- Locking an entity removes that entity and effectively locked descendants from the current
  selection inside the same undoable command or batch. Undo restores the previous selection; normal
  unlock does not auto-select anything.
- `SceneEnvironmentSettings::clearColor` owns the wireframe/shaded clear color. Rendered mode clears
  black, and skybox background attachment requires environment enabled, `showBackground == true`,
  and `backgroundIntensity > 0.0f`. IBL lighting intensity remains independent of background
  intensity.
- Outliner non-leaf nodes start expanded on first presentation; inspector collapsing sections start
  collapsed unless a later product decision explicitly changes that default.

Affected owners/files:

- `src/editor/commands/modeling/scene_aware_delete_selection_command.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/editor/selection/editor_selection_service.cpp`
- `src/editor/commands/document_command_service.cpp`
- `src/api/quader_scene_api.cpp`
- `src/api/quader_api_mutation_runner.cpp`
- `src/scene/components.hpp`
- `src/io/qdr/qdr_document_reader.cpp`
- `src/io/qdr/qdr_document_writer.cpp`
- `src/render/viewport_renderer.cpp`
- `src/render/filament_environment_library.cpp`
- `src/viewport/viewport_settings.*`
- `src/ui/inspector_panel.cpp`
- `src/ui/outliner_panel.cpp`

Resolved approach:

- Replaced group/object-only scene-aware delete checks with
  `documentCommandSelectedRootEntities(...)` and exposed that through `ModelingSessionService` for
  command execution, availability, viewport context menus, and outliner delete.
- Centralized user-selectable scene filtering in `EditorSelectionService` and projected scene
  selection back into modeling selection after lock and undo/redo paths that mutate scene selection.
- Appended selection cleanup commands after lock edits in `DocumentCommandService`, including batched
  scene lock edits, so lock selection cleanup is part of the same undoable transaction.
- Added `environment.clearColor` to internal/public scene settings, API conversions, QDR
  serialization, inspector editing, and render clear behavior; removed the old viewport
  `BackgroundColor` setting.
- Updated UI guardrails so inspector sections remain collapsed by default and outliner branch nodes
  default open.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked`

## 2026-06-30 - Entity Selection Owns Scene Transform And Duplicate Paths

Task/context: Selecting a light after mesh component selection could leave stale modeling runtime
selection active, so immediate transforms and duplicate/delete command routing could operate on the
old mesh instead of the selected scene entity.

Invariants:

- Object/Mesh selection is editor entity selection: selected meshes, lights, groups, empties, and
  future scene entities are canonical `SceneDocument` `EntityId`s in `EditorSelectionService`.
- Modeling runtime object selection is only a projection/cache for editable mesh workflows.
  Selecting any scene entity through editor selection forces Object/entity mode; component modes are
  polygon-only.
- Entity-mode transforms route through scene/document transform commands and
  `SceneTransformDragService`. Runtime polygon transforms stay only for component modes.
- Public `DuplicateSelection` duplicates selected scene entity hierarchies. The mesh-only
  `duplicate_meshes` action remains available as the generated modeling command id.

Affected owners/files:

- `src/editor/selection/editor_selection_service.cpp`
- `src/editor/selection/selection_projection_service.cpp`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/editor/modeling/services/modeling_hierarchy_duplicate_service.*`
- `src/editor/transform/scene_transform_drag_service.*`
- `src/api/quader_modeling_api.*`
- `src/api/quader_commands_api.cpp`
- `src/api/quader_api_mutation_runner.cpp`

Resolved approach:

- Forced editor scene selection to project as Object/entity mode and clear stale runtime selection
  when the selected scene entities do not map to modeling objects.
- Routed immediate entity-mode `moveSelection` and `transformSelection` through document transform
  edits, while preserving runtime modeling transforms for component modes.
- Added public entity-hierarchy duplicate routing behind `edit.duplicate` and kept mesh duplication
  on `modeling.duplicate_meshes`, wrapping public duplicate in an editor transaction so undo/redo is
  one coherent history step.
- Reprojected runtime selection after undo/redo and light creation when scene selection changes to
  non-modeling entities.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no`

## 2026-06-30 - Undo/Redo And Entity Hierarchy Architecture Pass

Task/context: Six-agent architecture pass on history, entity handling, public command routing, OOP
boundaries, stale code, and DRY cleanup found that redo availability, scripts, duplicate routing,
scene-aware delete predicates, batch lock planning, and repeated QDR/cut/component helpers could
drift across owners.

Invariants:

- `QuaderApiMutationRunner` owns public undo/redo availability. Redo is unavailable while an active
  modeling preview can be canceled by undo, and history availability stays false while an editor
  transaction is active.
- `QuaderScript` records through `EditorTransaction`; history steps are rejected inside a running
  script, and a changed script records the final modeling memento so redo restores component
  selection as well as document edits.
- Public `edit.duplicate` is the scene-aware duplicate command. Mesh-only duplication stays on
  `modeling.duplicate_meshes`; UI/host shortcuts must target `DuplicateSelection`.
- Document hierarchy redo must reuse mesh clone entities already restored by modeling mementos
  instead of recreating them.
- Scene-aware delete route checks, QDR tone-mapping names, plane-cut request conversion, and
  component-mode predicates each have one owner helper.
- Scene batch transform validation plans against simulated parent/lock state, not only live
  `SceneDocument` state before the batch.

Affected owners/files:

- `src/api/quader_api_mutation_runner.*`
- `src/api/quader_script.cpp`
- `src/api/quader_command_ids.hpp`
- `src/api/quader_modeling_api.cpp`
- `src/editor/history/editor_history_service.*`
- `src/editor/commands/document_command_service.cpp`
- `src/editor/commands/document/duplicate_selected_hierarchy_command.hpp`
- `src/editor/commands/modeling/scene_aware_delete_selection_command.*`
- `src/editor/modeling/services/modeling_hierarchy_duplicate_service.*`
- `src/editor/modeling/runtime/session_operations.cpp`
- `src/editor/modeling/services/selection_mode_adapter.hpp`
- `src/editor/selection/editor_selection_service.cpp`
- `src/io/qdr/qdr_project_codecs.*`
- `src/ui/outliner_panel.cpp`
- `src/ui/viewport_panel.*`
- `src/viewport/host_viewport_contract_adapter.cpp`
- `tools/affected_check.ps1`

Resolved approach:

- Added transaction-safe redo checks and script memento recording while keeping editor transactions
  as the source of truth for grouped history.
- Routed outliner and host shortcut duplicate actions through `QuaderCommand::DuplicateSelection`.
- Kept direct document duplication from using selected modeling roots, while preserving document
  group hierarchy duplication and clone reuse for mixed mesh/light redo.
- Centralized repeated delete, QDR, cut, and component-mode helpers and removed stale viewport
  pass-through APIs plus the obsolete cut policy header.
- Added guardrails and behavior coverage for redo preview blocking, script history rejection,
  mixed hierarchy duplicate/delete undo/redo, root protection, scene batch simulated locks, typed
  command catalog uniqueness, and affected-check transform routing.

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_tests quader_modeling_domain_metadata_loop_tests quader_modeling_domain_polygon_loop_tests quader_modeling_domain_runtime_loop_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=*:BuildGuardrailTests.*:DependencyLicenseGuardrailTests.*:ProgrammaticApiGuardrailTests.*:WindowGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=*:Console*:EditorApiTests.*:GroupingPublicApiTests.*:ProgrammaticApiTests.*:Qdr*:Viewport*:Window*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=*`
- `build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=QdrEditorMetadata*`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=QdrIoDocumentTests.*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_brief=1 --gtest_filter=QdrModelingRuntime*:QdrRuntimeIoCompatTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-30 - Scene Root Remains Selectable For Settings

Task/context: after scene/entity selection filtering was centralized, the outliner scene root could
no longer be selected, so the inspector scene settings were unreachable.

Invariants:

- Direct user selection of the scene root is allowed so the inspector can edit
  `SceneSettingsComponent` through the public scene API.
- Bulk object/group selection commands still exclude the scene root; root is an editor settings
  entity, not a normal selectable transform/delete/duplicate target.
- Public duplicate/delete command roots continue to come from
  `documentCommandSelectedRootEntities(...)`, which filters the scene root.

Affected owners/files:

- `src/editor/selection/editor_selection_service.cpp`
- `src/api/quader_commands_api.cpp`
- `tests/editor_api_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.SceneRootSelectionCanEditSettingsButCannotBeDeletedOrDuplicated:EditorApiTests.ObjectSelectAllAndInvertUseSceneEntitiesAndExcludeLockedAndRoot:EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.PublicDuplicateSelectionDuplicatesLightEntityThroughSceneHierarchy`
- `build\vs2026\Debug\quader_tests.exe`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Minimized Or Occluded Windows Must Not Submit Frames

Task/context: minimizing Quader or covering it with another app could leave the restored window in a
broken internal render state.

Invariants:

- Focus loss is not a render-suspension signal; minimized, hidden, occluded, and zero-pixel window
  states are.
- `PlatformWindow` owns drawable/presentable window state through `canRenderFrames()` and the
  testable `platformWindowCanRenderFrames(...)` helper.
- `App::renderFrame()` must check presentability before UI/GPU frame prep and again after chrome
  actions, because the custom minimize button can make the swapchain non-drawable mid-frame.
- If a frame was begun but the window becomes non-drawable before `ImGui::Render()`, `UISystem`
  must close the ImGui frame with `endFrameWithoutRender()` instead of submitting draw data.
- CPU/power idling belongs in the app loop: active viewport input uses a short wait, normal visible
  idle uses a low-FPS wait, and suspended windows wait longer while waking immediately on SDL events.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app.*`
- `src/ui/ui_system.*`
- `src/ui/viewport_panel.hpp`
- `tests/core_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:DependencyLicenseGuardrailTests.*:WindowGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=AppFrameClockTests.*:AppLoopTests.*:AppStartupTests.*:AppWindowSettingsTests.*:AssetProjectArchitectureTests.*:AssetRegistryTests.*:BakeServiceTests.*:Console*:ConsoleModelTests.*:DocumentCommandServiceTests.*:DocumentCommandTests.*:EditorCommandServiceTests.*:EditorHistoryServiceTests.*:FatalErrorHandlerTests.*:GroundGridMaterialTests.*:Localization*:LoggerTests.*:MaterialDocumentTests.*:MathTypesTests.*:ModelingInteractionTests.*:ModelingSessionServiceOverlayTests.*:ModelingSessionServiceTests.*:NavigationGizmo*:OverlayMaterialTests.*:PlatformWindow*:PlatformWindowChromeTests.*:PolygonOperationRegistryTests.*:ProjectDocumentTests.*:ProjectSessionTests.*:QuaderMeshMaterialTests.*:RenderLifecycleTests.*:RenderProxyLifecycleTests.*:RenderSystemTests.*:SceneBoundsTests.*:SceneDocumentTests.*:SceneEntityNamingTests.*:SceneRenderBridgeTests.*:Sdl*:SdlEventUtilsTests.*:TextureRegistryTests.*:TransformPivotTests.*:Ui*:UISettingsTests.*:UndoRedoManagerTests.*:Viewport*:ViewportCameraAdapterTests.*:ViewportCameraTests.*:ViewportContextMenuPolicyTests.*:ViewportGridPlacementPolicyTests.*:ViewportInputRouterTests.*:ViewportLayoutTests.*:ViewportOverlayPolicyTests.*:ViewportOverlayStyleResolverTests.*:ViewportRendererTests.*:ViewportSettingsTests.*:ViewportWorkspaceServiceTests.*:Window*`
- `cmake --build --preset vs2026-debug --target quader`
- Manual smoke: launched `build\vs2026\Debug\quader.exe --borderless-window`, minimized, restored,
  maximized, restored, and confirmed the process/window stayed alive.

## 2026-06-30 - Project Save/Open Restored Through QDR Owners

Task/context: File menu Save, Save As, and Open needed to be restored with `Ctrl+S`,
`Ctrl+Shift+S`, and `Ctrl+O`.

Invariants:

- UI and shortcuts must execute `QuaderCommand::ProjectOpen`, `ProjectSave`, and `ProjectSaveAs`;
  the File menu does not read or write project files directly.
- `QuaderProjectApi` owns project path state and routes durable persistence through
  `QdrDocumentReader::readProjectDocument(...)` and
  `QdrDocumentWriter::writeProjectDocumentAtomic(...)`.
- `ProjectSession::replaceDocument(...)` replaces the live `ProjectDocument` in place so existing
  public API services keep valid references, clears history, rehydrates modeling runtime state from
  authored polygon mesh assets, and marks the opened document clean.
- Document replacement must advance the loaded `SceneDocument` revision past the previous live scene
  revision. The QDR payload can be correct while renderer/UI revision subscribers miss the replacement
  if an opened document's scene/asset counters collide with the prior document.
- QDR project serialization is scene/entity/component based: import-specific Unity/Godot runtime junk
  belongs in import adapters, while durable placeable scene state belongs in `SceneDocument` components
  and `src/io/qdr` component codecs.
- Empty project paths are supplied by runtime hooks from the native shell. Headless/API callers must
  pass explicit paths or install hooks.

Affected owners/files:

- `src/api/quader_project_api.*`
- `src/scene/scene_document.*`
- `src/scene/document/project_session.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/platform/platform_file_dialog.*`
- `src/app/app.cpp`
- `src/ui/main_menu_bar.cpp`
- `resources/locales/*.lang`
- `src/editor/metadata/host_contract.cpp`
- `src/viewport/host_viewport_contract_adapter.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.ProjectDocumentCommandsRouteThroughProjectApi:EditorApiTests.ProjectSaveAsSaveAndOpenUseQdrDocumentIo:EditorApiTests.ProjectCommandsUseRuntimePathHooksForEmptyPaths:EditorApiTests.GlobalCommandKeybindingsResolveWithoutViewportFocus`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=LocalizationResourceTests.*:LocalizationServiceTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrEditorMetadata*.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=UiLayoutGuardrailTests.MainMenuDropdownsUsePopupPaddingAndVerticalSpacing`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.ProjectSaveAsSaveAndOpenUseQdrDocumentIo:ProjectSessionTests.ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Wireframe Viewport Mode Uses X-Ray Authored Topology

Task/context: Wireframe viewport mode was showing an unselected mesh as a mostly solid depth-tested
shape, with hidden topology and the grid masked by invisible mesh depth.

Invariants:

- Wireframe viewport mode is a viewport mode, not selected-object wire styling. It must emit
  unselected scene topology on `ViewportOverlayLayer::WireframeXray` so back/hidden edges remain
  visible and are not occluded by mesh depth proxies.
- `WireframeBase` remains the depth-tested selection/base-wire policy layer. Do not reuse it for
  wireframe viewport scene topology.
- Scene wireframe extraction prefers `MeshAsset::authoredPolygonDocument` when present so authored
  quads/ngons display their real topology instead of baked triangle diagonals. Triangle-index
  extraction is only the fallback for meshes without authored polygon data.
- Depth-only mesh proxies belong to the shared `kViewportOverlayLayerMask` overlay-depth layer, and
  `ViewportRenderPassCoordinator` must include that layer only while the shaded/rendered scene is
  visible. Wireframe mode hides the scene and must exclude the depth-proxy layer so grid and x-ray
  wires are not masked by invisible mesh depth.

Affected owners/files:

- `src/render/viewport_overlay_frame.hpp`
- `src/render/viewport_overlay_policy.*`
- `src/render/overlay/viewport_scene_overlay_builder.cpp`
- `src/render/overlay/viewport_overlay_style_resolver.cpp`
- `src/render/viewport_renderer.cpp`
- `tests/render_extraction_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportSceneOverlayBuilderTests.*:ViewportOverlayPolicyTests.*:ViewportRendererTests.MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame:ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder:ViewportRendererTests.OverlayPassUsesNeutralExposureCamera`
