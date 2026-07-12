# Quader Agent Memory

This file records hard-resolved tasks whose context is easy to lose and costly to rediscover.
Read it after `AGENTS.md` for non-trivial work, but treat current code, tests, AGENTS guidance, and
the user's request as authoritative.

When adding an entry, keep it concise and useful: date, task/context, root cause or invariant,
affected owners/files, resolved approach, and verification. Do not record routine edits, secrets,
private user data, guesses, or unresolved issues. Prefer updating a related existing entry over
adding a duplicate.

## 2026-07-12 - Grid Width And Tier Composition Preserve Color

Task/context: reducing ground-grid or axis line size made the axis colors turn gray and made color
alpha appear unreliable. Ground-grid and mesh-surface-grid controls were also easy to confuse, and
mesh major lines blended with coincident minor lines, producing a light fringe around dark majors.

Invariant:

- Ground-grid `lineThickness`, `majorLineThickness`, and `axisLineThickness` affect only geometric
  anti-aliased width. They must not multiply line coverage amplitude; the corresponding RGBA color
  alpha is the sole opacity control.
- Ground-grid and mesh-surface-grid colors remain separate settings. Their visible labels and show
  toggles must say which grid they control.
- Mesh-surface major slots exclude minor coverage before RGBA composition. The exclusion reserves
  both current and next LOD footprints, uses the wider configured tier, and is independent of the
  major color alpha; a major line therefore anti-aliases directly against the mesh surface.
- Qt color swatches remain stock Fusion palette-only buttons. Their tooltip and accessibility text
  expose localized numeric opacity without adding button text, icons, QSS, or custom painting.

Affected owners/files:

- `resources/materials/ground_grid.mat`
- `resources/materials/quader_mesh.mat` and `resources/materials/quader_mesh_lit.mat`
- `src/viewport/viewport_settings.cpp`
- `src/ui/qt_settings_dialog.cpp`
- `resources/locales/*.lang`
- `tests/core_tests.cpp`, `tests/qt_settings_controller_tests.cpp`, and UI guardrails

Verification:

- Focused ground-grid, viewport-settings, Qt-settings, mesh-signature, and UI guardrail tests:
  14/14.
- Mesh-material shader contract tests: 4/4; both shaded and rendered mesh materials compiled to
  Filament packages during the test build.
- Full CTest: 6/6 targets passed.
- `cmake --build --preset vs2026-debug --target quader` produced the v0.0.99 executable, and the
  GPU/window boot smoke completed without runtime material or log errors.

## 2026-07-12 - Half-Cut Inserts Or Separates Per-Mesh Center Cuts

Task/context: Half-Cut needed to operate on one or more selected meshes, default to adding only a
centered edge loop, and optionally separate and cap both halves through live tool settings.

Invariant:

- Half-Cut is Object-mode only. Each selected mesh uses its own local AABB center and local X/Y/Z
  axis; multi-mesh execution is atomic.
- The default (`Separate Parts = false`) routes through `plane_surface_cut`, keeps one object, and
  only inserts the centered topology cut. `Fill Parts` is ignored in this mode.
- `Separate Parts = true` keeps both plane-cut outputs as distinct selected mesh objects. `Fill
  Parts = true` caps both halves; false leaves their cut boundaries open while still rejecting
  non-manifold output.
- Every preview update rebuilds from the original base state. Cancel restores the exact state and
  history; Apply creates one undo step. New separated scene entities inherit their corresponding
  source's parent, transform, visibility, lock, mesh metadata, and material.
- The action is metadata-driven as `half_cut`, exposes generic X/Y/Z, Separate Parts, and Fill
  Parts controls, and owns the typed public command `quader_command::kModelingHalfCut`.

Affected owners/files:

- `src/mesh/polygon/operations/plane_cut_operation.cpp` and the mirrored SDK-internal operation
- `src/editor/modeling/operations/half_cut_operation.cpp`
- `src/editor/modeling/runtime/session.*`
- `src/editor/modeling/services/modeling_session_service.cpp`
- modeling metadata, public API tool settings, command IDs, locales, CMake, and focused tests

Verification:

- Half-Cut, Plane Cut, command-schema, and geometry focus: 22/22.
- Half-Cut editor API, multi-source scene identity, and command catalog focus: 4/4.
- Full CTest: 6/6 targets passed.
- `cmake --build --preset vs2026-debug --target quader` produced the v0.0.97 executable.

## 2026-07-12 - Saved-Maximized Qt Startup Reveals Only The Final Shell

Task/context: a saved maximized launch briefly painted the retained normal-size
viewport and dock layout, leaving a white strip and apparent duplicate docks
until Qt completed the maximized layout.

Invariant:

- `UISystem` disables painting for the complete `QMainWindow` before its first
  show. It reveals the shell with one synchronous repaint only after the HWND
  is exposed, Windows reports the expected maximized state, the Qt client
  physical size matches `GetClientRect`, and the central widget, menu/status
  bars, and dock geometry cover the final client for three stable samples.
- A timeout may skip only the extra stability samples after those hard
  invariants pass. It must never reveal an internally consistent saved-normal
  layout inside a larger native client.
- Embedded native viewport surfaces stay hidden through the shell gate. If
  their pre-presentation geometry or exposure changes, they hide again and
  restart stabilization before Filament submission.
- Apply the maximized Qt state while hidden; `UISystem::show()` remains the
  sole owner of mapping the top-level window.

Affected owners/files:

- `src/app/app.cpp`
- `src/platform/window_placement.*`
- `src/ui/ui_system.*`
- `src/ui/qt_startup_presentation.hpp`
- `src/ui/qt_viewport_panel.*`
- `tests/qt_viewport_pane_activation_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Verification:

- startup presentation and pane activation tests: 3/3.
- affected UI/app tests: 212/212.
- affected build, window, dependency, and UI guardrails: 80/80.
- `cmake --build --preset vs2026-debug --target quader` produced the v0.0.96
  executable.

## 2026-07-12 - Stock Fusion Uses Its Own Neutral Palette And Button Faces

Task/context: selecting the Fusion style alone still allowed the Windows
accent color to replace alternating-row and highlight roles, and a manually
painted color swatch looked like a square block inside a button.

Invariant:

- `main.cpp` selects Fusion and applies exactly
  `fusion_style->standardPalette()`. This is Qt's uncustomized Fusion palette,
  not an app-authored color theme, and prevents OS accent colors from leaking
  into editor tables.
- `QtColorSwatchButton` remains a normal `QPushButton`. It changes only its
  local `QPalette::Button` role and lets Fusion draw the rounded face, border,
  hover, pressed, and focus states; do not custom-paint a rectangular swatch.
- The Outliner Name section stretches to consume remaining header width while
  Visible and Locked size to their labels, so resizing the dock never leaves a
  dead header strip.

Affected owners/files:

- `src/main.cpp`
- `src/ui/qt_color_swatch_button.*`
- `src/ui/outliner_panel.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Verification:

- focused Fusion/Outliner/swatch guardrails: 5/5.
- `OutlinerPanelTests.*`: 2/2.

## 2026-07-12 - Qt Outliner Collapse Does Not Resynchronize Hidden Selection

Task/context: collapsing an Outliner branch could freeze the Qt shell while the
selected primary entity was inside that branch.

Invariant:

- Collapse/expand signals only update retained `collapsed_entities_`; they do
  not mutate the scene, rebuild the model, or synchronize selection.
- Qt selection synchronization runs only after a model rebuild or a real
  `SceneDocument::selection_revision()` change. It must not call
  `setCurrentIndex()` for a primary row hidden below a collapsed ancestor,
  because Qt can reveal/re-expand that row and re-enter layout/event work.
- The stock-Fusion Outliner is a three-column hierarchy table. Name stretches
  across remaining width; Visible and Locked size to content. Partial check states mean an ancestor determines
  the effective visibility/lock. Rename, visibility, and lock changes route
  only through `QuaderEditorApi::scene()`.

Affected owners/files:

- `src/ui/outliner_panel.*`
- `tests/outliner_panel_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/programmatic_api_guardrail_tests.cpp`

Verification:

- `OutlinerPanelTests.*`: 2/2.
- Outliner/API focused guardrails: 3/3.
- `UiLayoutGuardrailTests.*`: 21/21.
- `cmake --build --preset vs2026-debug --target quader`.

## 2026-07-12 - Qt Fly HUD And Focused Pane Use Retained Fusion Widgets

Task/context: the Qt viewport migration had no fly-navigation HUD and no
visible distinction between the command-targeted pane in multi-viewport
layouts.

Invariant:

- `ViewportInputResult::fly_hud_active` is the fly HUD authority. It shows a
  centered crosshair for the whole fly session; the localized speed/reset
  banner appears only when the pane camera's fly speed differs from default,
  matching the legacy behavior.
- The reset hint names the canonical Middle Mouse binding in every locale; do
  not hardcode an unrelated key in the Qt presentation.
- `ViewportWorkspaceService::focused_pane()` remains the command-target owner.
  A stock `QFocusFrame` identifies it only when multiple panes are visible,
  and surface, viewport-combo, and navigation-gizmo interactions update that
  owner before subsequent commands.
- The HUD and focus cue use stock Fusion widgets and the inherited Qt palette.
  Do not add QSS, app-authored viewport palette overrides, or app-specific theme colors.

Affected owners/files:

- `src/ui/qt_viewport_fly_navigation_hud.*`
- `src/ui/qt_viewport_panel.*`
- `resources/locales/*.lang`
- `tests/qt_viewport_fly_navigation_hud_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `QtViewportFlyNavigationHudTests.*`: 3/3.
- affected UI/localization app tests: 189/189.
- affected Fusion/UI layout guardrails: 20/20.
- `cmake --build --preset vs2026-debug --target quader`.

## 2026-07-11 - Qt Viewport X Retains The Legacy Delete Choice Policy

Task/context: the Qt viewport routed the canonical X/DeletePopup shortcut but consumed it without
presenting the legacy deletion choices.

Invariant:

- X remains owned and gated by `ViewportInputRouter`; Qt must not add a QAction shortcut or raw key
  check. Viewport focus, text input, navigation, operation-preview, and modal-tool gates stay intact.
- Object mode, Group mode, or any selected scene root uses a stock Qt confirmation before the
  scene-aware `edit.delete` command. Component mode uses a stock Qt menu with Delete
  Vertices/Edges/Face and Dissolve Vertices/Edges enabled only for the applicable nonempty mode.
- Commands execute through `QuaderEditorApi`, then synchronize the router's selection-mode
  snapshot. A changed command invalidates hover/overlay caches, and closing either surface restores
  viewport focus. Do not add custom styling or rendering work.

Affected owners/files:

- `src/ui/qt_viewport_delete_popup.*`
- `src/ui/qt_viewport_panel.*`
- `tests/qt_viewport_delete_popup_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `QtViewportDeletePopupTests.*`: 5/5.
- `ViewportInputRouterTests.DeletePopupPreservesViewportFocusTextAndModalToolGates`.
- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader`.

## 2026-07-11 - Qt Component Drags Run At Presentation Cadence

Task/context: after the Qt shell migration, moving a face or vertex on an otherwise empty box could
feel dramatically slower even though the raw polygon transform was cheap. Diagnostics showed the
app timer attempting roughly 700-900 callbacks per second for about 165 actual submissions, while
component drags also rebuilt the same preview first in world space and then again in object-local
space.

Invariant:

- The Qt app timer follows the active screen refresh rate, clamped to 30-240 Hz. Do not restore a
  continuously visible viewport to a 1 ms timer; Qt input still wakes through its event loop and
  the next presentation tick consumes the latest state.
- Component transform drags let the runtime update drag math without mutating the session, then the
  host applies the preview once in object-local space. An unchanged computed sample must not rebuild
  the local preview, invalidate overlays, or force scene synchronization.
- A forced modeling mesh sync bypasses the coarse content-revision gate so changing preview geometry
  reaches the scene, but it still honors per-object projected mesh signatures. Unaffected mesh
  objects must not be recompiled merely because another object's component preview changed.
- Preserve frame ordering: viewport input, modeling-to-scene sync, overlay preparation, render.

Affected owners/files:

- `src/app/app.cpp`
- `src/editor/interactions/transform_drag.hpp`
- `src/editor/interactions/move_tool.cpp`
- `src/editor/modeling/services/modeling_transform_drag_service.cpp`
- `src/editor/modeling/services/modeling_mesh_sync_service.cpp`
- `tests/core_tests.cpp`

Verification:

- Empty-box runtime transform preview benchmark: 5,000 updates in 105.883 ms median-like sample,
  about 0.021 ms/update; the observed stall was not polygon transform math.
- `quader_tests`: 698/698.
- `quader_modeling_domain_runtime_loop_tests`: 281/281.
- `cmake --build --preset vs2026-debug --target quader`.

## 2026-07-11 - Object-Mode Mesh Alignment And Distribution

Task/context: Quader needed native world-space arrangement for selected mesh objects without
changing polygon topology or adding host transforms to the portable modeling SDK.

Invariant:

- `MeshArrangementService` owns host arrangement through scene world AABBs and one batched document
  transform command. It operates only in Object mode on topmost selected mesh roots.
- Alignment promotes a nested active mesh to its selected root and keeps that root fixed.
  Distribution sorts initial centers by world axis then entity ID and keeps both endpoints fixed;
  equal gaps remain signed so overlap is valid.
- World deltas are converted through the inverse parent transform by changing local position only;
  local rotation, scale, custom pivot, selection, and topology remain unchanged.
- The 15 typed actions come from `quader_mesh_arrangement_catalog.hpp`. Menu commands read the
  shared persisted Grid Snap preference and current viewport grid preset, while direct API calls
  default to exact unsnapped results.

Affected owners/files:

- `src/editor/transform/mesh_arrangement_service.*`
- `src/api/quader_modeling_api.*`, `src/api/quader_commands_api.*`, and public arrangement types
- `src/ui/main_menu_bar.*`, `src/ui/ui_system.cpp`, and `resources/locales/*.lang`
- `tests/mesh_arrangement_tests.cpp`, localization tests, and UI guardrails

Verification:

- `tools/affected_check.ps1` passed 81 affected guardrails and 333 affected app tests.
- `quader_guardrail_tests` passed all 164 tests.
- `MeshArrangementTests.*` passed all 7 focused behavior tests.
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-10 - Hovered Face Plane Cut Splits And Deselects

Task/context: Alt-hovering a face and Alt+RMB cutting selected meshes originally reused the existing
Plane Cut split operation incorrectly, then briefly routed through an in-place surface cut. The
final viewport gesture is intended to use the hovered face as one infinite plane, keep both sides,
separate the pieces into mesh objects, and leave nothing selected.

Invariant:

- `cut_selected_meshes_by_hovered_face_plane` must route through the object-separating
  `plane_cut` KeepBoth path using per-selected-object local plane requests.
- The shortcut must keep both sides of every cut mesh, create separate mesh objects for split-off
  halves, and finish with no selected objects, vertices, edges, or faces.
- `plane_surface_cut` must directly classify each face against the infinite plane and split by the
  face/plane intersection segment. Do not route it through `knife_stroke`; knife repair can create
  off-plane connector diagonals on subdivided meshes.
- Alt face-plane hover uses dedicated red plane hover overlay roles and must not reuse the blue
  remove-selection styling.
- New mesh objects created by the shortcut must inherit the source mesh scene transform/material
  identity so the separated half appears in-place and can be selected independently.

Affected owners/files:

- `src/mesh/polygon/operations/plane_cut_operation.cpp`
- `src/editor/modeling/runtime/session.cpp`
- `src/editor/modeling/services/modeling_session_service.cpp`
- `src/editor/viewport/viewport_selection_interaction.cpp`
- `tests/modeling_domain/operation/qdr_document_operation_plane_cut_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrDocumentOperationPlaneCutTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-08 - Bridge Boundary Winding Uses Adjacent Face Votes

Task/context: bridging the preselected open edge loops from `test.qdr` could generate many bridge
faces with reversed normals. Normal Bridge and Interpolated Bridge share the same boundary bridge
surface owner, so both paths needed the same winding fix.

Invariant:

- Boundary Bridge must not rely on shortest-distance loop alignment alone. Before creating bridge
  faces, choose the loop orientation whose candidate quads get the strongest opposite-winding vote
  from adjacent source faces, matching Blender's bridge-loop winding vote behavior.
- `append_bridge_face` must preserve adjacent-edge winding when existing faces can vote; the
  expected-normal fallback is only for isolated faces with no adjacent winding evidence.
- Regression coverage must assert shared edges have opposite winding for both `steps=1` and
  interpolated `steps>1` bridge-edge-boundary operations.

Affected owners/files:

- `src/mesh/polygon/operations/bridge_edge_boundaries_operation.cpp`
- `src/mesh/polygon/quader_poly_document_bridge_surface_helpers.cpp`
- `tests/modeling_domain/operation/qdr_document_operation_bridge_edge_boundaries_tests.cpp`

Verification:

- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_filter=QdrDocumentOperationBridgeEdgeBoundariesTests.*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_filter=QdrModelingRuntimeOperationBridgeTests.*`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_filter=*Bridge*`
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -ChangedPath src/mesh/polygon/operations/bridge_edge_boundaries_operation.cpp,src/mesh/polygon/quader_poly_document_bridge_surface_helpers.cpp,tests/modeling_domain/operation/qdr_document_operation_bridge_edge_boundaries_tests.cpp`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Occluded Maximized Windows And Face-Aware Edge Loops

Task/context: a maximized background Quader window could return fully dark, including custom chrome,
until restored/unmaximized; edge double-click on an open boundary/fan shape failed to select the
Blender-like loop. The first boundary-only pass still failed on the screenshot class because the
selected edge can be a shared corner/manifold edge; the loop side must come from the picked surface
face.

Invariant:

- `SDL_WINDOW_OCCLUDED` is not a hard non-drawable state for `PlatformWindow::can_render_frames()`.
  Hidden/minimized and zero-sized windows still suspend GPU submission; stale occlusion on a
  maximized focused window must not block the ImGui/custom-chrome frame.
- Polygon edge-loop selection owns boundary fan traversal. A boundary seed follows adjacent face
  fans across manifold edges until the next boundary edge, while disconnected same-vertex fan faces
  are not selected. Quader has one double-click edge-loop action, so this boundary path is used
  directly instead of Blender's separate loop/boundary cycling UI.
- Runtime edge picks must preserve or re-query the adjacent picked face for double-click edge-loop
  expansion. Shared corner edges use that face to select the connected coplanar surface boundary
  loop; edge-only topology is ambiguous and can stop at the seed.

Affected owners/files:

- `src/platform/platform_window.cpp`
- `src/mesh/polygon/quader_poly_document_selection_api.cpp`
- `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- `src/editor/modeling/runtime/session_picking.cpp`
- `tests/core_tests.cpp`
- `tests/build_guardrail_tests.cpp`
- `tests/modeling_domain/operation/*`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=PlatformWindowChromeTests.RenderFrameAvailabilityRejectsHiddenOrMinimizedWindowStates:AppLoopTests.SuspendsGpuSubmissionForNonDrawableWindowsAndIdlesBetweenFrames`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrDocumentOperationInsertEdgeLoopTests.*:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadePickSelectDoubleClickDelegatesBySelectionMode:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeShiftAndCtrlDoubleClickEditWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectsBoundaryFanEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickUsesPickedFaceForSharedCornerEdgeLoop`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=*Chrome*:*Window*:*App*`

## 2026-07-07 - Bridge, Viewport Stats, Gizmo Selection Regressions

Task/context: bug-fix pass for normal Bridge inheriting Interpolated Bridge steps, unreliable FPS
and draw-call overlay values, transform gizmos disappearing during navigation or clearing edge
selection on short clicks, Shift/Ctrl edge-loop double-click selection, Quick Pivot leaking through
RMB fly navigation, settings modal close affordance, and same-mesh grouped edge Bridge rejection.

Invariant:

- Normal/instant Bridge always uses one segment. Interpolated Bridge remains the operation that owns
  user-configurable `BridgeOperationSettings::steps`.
- Runtime same-object edge Bridge accepts exactly two connected selected edge groups on one mesh and
  routes through the same boundary-alignment bridge owner as cross-object edge Bridge.
- Viewport info FPS is sampled from completed render frames over the render system's sample window;
  CPU/GPU frame timings stay render-owned. Viewport draw-call/object/primitive counts are scene
  info estimates owned by `src/render/viewport_info_stats.*` and are camera-visible, not raw
  Filament submission counters.
- Transform gizmo frames survive orbit/fly camera movement, but hover is suppressed while
  navigation/camera changes are active. Deferred gizmo clicks preserve existing component
  selection on selected hits and misses.
- Quick Pivot must clear its temporary override if RMB navigation/fly navigation starts before the
  `D` key is released.
- Edge double-click Shift/Ctrl selection must apply Add/Remove to every edge returned by the
  topology edge-loop helper, not just the picked edge.
- `ViewportSettingsPanel` is intentionally a floating settings surface without a close button.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInfoStatsTests.*:RenderSystemTests.ExposesCompletedFramePerformanceStatsFromRendererHistory:ViewportRendererTests.ViewportInfoOverlayUsesRendererTimingAndVisibleMeshSubmissionStats:ModelingSessionServiceSplitTests.*:ModelingSessionServiceTests.NormalBridgeFacesIgnoresInterpolatedBridgeSteps:ViewportInteractionControllerTests.ShortMoveGizmoClickKeepsEdgeSelection:ViewportInteractionControllerTests.TransformGizmoFrameSurvivesNavigationFrames:ViewportToolGuardrailTests.*:UiLayoutGuardrailTests.ViewportSettingsFloatingPanelHasNoCloseButton`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_filter=QdrModelingRuntimeOperationBridgeTests.BridgeSelectedEdgesBridgesTwoEdgeGroupsOnOneCombinedMesh:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeShiftAndCtrlDoubleClickEditWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadePickSelectDoubleClickDelegatesBySelectionMode`

## 2026-07-07 - Shift Rotate Component Drag Extrudes

Task/context: Shift+dragging a selected face with the Rotate transform gizmo regressed to a plain
component rotation instead of creating extruded topology first.

Invariant:

- In face/edge component modes, transform gizmo Shift+drag maps Move and Rotate to `Extrude`, and
  Scale to `Inset`; object mode Shift remains duplicate-selection behavior.
- `TransformDragOperation::Extrude` is valid for Move and Rotate. Move extrudes with an offset;
  Rotate extrudes selected elements through the rotate `Transform3` so the selected cap is copied
  and rotated before commit/repeat-last.
- Component transform previews must convert world-space gizmo deltas through each selected object's
  scene transform before mutating polygon documents. The runtime documents are local to their scene
  object; applying a world pivot or world affine directly moves transformed, mirrored, or pivot-edited
  component selections.
- Cross-object Bridge executes in the survivor object's local space. Before appending another
  selected object document, convert that source document by `inverse(survivorWorld) * sourceWorld`;
  record undo from the original session state, not from the temporary normalized bridge input.
- Cross-object Bridge preview updates cannot re-query live scene transforms after begin-preview,
  because the preview may have temporarily removed the merged source entity. Capture participant
  world transforms when the scene-workspace preview begins and reuse them for every Bridge preview
  settings update and commit.
- Runtime operations that combine selected object documents, currently Bridge and Combine Meshes,
  must route through `ModelingSessionService::execute_runtime_operation_command()` so the editor
  scene-space adapter supplies survivor-local/common-local inputs and the history command records
  the original scene-authored state.
- When the scene-space adapter maps a participant by `inverse(survivorWorld) * sourceWorld`, it
  must reverse that participant's face loops and loop UVs when the relative transform has negative
  determinant. Otherwise mirrored sources keep transformed positions but enter Bridge,
  Interpolated Bridge, or Combine Meshes with inverted winding.
- Repeat-last for component transforms must also convert the repeated world affine or offset into
  object-local transform payloads before calling runtime transform previews; repeat-last for
  runtime operations should re-enter `execute_runtime_operation_command()` instead of bypassing the
  scene-space adapter.
- `src/mesh/polygon` owns copied-vertex extrusion topology. Viewport and runtime drag layers only
  choose and route the semantic operation.

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_modeling_domain_metadata_loop_tests quader_modeling_domain_polygon_loop_tests quader_modeling_domain_runtime_loop_tests quader_tests`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationExtrudeElementsTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProgrammaticBehaviorScenarioTests.ShiftFaceMoveAndRotateExtrudeAndShiftFaceScaleInsetsThroughGizmoDrag:ViewportInteractionControllerTests.ShiftRotateGizmoDragExtrudesSelectedFace`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiGuardrailTests.*Viewport*`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocument*:QdrDocumentOperation*:QdrDocumentOperationExtrudeElementsTests.*:QdrPoly*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntime*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.*BoxSelect*:EditorApiTests.*Input*:EditorApiTests.*Keybinding*:EditorApiTests.*RightClickPick*:EditorApiTests.*Viewport*:GroupingPublicApiTests.*:ProgrammaticBehaviorScenarioTests.*:SelectionModeAdapterTests.*:Viewport*:ViewportBoxOverlayBuilderTests.*:ViewportInteractionControllerTests.*:ViewportSelectionMarqueeOverlayBuilderTests.*:ViewportTool*:ViewportTransformGizmoOverlayBuilderTests.*`
- `cmake --build --preset vs2026-debug --target quader_tests quader_modeling_domain_runtime_loop_tests quader_modeling_domain_polygon_loop_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProgrammaticBehaviorScenarioTests.ShiftFaceMoveAndRotateExtrudeAndShiftFaceScaleInsetsThroughGizmoDrag:ProgrammaticBehaviorScenarioTests.ShiftRotateExtrudeUsesWorldPivotForTransformedComponentSelection:ProgrammaticBehaviorScenarioTests.BridgeAfterShiftRotateExtrudeKeepsTransformedSourceInWorldPlace:ViewportInteractionControllerTests.ShiftRotateGizmoDragExtrudesSelectedFace`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ProgrammaticBehaviorScenarioTests.BridgeFacesPreservesWindingForMirroredSceneSource:ProgrammaticBehaviorScenarioTests.InterpolatedBridgePreviewPreservesWindingForMirroredSceneSource:ProgrammaticBehaviorScenarioTests.CombineMeshesPreservesWindingForMirroredSceneSource`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationExtrudeElementsTests.*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOperationBridgeTests.*:QdrModelingRuntimeOperationTransformPreviewTests.*:QdrModelingRuntimeRepeatLastActionTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*Bridge*:*ShiftRotate*:*CombineMeshes*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOperationBridgeTests.*:QdrModelingRuntimeOperationCombineMeshTests.*:QdrModelingRuntimeOperationTransformPreviewTests.*`

Note: `QdrEditorMetadataToolCatalogTests.HostContractToolCatalogMirrorsEditorCoreRegistryIdentity`
was already red in the affected metadata loop because the viewport descriptor catalog includes
Pivot while the legacy editor-core tool registry has 12 tools.

## 2026-07-07 - Rotated Mesh Selection Uses World AABB Cage

Task/context: Source/Hammer-style object selection for non-world-aligned meshes should keep the mesh
geometry rotated while the editor outline, grid snap reference, and transform drag bounds use a
world-aligned AABB cage.

Invariant:

- `src/render/overlay/render_overlay_snapshot.*` records direct selected mesh world bounds through
  `entity_world_bounds`; group/empty hierarchy bounds stay separate.
- `src/render/overlay/viewport_scene_overlay_builder.*` draws selected mesh AABB cages with the
  existing `GroupBounds` style only in object/group selection contexts. Component modes do not add
  the selected mesh AABB over edit topology.
- `src/editor/modeling/services/modeling_session_service.cpp` makes object-mode transform drag
  bounds prefer `selected_transform_world_bounds` before runtime selection bounds, keeping scale and
  snap math aligned with the visible world-space cage.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportSceneOverlayBuilderTests.AddsSelectedMeshWorldAabbBoundsInObjectMode:ViewportSceneOverlayBuilderTests.AddsHierarchySelectionWiresAndGroupBounds:RenderOverlaySnapshotTests.CapturesSelectionGroupLightHierarchyAndToolOverlayRecords:ViewportOverlayPolicyTests.ObjectTransformDragBoundsPreferSceneWorldAabb`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Outliner Inspection Selection Can Target Locked Entities

Task/context: locked meshes could no longer be selected from the outliner, making the inspector
lock checkbox unreachable and preventing normal unlock from the UI.

Invariant:

- Normal user/editor selection (`select_object`, viewport picking, box select, select-all, invert)
  still filters missing, effectively locked, and non-selectable scene entities.
- Outliner row clicks use `QuaderSelectionApi::select_objects_for_inspection`, which records an
  exact existing-entity scene selection through `EditorSelectionService::set_inspection_scene_selection`
  so locked/non-pickable rows can be inspected and unlocked.
- Selection-driven hierarchy mutations (`DeleteSelection`, duplicate, group, ungroup) use
  `document_command_selected_root_entities`, which ignores effectively locked roots. Selecting a
  locked row for inspection must not become a delete/duplicate/group back door.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.LockedEntitiesCannotBeUserSelectedButCanBeInspectedAndUnlocked:EditorApiTests.LockingParentClearsSelectedModelingDescendantAndUndoRestoresIt:EditorApiTests.ObjectSelectAllAndInvertUseSceneEntitiesAndExcludeLockedAndRoot`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiGuardrailTests.SceneSelectionApplicationOwnerCoversCommandServiceAndProjection:ProgrammaticApiGuardrailTests.OutlinerUsesDocumentCommandRootSelectionOwner`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=GroupingPublicApiTests.*:EditorApiTests.DeleteCommandRouteRemovesSameObjectAndExposesUndoLabel:EditorApiTests.DeleteSelectionDeletesSelectedLightsThroughSceneAwareRoute:EditorApiTests.PublicDuplicateSelectionDuplicatesLightEntityThroughSceneHierarchy:EditorApiTests.SceneRootSelectionCanEditSettingsButCannotBeDeletedOrDuplicated:EditorApiTests.DuplicateMeshesHistoryRestoresSceneOnlyDuplicateState:EditorApiTests.DuplicateMeshesActionCommandPreservesSceneOnlyDuplicateState`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Object-Mode Mesh Selection OBJ Export

Task/context: File > Export OBJ needed to export only the direct object-mode selected meshes,
without adding Assimp or another mesh export dependency for v1.

Invariant:

- `src/io/mesh/obj_writer.*` owns geometry-only Wavefront OBJ serialization and validation.
- `QuaderProjectApi::export_selected_meshes_as_obj` owns export intent: require object mode, collect
  direct selected mesh entities, prefer live modeling mesh compilation, then fall back to
  `AssetRegistry` mesh data for plain mesh entities.
- OBJ export writes local mesh coordinates only; scene transforms, materials, MTL files, textures,
  hierarchy, history, dirty state, and project path are not mutated.
- Single selection writes the chosen path exactly; multi-selection writes sibling
  `Base_<EntityName>.obj` files with sanitized names and numeric collision suffixes.

Verification:

- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.AggregatesDocumentApisAndCommandRouting:EditorApiTests.CommandRegistryCoversEveryAppOperationAndViewportTool:ObjExportTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=LocalizationResourceTests.*:LocalizationServiceTests.*`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-07 - Edge Bevel Corners Fill Local Open Profile Loops

Task/context: beveling adjacent L-shaped opening edges could leave a black terminal hole at the
opposite unshared endpoint; beveling opposite side plus bottom opening edges could leave two bottom
holes at two-selected-edge corners. Both matched missing pieces of Blender's bevel boundary rebuild
around selected endpoints. The opposite side plus bottom opening case also needed straight wall
support edges: the source wall face must not connect old support vertices directly into bevel
profile vertices when a selected edge leaves that wall plane.

Invariant:

- `src/mesh/polygon/operations/bevel_edges_operation.cpp` owns edge bevel topology. Runtime/UI
  bevel paths should not patch terminal holes outside the polygon operation.
- After source-face rebuilds, strip faces, and normal corner/terminal support faces are emitted,
  selected endpoints must close any remaining local open-edge loop connected to that endpoint's
  bevel profile, offset vertices, and unselected support/boundary vertices.
- The local close pass must be constrained to the affected bevel profile component so it does not fill
  unrelated document holes.
- For non-terminal selected corners on an unselected source face, insert the same boundary support
  vertices only when a selected edge leaves that face plane. Flat in-plane L selections should keep
  their existing miter path; mixed wall/extrusion selections need boundary vertices to prevent
  converging or diverging wall support lines.

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_polygon_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationBevelEdgesTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Component Source-Wire Stickiness Is Mode-Specific

Task/context: after source-wire overlay changes, component mode either stuck the cyan wire to the
previous object after any click or hid the wire when switching from selected object mode into
component mode.

Invariant:

- Entering component mode from a selected mesh shows that mesh's fallback `SourceWire` immediately,
  even with no selected components.
- A component hover candidate replaces that fallback wire when the active object has no selected
  component for the current mode.
- Sticky source-wire ownership requires selected components for the exact mode: faces for face mode,
  edges for edge mode, vertices for vertex mode. Object selection, `active_object_id`, and component
  selections from another mode are not sticky.
- Removing the last selected component leaves the active mesh fallback wire visible until another
  hover candidate replaces it.

Affected owners/files:

- `src/editor/modeling/runtime/session.cpp`
- `src/editor/modeling/runtime/session_selection_overlay.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp`
- `docs/overlay-handling.md`

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOverlayTests.QdrEditorCoreOverlayTests`

## 2026-07-04 - Component Handles Depth-Test Without Source-Wire Depth Testing

Task/context: component selected/hover handles needed to stop x-raying through opaque geometry, but
a broader attempt to depth-test `SourceWire` revived old inverted-normal visibility failures.

Invariant:

- `SourceWire` remains draw-on-top editable topology and keeps `SourceWireDepthStamp` as
  non-rendered visibility metadata.
- Component-mode base-frame `SourceWire` and `SourceWireDepthStamp` emission is active-edit-mesh
  only. Other selected meshes must not emit full-mesh source wire just because they are selected.
- Component selected/hover edge and vertex handles are projected with component-specific
  `ViewportOverlaySource` values.
- `ViewportOverlayPolicy` depth-tests only those marked component selected/hover primitives.
  Object-mode selected wires and unmarked helper overlays keep their layer-level draw-on-top
  behavior.
- `OverlayRenderableBuilder` must split line and point batches by primitive depth mode so a single
  native layer can contain both component depth-tested handles and object/helper draw-on-top
  handles.

Affected owners/files:

- `src/editor/projection/selection_overlay_projection.*`
- `src/editor/modeling/runtime/session_selection_overlay.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `src/render/viewport_overlay_frame.hpp`
- `src/render/viewport_overlay_policy.*`
- `src/render/overlay/overlay_renderable_builder.*`
- `tests/core_tests.cpp`
- `tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceTests.ComponentModeSourceWireUsesActiveEditMeshOnly:ModelingSessionServiceTests.SwitchingSelectedObjectToEdgeModeShowsHandlesImmediately:ModelingSessionServiceTests.SwitchingSelectedObjectToFaceModeShowsHandlesImmediately:ViewportOverlayPolicyTests.*:SelectionOverlayProjectionTests.*:OverlayLineMeshBuilderTests.*:OverlayPointMeshBuilderTests.*:SceneRenderBridgeTests.MeshRenderablesReceiveShadowsAndViewportEnablesShadowing`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_runtime_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOverlayTests.*:QdrModelingRuntimePickingTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Point Lights Use Selectable Billboard Gizmos

Task/context: point lights needed a larger upright PNG viewport gizmo, visible in shaded and
rendered viewports, selectable from the viewport, and governed by the existing Show Gizmos toggle.

Invariant:

- Visible point lights are represented by `ViewportOverlayIconKind::PointLight` billboard icons,
  not the old selected-light cross/line marker.
- Point-light billboard icons are shown only when Show Gizmos is enabled and the pane shading is
  `Shaded` or `Rendered`; wireframe panes do not show them.
- Point-light icons are scene gizmos, so they are emitted for all effectively visible point lights,
  selected or not, and use screen-facing icon quads with `OverlayDepthFunc::Always`.
- Viewport selection must include matching screen-space billboard pick candidates so clicking or
  context-clicking the icon selects the light through the public selection API.

Affected owners/files:

- `src/ui/viewport_panel.cpp`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/editor/viewport/viewport_interaction_frame.hpp`
- `src/editor/viewport/viewport_selection_interaction.cpp`
- `tests/render_extraction_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportRendererTests.MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame:RenderOverlaySnapshotTests.CapturesSelectionGroupLightHierarchyAndToolOverlayRecords:ViewportSceneOverlayBuilderTests.AddsSelectedDirectionalLightMarkerAndRetiresPointLightCross:ViewportSceneOverlayBuilderTests.AddsPointLightBillboardGizmosWhenRequested:ViewportInteractionControllerTests.ClickFrameSelectsPointLightBillboardThroughViewportFallback:ViewportInteractionControllerTests.ContextMenuPointLightBillboardPreservesSelectedHitSelection`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Post-Processed Viewport Textures Are Display Encoded

Task/context: rendered viewport dithering looked like heavy noise in black areas, while disabling
dithering made textured surfaces look posterized compared with Filament's material sandbox.

Invariant:

- `ViewportRenderer` owns the scene viewport texture presentation color-space. When Filament
  post-processing is enabled, the scene target has already gone through color grading/gamma output
  and must be registered as `TextureSampleColorSpace::DisplaySrgb` for the ImGui composite pass.
- When post-processing is disabled, the scene target remains linear and must be registered as
  `TextureSampleColorSpace::LinearSrgb` so the ImGui material performs the one required
  `linearToSrgb` encode.
- Overlay viewport targets remain separate premultiplied display-sRGB textures with post-processing
  and dithering disabled.
- The default albedo PNG is 16-bit, but preserving 16-bit PNG precision is a separate texture-loader
  follow-up because sRGB 16-bit content needs a deliberate high-precision linear upload path.

Affected owners/files:

- `src/render/viewport_renderer.*`
- `src/render/viewport_render_target.*`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder:OverlayMaterialTests.ImGuiMaterialCanCompositePremultipliedDisplayTextures:TextureRegistryTests.*`

## 2026-07-04 - Fill Hole Filters Selected Boundary Loops

Task/context: Fill Hole failed on a valid hole when the selection also contained an unrelated
non-boundary edge, matching Blender's more tolerant Fill Holes behavior.

Invariant:

- `src/mesh/polygon/operations/fill_hole_from_edges_operation.cpp` owns polygon Fill Hole topology.
- The operation should fill closed components made from selected open-boundary edges and ignore
  extra selected non-boundary or wire edges when at least one valid boundary loop exists.
- If selected open-boundary edges do not themselves form a closed loop, the operation should expand
  to the containing open-boundary ring when at least three selected edges belong to that ring.
- Fully non-boundary edge loops remain rejected; valid fills switch selection to generated face IDs.

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_polygon_loop_tests`
- `build\vs2026\Debug\quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no --gtest_filter=QdrDocumentOperationFillHoleTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - App Loop Consumes Typed Platform Events

Task/context: `App::run()` classified SDL window event enums directly for quit, resize, native
chrome refresh, idle wait, and live-resize redraw handling.

Invariant:

- Backend event polling, waiting, event-watch installation, and SDL event classification live in
  `src/platform/platform_event.*`.
- `App::run()` consumes `PlatformEvent` flags (`quit_requested`, `mouse_motion`,
  `window_pixel_size_changed`, `window_chrome_state_changed`) and passes only the opaque
  `native_event` payload to UI/chrome backend adapters.
- App headers must not expose `SDL_Event`; live-resize event watches use `PlatformEventWatch` and
  `PlatformEvent` callbacks.

Affected owners/files:

- `src/platform/platform_event.*`
- `src/platform/platform_window.*`
- `src/app/app.*`
- `tests/build_guardrail_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.UiSystemKeepsSdlBehindAppPlatformAdapter:BuildGuardrailTests.AppLoopConsumesTypedPlatformEvents`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=AppLoopTests.*:SdlEventUtilsTests.*`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Component Transforms Keep Generated UVs Procedural

Task/context: an attempted UV regression fix materialized generated face-loop UVs before component
transforms, which made point/vertex manipulation stretch the checker texture from the base shape.

Invariant:

- Component-mode transforms (`Vertex`, `Edge`, `Face`) must preserve the pre-refactor runtime
  behavior: after geometry changes they clear stored face-loop UVs and leave checker coordinates
  procedural/generated at mesh compile time.
- Only object-mode texture-lock paths materialize and preserve loop UVs during transforms. Do not
  extend those texture-lock semantics to component transforms unless an explicit UV-editing feature
  changes the owner invariant.

Affected owners/files:

- `src/editor/modeling/runtime/session_transform_bounds.cpp`
- `tests/modeling_domain/operation/qdr_modeling_runtime_operation_translate_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntimeOperationTranslateTests.*`
- `python tools\check_cpp_style.py --paths-only`
- `git diff --check`
- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests quader_modeling_domain_tests`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - QDR Project Writes Consume Export Snapshots

Task/context: Phase 7 architecture work prepared project export for future background workers.

Invariant:

- QDR project serialization and atomic write internals consume `QdrProjectWriteSnapshot`, not live
  `ProjectDocument`, `SceneDocument`, or `AssetRegistry` state. Compatibility overloads may still
  accept `const ProjectDocument&`, but they must immediately build a snapshot and delegate.
- Snapshot mesh records own authored polygon documents by value; copying `MeshAsset` directly would
  preserve a shared pointer to mutable live polygon data.

Affected owners/files:

- `src/io/qdr/qdr_project_write_snapshot.*`
- `src/io/qdr/qdr_document_writer.*`
- `tests/io_qdr_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.ProjectWriteSnapshotOwnsCopiesForBackgroundExport:QdrDocumentIoTests.ProjectWriterConsumesImmutableExportSnapshot:QdrDocumentIoTests.AtomicProjectWriteUsesCompactJsonWhileDebugSerializerStaysPretty:QdrDocumentIoTests.ProjectAtomicWritePolicySkipsHotSaveSelfParseAndKeepsRoundTripCoverage`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Render Snapshot Boundary Owns Scene/Asset Extraction

Task/context: Phase 5 architecture migration moved render scene, overlay, preview, and UI icon
inputs behind render-owned snapshots and services.

Invariants:

- `SceneRenderBridge`, `SceneRenderSyncService`, mesh caches, light caches, overlay caches, and tool
  preview cache consume render snapshot records, not `SceneDocument` / `AssetRegistry` pointers.
- `ViewportPanel` may submit viewport presentation requests and overlay frames, but must not call
  `build_render_overlay_*` or `ViewportSceneOverlayBuilder` directly. Runtime render access builds
  scene/asset overlay snapshots from the project session and hands them to the render bridge.
- UI icon textures are allocated and destroyed by `UiIconTextureService` under `src/render`; UI
  receives only ImGui texture IDs through `RenderSystem::load_ui_icon_textures()` and clears them
  through `RenderSystem::clear_ui_icon_textures()`.

Affected owners/files:

- `src/render/render_scene_snapshot.hpp`
- `src/render/scene_render_extraction.*`
- `src/render/scene_render_bridge.*`
- `src/render/scene_render_sync_service.*`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/render/ui_icon_texture_service.*`
- `src/editor/render/quader_render_api_runtime_access.hpp`
- `src/api/quader_render_api.cpp`
- `src/ui/viewport_panel.cpp`
- `src/ui/ui_system.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=RenderSceneSnapshotTests.*:RenderOverlaySnapshotTests.*:SceneRenderBridgeTests.UsesRenderSceneSnapshotsForIncrementalMeshSync:SceneRenderBridgeTests.UsesDepthStampedFaceFillForOverlayTriangles:RenderProxyLifecycleTests.*:TextureRegistryTests.*:ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=OopArchitectureGuardrailTests.UiShellLeavesSceneOverlayExtractionToRenderBuilder:OopArchitectureGuardrailTests.RenderSnapshotBoundaryOwnsSceneAndOverlayExtraction:OopArchitectureGuardrailTests.UiDoesNotOwnFilamentResourceAllocation:OopArchitectureGuardrailTests.RenderCodeDoesNotCallDocumentOrAssetMutators:BuildGuardrailTests.UiIconTexturesStayBehindRenderSystemAdapters:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:ProgrammaticApiGuardrailTests.ReadOnlyApiPathsUseConstProjectSessionAccess`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Viewport Selection Overlay Cache Follows Scene Preview Transforms

Task/context: scene-transform drags moved the shaded mesh immediately, but the semantic wire cage
could stay at the pre-drag position until release.

Invariant:

- `ViewportPanel`'s `ViewportPaneOverlayCacheKey` must include `SceneDocument::revision()` and
  `AssetRegistry::revision()`. Modeling overlay extraction already keys on scene revision, but the
  viewport pane cache can otherwise skip asking the modeling owner for a fresh frame during live
  scene-transform previews.
- Persistent selected-object render overlays still live behind render snapshot/cache ownership; UI
  should only decide whether its pane-level semantic overlay frame is stale.

Affected owners/files:

- `src/ui/viewport_panel.*`
- `src/ui/viewport_pane_overlay_cache.*`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportPaneOverlayCacheTests.*:ViewportOverlayPolicyTests.NativeViewportSkipsTransientObjectSelectionOverlayBeforeProjection:ModelingSessionServiceTests.TransformDragObjectSelectionUsesSceneTransformPreview:ModelingSessionServiceTests.ObjectTransformDragRefreshesViewportOverlayCacheKeyDuringScenePreview`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Scene Selection Application Has One Owner

Task/context: Phase 3 architecture work consolidated scene entity selection writes that had drifted
between `SelectEntitiesCommand`, `EditorSelectionService`, and `SelectionProjectionService`.

Invariants:

- Scene entity selection application lives in `src/editor/selection/scene_selection_application.*`.
- `SceneSelectionApplicationMode::UndoableCommand` preserves command undo/redo clear-then-set
  behavior; `SceneSelectionApplicationMode::RuntimeSync` uses diff-style sync for modeling/runtime
  projection so unchanged projection does not churn scene selection revisions.
- `EditorSelectionService` still owns user selection intent, filtering, and modeling projection
  orchestration, but uses the shared owner for selectability, edit resolution, and command payloads.
- Component selection remains modeling-runtime owned; this owner only applies `SceneDocument`
  entity selection.

Affected owners/files:

- `src/editor/selection/scene_selection_application.*`
- `src/editor/selection/editor_selection_service.*`
- `src/editor/selection/selection_projection_service.cpp`
- `src/editor/commands/document/select_entities_command.hpp`
- `src/editor/commands/document/document_command_scene_helpers.hpp`
- `src/editor/commands/document_command_service.*`
- `tests/programmatic_api_guardrail_tests.cpp`
- `tests/modeling_session_service_split_tests.cpp`

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=ProgrammaticApiGuardrailTests.PublicSelectionApiDelegatesEntityEditPolicyToEditorSelectionService:ProgrammaticApiGuardrailTests.ObjectAndGroupSelectionCommandsUseSceneDocumentSelectionOwner:ProgrammaticApiGuardrailTests.SceneSelectionApplicationOwnerCoversCommandServiceAndProjection`
- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests` was attempted but
  `quader_tests` was blocked by unrelated dirty `tests/core_tests.cpp` compile errors around
  `ProjectSessionTests.PublicHistoryIgnoresUntrackedModelingSessionHistory`.

## 2026-07-04 - ProjectObjectRecord Scene Components Stay Canonical

Task/context: Phase 3 migration revisited duplicated live fields on `ProjectObjectRecord`.

Invariant:

- For records bound to live scene entities, mesh, material, modeling object, visibility, and material
  slot fields are projections of `SceneDocument` components. Stale object-record copies must not
  become a second source of truth during indexing, synchronization, validation, or QDR load/save.
- Detached object records without a live scene entity may still carry their own stored fields.

Affected owners/files:

- `src/scene/document/project_document.*`
- `src/io/qdr/qdr_document_reader.cpp`
- `src/io/qdr/qdr_document_writer.cpp`
- `tests/io_qdr_tests.cpp`

Resolved approach:

- `ProjectDocument` rehydrates scene-linked component fields before indexing and per-record asset
  validation, so stale QDR object-record component copies are ignored when valid scene components
  exist.
- Added document and QDR tests proving stale record fields do not dirty a second sync pass and are
  rewritten from scene components.

Verification:

- `cmake --build --preset vs2026-debug --target quader_document quader_project_qdr_io`
- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_tests` currently blocked by unrelated dirty
  `tests/core_tests.cpp` compile errors in another worker's ProjectSession read-view assertions.

## 2026-07-03 - Property Table Numeric Sliders And Grid Defaults

Task/context: inspector/property settings were using mixed ImGui numeric semantics: bounded
properties used `DragFloat` while settings used `SliderFloat`/`SliderInt`, and ImGui sliders did not
open exact text input on double-click in the vendored version. The property table and outliner
hierarchy guide lines also drew full-height vertical segments for last children, producing broken
T branches.

Invariants:

- Bounded non-transform numeric UI controls should route through `quader_slider_float` /
  `quader_slider_int`; those wrappers preserve slider visuals and add double-click exact input via
  ImGui's temp-input activation path. Transform axis rows stay as raw drag inputs, including bounded
  scale, because transform editing needs drag semantics.
- Do not duplicate viewport grid default numbers between runtime settings and host metadata. Shared
  grid defaults live in `src/viewport/viewport_settings_defaults.hpp`; host metadata may project
  those constants but must not depend on runtime `ViewportSettings` objects.
- Property-table child guide lines draw the current row's L connector immediately, then the contained
  widget state flushes the previous child row's extra vertical segment only when another child
  follows. Outliner guide drawing must carry ancestor/last-sibling state through tree recursion.

Affected owners/files:

- `src/ui/imgui_quader_widgets.*`
- `src/ui/inspector_panel.cpp`
- `src/ui/tool_settings_panel.cpp`
- `src/ui/viewport_settings_panel.cpp`
- `src/ui/outliner_panel.cpp`
- `src/viewport/viewport_settings_defaults.hpp`
- `src/viewport/viewport_settings.cpp`
- `src/editor/metadata/host_contract.cpp`
- `tests/ui_layout_guardrail_tests.cpp`
- `tests/core_tests.cpp`
- `tests/modeling_domain/editor_metadata/qdr_editor_metadata_preference_catalog_tests.cpp`

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_tests quader_modeling_domain_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportSettingsTests.DefaultsMatchNativeViewportSettingsState:ViewportSettingsTests.SavesLoadsAndClampsViewportPreferenceKeys:GroundGridMaterialTests.UsesUserWorldAnchoring:DocumentCommandTests.SceneTransformDragServicePreviewsInspectorPrimaryTransformsBeforeCommit`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrEditorMetadataPreferenceCatalogTests.HostContractPreferenceCatalogExposesViewportSettingDefaultsAsEngineNeutralDescriptors`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_filter=UiLayoutGuardrailTests.PropertyTableWidgetIsResizableStripedTwoColumnTable:UiLayoutGuardrailTests.NumericSlidersUseSharedDoubleClickInputWrapper:UiLayoutGuardrailTests.HierarchyGuideBranchesTerminateAtLastChild:BuildGuardrailTests.AppVersionMetadataUsesRootVersionSource`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-01 - Selected Overlay Navigation Must Stay Visible And Cheap

Task/context: `14.tracy` showed selected-all navigation dropping to roughly 10 FPS with 395 objects.
Navigation input was cheap; the cost came from selected overlays, outliner drawing, and transform
preview invalidation. A temporary UI suppression experiment was rejected because it made overlays
disappear during interaction.

Invariants:

- Do not hide selected overlays during navigation or transform drags to paper over CPU cost.
- Object-mode selected overlays can be dense, so render-side cache invalidation must avoid repeated
  whole-frame/per-layer signature scans and must reuse compatible line/point proxy resources on
  camera-only rebuilds.
- `SourceWireDepthStamp` visibility filtering is only meaningful when the frame actually contains
  source-wire depth-stamp triangles; object-mode selected edges must not scan selected face-fill
  triangles to build an empty visibility filter.
- Scene transform previews for large object selections must batch scene transform writes and avoid
  invalidating semantic/base selection overlay caches when only object transforms change.
- The outliner must cache scene tree records by scene revision and skip custom drawing for rows
  outside the visible clip rect.

Affected owners/files:

- `src/render/render_signature_builder.*`
- `src/render/overlay/overlay_renderable_builder.*`
- `src/render/overlay/overlay_renderable_cache.*`
- `src/render/overlay/overlay_proxy_factory.*`
- `src/editor/history/scene_transform_preview_transaction.hpp`
- `src/editor/transform/scene_transform_drag_service.cpp`
- `src/editor/modeling/services/modeling_session_service.cpp`
- `src/scene/scene_document.*`
- `src/ui/outliner_panel.*`
- `src/ui/viewport_panel.*`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=RenderSignatureBuilderTests.*:OverlayLineMeshBuilderTests.*:OverlayRenderableBuilderTests.*:OverlayRenderableCacheTests.*:OverlayProxyFactoryTests.*:SceneRenderBridgeTests.SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged:SceneDocumentTests.BatchedTransformAssignmentsDirtyOnceAndSkipNoOps:DocumentCommandTests.SceneTransformPreviewTransaction*:DocumentCommandTests.SceneTransformDragServiceFiltersTargetsAndRecordsScenePreviewThroughApi:ModelingSessionServiceTests.TransformDragObjectSelectionUsesSceneTransformPreview:ViewportToolGuardrailTests.TransformGizmoAndSelectionUseSharedViewportRuntime`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.Outliner*:ProgrammaticApiGuardrailTests.OutlinerUsesDocumentCommandRootSelectionOwner`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=LocalStressTests.MyFirstProjectLargeWorkflow`

## 2026-07-01 - Dense Object Selection Uses Batched Wires

Task/context: `17.tracy` and manual testing showed viewport performance still collapsed when many
objects were selected, especially in rendered mode, and wireframe mode could not visually distinguish
selected meshes from unselected scene wire.

Invariants:

- Do not replace selected mesh topology with a bounding cage during navigation or transforms.
- Persistent selected-object rendering must submit selected mesh wires as a batched renderable, not
  one wire proxy per selected mesh.
- Batched selected wires must cache local edge topology per selected entity/mesh revision. Transform
  drags should stream the transformed line vertex buffer and update the renderable bounds in place,
  not re-run authored polygon edge extraction or recreate the Filament proxy every drag tick.
- Dense object selections suppress translucent selected face-fill proxies and keep the selected wire
  as the stable selection signal; small selections may keep the face fill.
- Wireframe viewport scene-wire entries must use the selected-edge layer/color/width for selected
  meshes, because the normal selected-object overlay layer is not visible in wireframe mode.

Affected owners/files:

- `src/render/overlay/selected_object_overlay_cache.*`
- `src/render/overlay/scene_wireframe_overlay_cache.*`
- `src/render/viewport_overlay_policy.*`
- `src/scene/scene_document.*`
- `docs/overlay-handling.md`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportOverlayPolicyTests.*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-30 - Tracy Profiling Boot Convention And Diagnostic Build

Task/context: Tracy 0.13.1 was installed for local optimization captures. The user-provided profiler
GUI bundle is at `C:\Users\Drako\Desktop\quader-windows\_SOURCES\windows-0.13.1`; the matching
source checkout needed by the app client was installed at
`C:\Users\Drako\Desktop\quader-windows\_SOURCES\tracy-0.13.1`.

Invariant:

- When the user says to "boot up Tracy", launch both
  `_SOURCES\windows-0.13.1\tracy-profiler.exe` and
  `build\vs2026\Debug\quader.exe`, with the profiler first.
- The VS debug build cache is configured with `QUADER_ENABLE_TRACY=ON`,
  `QUADER_TRACY_ON_DEMAND=ON`, and `QUADER_TRACY_SOURCE_DIR` pointing at the 0.13.1 source checkout.
- Tracy zones are diagnostic-only through `src/diagnostics/profile.hpp`; normal builds remain no-op
  unless `QUADER_ENABLE_TRACY` is enabled.
- First capture `C:\Users\Drako\Desktop\quader-windows\logs\one.tracy` showed the largest stall as
  `TransformGizmoInteraction::beginPendingDrag` (~31.4s inside one frame). Add or preserve deeper
  zones in `QuaderModelingApi::beginTransformDrag`, `ModelingSessionService::beginTransformDrag`,
  preview transaction setup, scene sync, snap cache generation, and first preview apply before
  optimizing that path; otherwise the stall appears as opaque viewport wrapper self-time.
- Capture `three.tracy` showed command availability was no longer the duplicate/selection stall,
  but `Viewport refresh selection overlay` still rebuilt every frame because
  `ViewportPanel::drawPaneContents` unconditionally dirtied `selectionOverlayCache_` after active
  tool interaction. Keep selected overlay refresh driven by content/selection/scene/asset revisions
  plus `selectionOverlaySignature()`, not by every viewport frame.
- The same capture showed transform/duplicate preview begin still spending ~880ms in
  `Modeling mesh sync reconcile scene nodes`, while mesh projection was only ~23ms. Preserve the
  sub-zones in `modeling_mesh_sync_service.cpp` around scene identity lookup, name resolution,
  entity creation, parent/identity/material application, and object binding until the remaining
  scene-node stall is named and fixed.
- Capture `seven.tracy` showed the next transform/render stall came from forced modeling mesh sync
  replacing unchanged mesh assets. `AssetRegistry::replaceMesh` stamps a new mesh revision even when
  the compiled payload is identical, which made render sync destroy/recreate mesh proxies for
  untouched objects during component transform previews. `ModelingMeshSyncService` now compares the
  compiled mesh payload and authored polygon document before calling `replaceMesh`; unchanged assets
  keep their revisions so render sync can skip their proxies.
- Capture `eight.tracy` showed the fixed mesh-revision path was working but the dominant frame stall
  moved to `UISystem::beginFrame` self-time. The ImGui Filament renderer was destroying the previous
  frame's UI vertex/index buffers, material instances, and entities at the start of the next frame,
  which can force CPU/GPU synchronization. Capture `nine.tracy` proved merely buffering destruction
  moved the stall to `ImGui destroy retired frame resource batch`; the renderer now reuses persistent
  UI draw resource slots and only rebuilds a slot when vertex/index/primitive capacity grows. The
  same captures showed save spending seconds parsing the just-serialized QDR payload; hot project
  saves now skip self-parse validation and rely on QDR round-trip tests for writer coverage.
  Authored/modeling mesh assets are persisted through their `polygonDocument` only, not redundant
  compiled vertices/indices/primitives; the QDR reader already regenerates compiled render geometry
  from authored topology on load.

Affected owners/files:

- `src/diagnostics/profile.hpp`
- `CMakeLists.txt`
- `cmake/QuaderBuildOptions.cmake`
- `src/editor/modeling/services/modeling_mesh_sync_service.cpp`
- `src/render/imgui_filament_renderer.*`
- `src/io/qdr/qdr_document_reader.cpp`
- `src/io/qdr/qdr_document_writer.cpp`
- `src/scene/document/project_session.cpp`
- app/UI/viewport/render/save/selection/duplicate owner paths instrumented with `QDR_PROFILE_*`

Verification:

- `cmake --preset vs2026 -DQUADER_ENABLE_TRACY=ON -DQUADER_TRACY_SOURCE_DIR=C:\Users\Drako\Desktop\quader-windows\_SOURCES\tracy-0.13.1`
- `cmake --build --preset vs2026-debug --target quader`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ModelingSessionServiceTests.ComponentTransformSyncKeepsUntouchedMeshAssetRevision:ModelingSessionServiceTests.FaceTransformDragKeepsComponentSelectionAndMovesOnlyFaceGeometry:ModelingSessionServiceTests.TransformDragObjectSelectionUsesSceneTransformPreview:ModelingSessionServiceTests.PublicTransformDragFaceSelectionRecordsModelingHistory`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings:QdrDocumentIoTests.WritesAndReadsUnreferencedProjectAssets:ProjectSessionTests.ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument:ModelingSessionServiceTests.ComponentTransformSyncKeepsUntouchedMeshAssetRevision`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QdrDocumentIoTests.SharedPolygonDocumentCodecPreservesAuthoredNormalShadingAcrossProjectAndLegacyQdr:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings:QdrDocumentIoTests.WritesAndReadsUnreferencedProjectAssets:ProjectSessionTests.ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument:ModelingSessionServiceTests.ComponentTransformSyncKeepsUntouchedMeshAssetRevision`
- Launched `tracy-profiler.exe` and `build\vs2026\Debug\quader.exe`; both processes were alive and
  responding.

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

## 2026-06-30 - Overlay Cache Camera Changes Must Not Rebuild World-Space Face Fills

Task/context: `ten.tracy` showed that after selecting hundreds of objects, navigation stayed slow
even after ImGui frame-resource destruction was fixed. The steady-state cost was dominated by
viewport rendering plus overlay submission. `OverlayRenderableCache::rebuildSlot` ran on camera-only
changes, rebuilt every overlay layer, and repeatedly scanned/rebuilt selected face-fill triangle
layers even though their world-space geometry did not depend on the viewport camera.

Invariants:

- Overlay face-fill triangle layers are world-space render geometry. Camera-only viewport navigation
  must not rebuild their proxies or even rescan their layer signatures.
- Overlay line and point layers are camera-dependent because the builder expands them in screen
  space and source-wire visibility can depend on depth-stamp camera rays.
- `OverlayRenderableCache::rebuildSlot(..., rebuildCameraIndependentLayers)` is the layer-aware
  invalidation boundary. Frame/layer-mask changes rebuild all layers; camera-only changes rebuild
  only `OverlayRenderableBuilder::layerBuildDependsOnCamera(...)` layers.
- `OverlayRenderableBuilder::buildLayer(...)` must skip primitive-kind collectors for layers that
  cannot emit that primitive kind. Large selected-object overlays must not scan all triangles for
  every line/point-only layer.

Affected owners/files:

- `src/render/overlay/overlay_renderable_builder.*`
- `src/render/overlay/overlay_renderable_cache.*`
- `tests/render_extraction_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=OverlayRenderableBuilderTests.*:OverlayRenderableCacheTests.*:OverlayProxyFactoryTests.*:ViewportRenderPassCoordinatorTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportRendererTests.*`

## 2026-07-01 - Native Window Shrink Resize Needs Size-Only Live Repaint

Task/context: During Windows live resize, shrinking the app clipped the old larger UI frame until
mouse-up. A prior timer/move repaint attempt made title dragging lag badly.

Invariants:

- `PlatformWindow` owns Win32 move/size tracking through the subclassed HWND proc. Install the
  message hook for native-window mode too, but apply custom chrome styles only when borderless
  chrome is enabled.
- Live repaint is allowed for committed native client-size changes only: `WM_SIZE` and
  `WM_DPICHANGED`. `WM_SIZING` tracks that the native move/size loop is resizing, but it must not
  present. `WM_WINDOWPOSCHANGED` must not present; it can arrive around non-client and restore/move
  transitions where the drawable is not stable enough for flicker-free GPU presentation.
- Do not repaint continuously for `WM_MOVING`, and do not use a Win32 timer to force frames during
  move/size. That reintroduces drag lag.
- `requestLiveResizeFrame()` must coalesce against the last successfully presented client size, not
  merely the last requested size. If Filament skips `beginFrame()`, the pending size must remain
  eligible for a later live-resize draw instead of being swallowed until mouse-up.
- Live-resize callbacks are success-aware (`bool` return). Size-bearing native messages schedule an
  invalidate-only `WM_PAINT` with erase suppressed; do not use `RDW_UPDATENOW` or directly dispatch
  the render callback from inside `WM_SIZE`/`WM_DPICHANGED`. If Filament declines `beginFrame()`,
  restore the pending size so the repaint remains eligible instead of validating the client area and
  dropping the frame.
- Prime live-resize coalescing with the current client size on `WM_ENTERSIZEMOVE`; that prevents the
  first move-only `WM_WINDOWPOSCHANGED` from repainting while still allowing the first actual size
  change to redraw.
- Do not force Filament `beginFrame()` during live resize. That experiment made resize sizes stick
  unpredictably and introduced drag lag.
- `App::renderFrame()` returns whether a Filament frame was submitted. Only set
  `liveResizeFrameRendered_` after a true return; a skipped `beginFrame()` must not suppress the
  next normal loop frame.
- `App::renderFrame()` snapshots framebuffer width/height once after `refreshPixelSize()` and uses
  that same size for ImGui frame setup, render-data preparation, and `noteFramePresented(...)`;
  mixing separately queried sizes can create clipped UI/scissor state during rapid resize.
- The Win32 subclass must handle `WM_ERASEBKGND` by returning handled for the GPU-backed client
  surface. Letting `DefWindowProc` erase between live-resize frames causes whole-window flicker
  even when Filament is successfully presenting during the drag.
- The main swapchain and ImGui clear pass must use `discard = false`; discarded backbuffers can show
  undefined contents when Windows live resize causes skipped frames.
- During native whole-window live resize, suspend viewport render-target reallocations when stable
  scene/overlay textures already exist. Keep panel splitter drags on the exact resize path and keep
  renderer-capacity UVs so the viewport aspect ratio does not regress.
- The ImGui dockspace must not force both sidebars to the full preferred 340 px width while the
  window is at its 800 px minimum. Compress the per-frame sidebar minimum before the viewport or
  sibling dock node collapses, and route panel minimums through that effective frame style.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app.cpp`
- `src/render/render_system.*`
- `src/ui/ui_system.*`
- `src/ui/viewport_panel.cpp`
- `tests/core_tests.cpp`
- `tests/build_guardrail_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=AppLoopTests.SuspendsGpuSubmissionForNonDrawableWindowsAndIdlesBetweenFrames:PlatformWindowChromeTests.*:OverlayMaterialTests.ImGuiMaterialCanCompositePremultipliedDisplayTextures:ViewportRendererTests.PresentsSceneAndOverlayViewportTexturesThroughUiEncoder`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=WindowGuardrailTests.*:UiLayoutGuardrailTests.ViewportUsesRendererCapacityUvsDuringPanelSplitterDrag`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-04 - Pressed Shortcut Chords Must Use SDL Event Modifiers

Task/context: `Ctrl+C` and debug-only `Ctrl+Shift+C` copied selection data but also triggered
viewport focus behavior. The root cause was split input ownership: command keybindings used the
frame's pressed chords, the viewport router also received a placeholder shortcut after command
execution, and the host adapter treated an empty viewport shortcut as a command target that could
resolve to focus selection.

Invariants:

- SDL keydown events own exact pressed shortcut chords because `SDL_KeyboardEvent::mod` is the
  modifier state at the keydown event. ImGui remains the source for UI/text focus, mouse/wheel, and
  held per-frame navigation state.
- A resolved command keybinding or pending chord must mark the `ViewportInputFrame` keybinding input
  as consumed. The viewport router must not interpret a command shortcut as a viewport action in the
  same frame.
- `ViewportShortcutKind::None` is a sentinel only. It must not map to a command target and must not
  request focus/document navigation.
- User shortcuts for viewport commands with no existing binding must use the command's default
  keybinding context, so Focus Selection remains viewport-scoped instead of becoming a global
  shortcut.

Affected owners/files:

- `src/platform/sdl_input_adapter.*`
- `src/platform/platform_event.*`
- `src/ui/ui_system.*`
- `src/ui/viewport_input_frame_builder.*`
- `src/ui/viewport_panel.cpp`
- `src/viewport/viewport_input_router.cpp`
- `src/viewport/host_viewport_contract_adapter.cpp`
- `src/api/quader_commands_api.cpp`
- `tests/core_tests.cpp`
- `tests/editor_api_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportInputFrameBuilderTests.ModifierHelperPreservesCanonicalKeyMods:SdlInputAdapterTests.KeyEventModifiersBuildPressedShortcutChord:ViewportInputFrameBuilderTests.EventPressedChordsOverrideImGuiPressedFallback:ViewportInputRouterTests.ConsumedCommandKeybindingDoesNotRouteViewportShortcut:ViewportInputRouterTests.EmptyShortcutSentinelDoesNotRouteViewportAction:ViewportHostContractAdapterTests.EmptyShortcutDoesNotMapToCommandTarget:EditorApiTests.GlobalCommandKeybindingsResolveWithAndWithoutViewportFocus:EditorApiTests.DebugCopySelectionJsonShortcutWinsOverFocusSelectionBind:EditorApiTests.FocusSelectionUserShortcutUsesViewportContext`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-06 - Mesh Surface Grid Shader Is A Mesh Material Uniform Path

Task/context: Add an optional TrenchBroom-style grid drawn on mesh surfaces while keeping the
ground grid and overlay systems as separate owners. The feature is toggled from View > Mesh Grid and
uses dedicated mesh-grid minor/major color-alpha settings plus existing viewport grid line-size
settings.

Invariants:

- Mesh surface grid drawing belongs in the mesh Filament materials and `MaterialBindingService`
  uniforms. Do not add separate overlay geometry for this path; overlay depth proxies must remain
  depth-only and must not receive visible grid updates.
- `ViewportSettings` owns the persisted `ShowMeshGrid` toggle. The UI menu should route through the
  normal viewport bool setting helper, while `ViewportPanel::prepare_viewports()` passes the full
  current settings and grid preset to `SceneRenderBridge::update_mesh_surface_grid(...)`.
- `ViewportSettings` also owns `MeshGridMinorColor` and `MeshGridMajorColor`; the settings panel
  exposes them from the normal descriptor model and `MaterialBindingService` maps them to
  `surfaceGridMinorColor` / `surfaceGridMajorColor`.
- Mesh grid line thickness changes width only. Do not multiply mesh-grid line coverage by
  `min(thickness, 1.0)`, because that makes selected RGB colors unable to reach the chosen color
  even when color alpha is 1.0.
- Default mesh albedo still resolves through `materials/default/default_albedo.png`; temporary
  source swaps should preserve the previous image beside it instead of changing catalog package
  paths.

Affected owners/files:

- `resources/materials/quader_mesh*.mat`
- `src/render/material_binding_service.*`
- `src/render/mesh_renderable_factory.*`
- `src/render/mesh_renderable_cache.*`
- `src/render/scene_render_bridge.*`
- `src/ui/main_menu_bar.cpp`
- `src/ui/viewport_panel.cpp`
- `src/viewport/viewport_settings.*`

Verification:

- `cmake --build --preset vs2026-debug --target quader_mesh_material quader_mesh_lit_material`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportSettingsTests.*:QuaderMeshMaterialTests.*:SceneRenderBridgeTests.UsesExplicitTextureColorSpaceForMaterialUploads:ViewportRendererTests.MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame:GroundGridMaterialTests.*:LocalizationTests.*:Localization*`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.*:BuildGuardrailTests.LocalizationResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:ProgrammaticApiGuardrailTests.*Viewport*`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-06 - Object Selection Wire Uses Transient Camera-Clipped Overlay Lines

Task/context: Object/mesh selection mode orange wires disappeared or faded near the viewport camera
because they used the persistent selected-object shader-expanded line cache, while component-mode
cyan/blue wires used CPU-clipped per-viewport overlay line quads.

Invariants:

- Selected object wire lines must stay in the transient `ViewportOverlayFrame` path so
  `OverlayLineMeshBuilder` clips them with the active `ViewportOverlayCamera`, matching component
  wire behavior in every viewport.
- `SelectedObjectOverlayCache` is face-fill-only. Do not reintroduce selected-object wire proxies,
  `SelectedObjectLineVertex`, or selected-object `nearPlane` shader uniforms there.
- `ModelingSelectionOverlayOptions::suppress_persistent_object_selection` suppresses selected object
  face-fill triangles from the transient frame, not the selected object wire lines. The persistent
  cache supplies the face fills for object mode; the transient frame supplies the wires.
- The remaining persistent shader-expanded line path is the scene wireframe cache. Keep it separate
  from object selection wire behavior unless wireframe mode itself is being changed.

Affected owners/files:

- `src/editor/modeling/services/modeling_session_services.cpp`
- `src/render/overlay/selected_object_overlay_cache.*`
- `src/render/scene_render_bridge.cpp`
- `tests/core_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportOverlayPolicyTests.PersistentObjectSelectionCacheOwnsFaceFillsOnly:ViewportOverlayPolicyTests.NativeViewportKeepsTransientObjectSelectionWireBeforeProjection:ModelingSessionServiceTests.PersistentObjectSelectionSuppressionKeepsTransientWire:RenderOverlaySnapshotTests.PersistentSceneWireframeLineCacheUsesSnapshotClipPlanes:RenderOverlaySnapshotTests.CapturesSelectedObjectAndSceneWireframeRecordsWithDirtyRevisions`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-06 - Ground Grid Far Clip Is A Shader Fade

Task/context: The scene camera far clip affected scene geometry, but the ground grid did not fade
like Blender when the far clip was reduced. The grid is a render-owned overlay plane with a custom
unlit material, not normal scene mesh material, so it needs its own far-plane uniform and alpha
fade.

Invariants:

- `ViewportGridRenderer` must pass the active perspective camera far plane into `ground_grid.mat`.
- `ground_grid.mat` should keep its existing large-distance and plane-edge fades, then multiply by
  a Blender-style far fade from half the far clip to the far clip using full world distance to the
  camera.
- Do not apply that distance fade in orthographic mode; orthographic grid size is viewport-derived
  and camera-to-plane distance can be unrelated to visible grid coverage.

Affected owners/files:

- `resources/materials/ground_grid.mat`
- `src/render/viewport_grid_renderer.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target ground_grid_material`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=GroundGridMaterialTests.UsesUserWorldAnchoring`

## 2026-07-06 - Superseded: Vertex Handles Drew On Top

Task/context: Flipped-normal meshes made vertex handle dots disappear unless the camera was very
close, while the same vertices on normal exterior faces stayed visible. The points were normal
depth-tested billboards, so two-sided or flipped surface depth could win over the dot quad.

Superseded: the 2026-07-07 vertex leakage fix below replaces this draw-on-top policy with
depth-tested vertex points plus a small point depth bias.

Former invariants, kept only as historical context:

- `UnselectedVertex`, `SelectedVertex`, and `HoverVertex` are editable viewport handles and should
  draw on top. Do not route component selected/hover vertex points through a separate depth-tested
  policy.
- Component wires/edges still use the depth-tested edit-wire batch path. This change is for point
  handles only.
- Do not reintroduce component-point private depth bias helpers in
  `OverlayRenderableBuilder`; avoiding the surface-depth fight is the owner policy.
- The per-object selection overlay cache must strip `SourceWire` and `SourceWireDepthStamp`
  primitives for inactive selected objects in component modes. Building one object in isolation can
  otherwise re-infer that inactive selected object as the source-wire owner.

Affected owners/files:

- `src/render/viewport_overlay_policy.cpp`
- `src/render/overlay/overlay_renderable_builder.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `tests/core_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- Superseded; use the 2026-07-07 depth-tested vertex point verification below.

## 2026-07-07 - Vertex Points Depth-Test With Bias And Inside-Out Room Exception

Task/context: after vertex points were made draw-on-top to avoid flipped-normal depth fights, normal
outward meshes still showed back/hidden vertex dots through shaded surfaces. CPU source-wire
depth-stamp filtering was not enough because the real point batches still used an always-pass depth
state. After switching editable vertex point batches to depth-tested, inside flipped-normal rooms lost
their visible/selectable vertex handles because the room surface depth could hide them from inside.

Invariant:

- `UnselectedVertex`, `SelectedVertex`, and `HoverVertex` point batches are depth-tested by policy;
  they must not use `OverlayDepthFunc::Always`.
- Use the existing point post-projection depth bias (`kOverlayPointDepthBias`) to keep same-surface
  vertex handles from z-fighting. Do not move point X/Y and do not add private component-point depth
  hacks.
- `SourceWireDepthStamp` remains non-rendered metadata. `OverlayRenderableBuilder` may use it to
  pre-filter outward-facing same-object vertex point inputs before building point batches. GPU depth
  testing is the normal visibility authority except for the camera-inside inside-out room exception.
- Inside-out source-wire depth stamps must not CPU-cull internal vertex point inputs. When the camera
  is inside the inside-out stamped volume, editable vertex point batches draw on top so inverted-room
  vertices remain visible/selectable. Outside the stamped volume they keep the normal depth-tested
  point path.
- Ordinary `SourceWire` line batches remain crisp draw-on-top topology; do not use this fix to
  CPU-delete source-wire lines.

Affected owners/files:

- `src/render/viewport_overlay_policy.cpp`
- `src/render/overlay/overlay_renderable_builder.cpp`
- `src/editor/metadata/host_contract.cpp`
- `docs/overlay-handling.md`
- `tests/core_tests.cpp`
- `tests/modeling_domain/editor_metadata/qdr_editor_metadata_overlay_catalog_tests.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=ViewportOverlayPolicyTests.MatchesDepthAndHoverSuppressionContract:ViewportOverlayPolicyTests.ComponentSelectionWiresAndVertexPointsUseDepthTestedState:OverlayLineMeshBuilderTests.PerspectiveDepthBiasDoesNotMoveLineOrPointScreenPosition:OverlayPointMeshBuilderTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadataOverlayCatalogTests.HostContractOverlayLayerCatalogStatesRenderDepthSemantics`

## 2026-07-11 - Device-Domain Overlay Bias Must Account For Filament Reversed Z

Task/context: after the Qt direct-swapchain migration, outward-facing convex box corners could be
pickable while their vertex dots were completely hidden by the adjacent shaded faces. CPU overlay
emission and picking tests passed, so they did not prove the real pixel result.

Invariant:

- `OverlayCameraProjector` outputs OpenGL-domain NDC Z for Filament `vertexDomain : device`
  materials. Filament then maps that input with `z * -0.5 + 0.5`, producing reversed device depth.
- A positive toward-camera bias must therefore be **subtracted** from the projector's GL-domain Z
  before Filament performs that mapping. Adding it makes the later reversed depth smaller, pushes
  `GreaterOrEqual` point quads behind the surface, and can hide an entire convex-corner billboard.
- Clamp the biased GL-domain Z at `-1.0f`. Do not turn ordinary editable vertex points into
  draw-on-top geometry; the existing outward hidden-point filtering, GPU occlusion, and inside-out
  room exception remain authoritative.
- Headless projection/emission tests protect the sign and hidden/flipped policy, but final
  verification for this class of regression requires the Vulkan app. The user visually confirmed
  the formerly missing top-corner marker in the rebuilt Qt app.

Affected owners/files:

- `src/render/overlay/overlay_camera_projector.cpp`
- `tests/render_extraction_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=OverlayCameraProjectorTests.*:OverlayLineMeshBuilderTests.PerspectiveDepthBiasMovesTowardCameraWithoutMovingScreenPosition:OverlayPointMeshBuilderTests.*`
- Manual Vulkan smoke: create/view a box in vertex mode from a diagonal camera; the outward top
  convex corner dot is visible while occluded back vertices remain governed by the existing policy.

## 2026-07-07 - Pivot Tool Uses Tool Settings And Origin Rebasing

Task/context: Pivot needed to become a first-class viewport tool on `P`, with hold-`D` Quick Pivot,
generic tool settings, snapping, quick-align actions, undoable permanent pivot changes, and no
visible model jump when rebasing origins.

Invariant:

- Tool settings are exposed through `QuaderToolsApi::active_settings()` as generic
  `QuaderToolSettingsTarget` controls. The UI panel must render these controls and write back
  through `editor.tools()`, not switch on modeling operation kinds.
- Persistent Pivot is owned by `PivotToolService` under `ProjectSession`. Applying it rebases the
  target origin by moving the scene transform and compensating mesh vertices or group child local
  transforms so world bounds/child world matrices stay fixed. Mesh data must be editable CPU data;
  shared mesh assets use clone-on-write.
- Pivot quick-align action labels are axis-stable: Top/Bottom use world Y, Left/Right use world X,
  and Front/Back use world Z. The AABB corner actions must keep that mapping.
- Quick Pivot is a temporary selection pivot override. It survives `D` release, feeds
  `selection_transform_pivot()` for subsequent transform gizmo drags, and clears on selection
  change or deselect. It must not resolve while WASD fly navigation is active.
- Transform gizmos must ask `QuaderQueryApi::selection_transform_pivot()`: quick-pivot override
  first, single selected object/group custom-pivot world origin second, otherwise the normal
  selection center. Default created/imported meshes are not custom-pivoted, so their gizmo stays at
  their selected bounds center; only an applied Pivot rebase marks the transform origin as the later
  transform pivot.
- The Pivot handle overlay is a small yellow `ToolHandle` line-ring sphere, not an overlay point
  billboard. Pivot editing/dragging must mark before-tool interaction as gizmo-consumed so viewport
  selection fallback and box marquee do not run while the pivot handle is being moved.
- Pivot settings expose only Prev/Next align-cycle actions. The cycle has one center preset only;
  Bounds Center is not a separate preset because it resolves to the same position.

Affected owners/files:

- `src/editor/pivot/pivot_tool_service.*`
- `src/api/quader_tools_api.*`
- `src/api/quader_query_api.*`
- `src/ui/tool_settings_panel.cpp`
- `src/ui/viewport_panel.cpp`
- `src/viewport/host_viewport_contract_adapter.cpp`
- `src/editor/tools/base/viewport_tool_descriptor_catalog.cpp`
- `tests/editor_api_tests.cpp`
- `tests/core_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.Pivot*:EditorApiTests.QuickPivot*:ProjectSessionTests.QuickPivotOverrideFeedsTransformPivotAndClearsOnSelectionChange:EditorApiTests.ViewportKeybindingContextsSeparateNavigationFromToolShortcuts`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-07-08 - Selected Source-Wire Edge Loop Face Context

Task/context: double-click edge-loop selection on an already selected open/shared lip edge could
select only the seed or the wrong adjacent side. The selected source-wire path synthesized edge
hits without a `face_id`, and shared-edge face picks could depend on document face order when the
ray hit exactly on the edge. A later open-box repro showed a true boundary seed could still expand
through the picked face perimeter instead of the Blender-style hole loop because face-context
selection ran before boundary fan traversal.

Invariant:

- Runtime source-wire edge hits must carry the same picked-surface context as normal polygon edge
  hits before loop expansion.
- For shared-edge context, face order must not decide the loop side. Choose the incident pickable
  face most facing the pick ray, then use `edge_loop_edges(document, edge, face_id)` so shared lip
  edges expand over the intended picked surface region.
- True open-boundary seed edges must run the boundary fan loop before picked-face context. The
  picked face is still needed for manifold/shared lip edges, but it must not turn a hole boundary
  seed into the surrounding face perimeter.
- Edge-mode `Alt + left click` is an explicit loop-select gesture and routes to the same
  `pick_select_edge_loop` owner as edge double-click. Shift/Ctrl still decide add/remove; Alt only
  chooses the loop gesture.
- Keep regression coverage for a selected/source-wire shared lip edge where the vertical drop face
  is ordered before the top face, plus an open-box missing-side boundary edge whose pick carries
  the top-face context.

Affected owners/files:

- `src/editor/modeling/runtime/session_picking.cpp`
- `src/editor/interactions/runtime_command.cpp`
- `src/editor/modeling/services/modeling_session_services.cpp`
- `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- `src/mesh/polygon/quader_poly_document_selection_api.cpp`
- `tests/modeling_domain/operation/operation_test_helpers.hpp`
- `tests/modeling_domain/operation/qdr_document_operation_insert_edge_loop_tests.cpp`
- `tests/modeling_domain/operation/qdr_modeling_runtime_command_tests.cpp`
- `tests/viewport_interaction_controller_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=QdrDocumentOperationInsertEdgeLoopTests.EdgeLoopSelection*:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadePickSelectDoubleClickDelegatesBySelectionMode:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeShiftAndCtrlDoubleClickEditWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeAltClickSelectsWholeEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectsBoundaryFanEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickUsesPickedFaceForSharedCornerEdgeLoop:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectedSharedLipEdgeUsesPickedSurfaceBoundary:QdrModelingRuntimeCommandTests.EditorRuntimeCommandFacadeDoubleClickSelectedOpenBoxBoundaryUsesHoleLoop`
- `build\vs2026\Debug\quader_tests.exe --gtest_filter=ViewportInteractionControllerTests.AltClickEdgeSelectsLoopThroughViewportFallback:ViewportInteractionControllerTests.ShortMoveGizmoClickKeepsEdgeSelection:ViewportInteractionControllerTests.TransformGizmoFrameSurvivesNavigationFrames`

## 2026-07-11 - Linear Array Uses Fixed A/B Origins And Atomic Duplication

Task/context: Linear Array needed to become a first-class native viewport tool: select one mesh,
drag endpoint B while A stays anchored to the selected mesh origin, preview evenly spaced copies in
real time, and commit the result as one undoable edit.

Invariant:

- Linear Array activates only for exactly one visible, unlocked modeling mesh in Object mode. Count
  is source-inclusive, defaults to 4, and stays within 2..128. A is the source world origin; B is the
  last copy origin. Changing count keeps B fixed, and zero-length arrays intentionally overlap.
- `LinearArrayToolService` owns transient source validation, count, endpoint, axis constraint, and
  preview state. X/Y/Z are semantic, remappable constraint actions; pressing the active axis again
  returns to Free. Space/Enter apply, Escape cancels, and Ctrl inverts global grid snapping during
  endpoint drag. UI code only projects the handle and dispatches intent.
- Editor-owned viewport tools route through `NativeViewportToolCoordinator`. Their descriptors set
  `editor_owned_native_lifecycle`; do not add per-tool exceptions to host-contract lookup.
- Commit runs one `LinearArrayHistoryCommand` and one runtime `duplicate_objects` batch. Results are
  source first then copies A-to-B; copies keep the source parent and durable scene properties, do not
  copy source children, own independent modeling documents/mesh assets, select source plus copies,
  and make B active.
- Modeling mementos preserve entity ID, editor object ID, and UUID across undo/redo. Scene/asset sync
  mutations, including the batch transform assignment, stay behind `ModelingMeshSyncService`.
- Array preview uploads source geometry once and uses shared Filament mesh/material resources for
  transform-only instances. Moving B updates instance transforms without re-uploading geometry.

Affected owners/files:

- `src/editor/array/linear_array_tool_service.*`
- `src/editor/tools/native_viewport_tool_coordinator.*`
- `src/editor/modeling/runtime/session.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `src/editor/modeling/services/modeling_mesh_sync_service.*`
- `src/api/quader_modeling_api.*`
- `src/api/quader_tools_api.*`
- `src/ui/viewport_panel.cpp`
- `src/render/tool_preview_renderable_cache.*`
- `src/render/mesh_renderable_factory.*`
- `tests/editor_api_tests.cpp`

Verification:

- `build\vs2026\Debug\quader_tests.exe --gtest_filter=EditorApiTests.LinearArray*` (6/6)
- `build\vs2026\Debug\quader_tests.exe` (692/692)
- `build\vs2026\Debug\quader_guardrail_tests.exe` (164/164)
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_filter=-QdrGeometryTests.QdrGeometryHelpersRemainStable` (498/498)
- Full CTest still exposes the pre-existing `QdrGeometryTests.QdrGeometryHelpersRemainStable` float
  ray/plane precision failure; no geometry source is changed by Linear Array.

## 2026-07-12 - Qt Viewport Navigation Gizmo Keeps Per-Pane Input Ownership

Task/context: the Qt Widgets migration preserved the Navigation Gizmo preference and View menu
toggle but omitted the rendered, interactive per-pane view cube that the ImGui viewport exposed.

Invariant:

- Every retained Qt viewport pane owns one `QtNavigationGizmo`. Visibility follows the existing
  public preference; the widget uses the stock Fusion palette and frame rather than QSS, palette
  overrides, custom fonts, or custom window chrome. Semantic X/Y/Z colors remain editor visuals.
- Cube geometry and hit testing stay in `quader_editor_core` through
  `build_view_cube_primitives()` and `pick_view_cube()`. Qt only paints those primitives and adapts
  pointer events.
- A face click applies the same unlocked orthographic axis view as the legacy gizmo. Crossing the
  three-pixel drag threshold converts the gesture to orbit and suppresses the click snap.
- A gizmo press focuses its owning workspace pane and grabs the mouse until release or cancel.
  While grabbed, viewport routing remains locked to that pane and modeling hover, selection,
  transform gizmos, pivot, and linear-array interactions are suppressed.
- The gizmo is a native sibling above each embedded Filament child surface and is positioned below
  that pane's floating view/shading controls. Do not move this behavior into render overlays or
  alter render-owned overlay depth policy.

Affected owners/files:

- `src/ui/qt_navigation_gizmo.*`
- `src/ui/qt_viewport_panel.*`
- `tests/qt_navigation_gizmo_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QtNavigationGizmoInteractionTests.*:ViewportCameraTests.ViewCube*` (6/6)
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.QtViewportRestoresPerPaneInteractiveNavigationGizmos:UiLayoutGuardrailTests.QtShellSelectsStockFusionExactlyOnceWithoutThemeOverrides:BuildGuardrailTests.RuntimeUiUsesQtWidgetsFusionOnly` (3/3)
- `cmake --build --preset vs2026-debug --target quader`

## 2026-07-12 - Qt Settings And Tool Windows Retain Stock Auxiliary State

Task/context: the Qt migration needed localized Tool Settings chrome, retained auxiliary-window
placement, and a scannable Settings layout without reintroducing custom theme code.

Invariant:

- `QtAuxiliaryWindowState` persists Settings and Tool Settings geometry plus logical visibility in
  `qt_auxiliary_windows.ini` through `QSettings`. The versioned `QMainWindow::saveState` binary
  remains dedicated to dock layout so existing main-window state stays compatible.
- Settings remains a normal non-modal `QDialog` with the stock Fusion/platform close chrome. Tool
  Settings remains a stock `Qt::Tool` window whose visibility follows the active public tool target.
- Tool Settings title, active-target title, Apply, and Cancel text resolve through
  `LocalizationService`; stable Qt object names remain untranslated.
- Settings categories use a stock `QListWidget` sidebar and `QStackedWidget` right-hand pages.
  Reusable `QtColorSwatchButton` controls set only the local `QPalette::Button` role to the selected
  color, so Fusion draws the normal rounded button face, border, hover, press, focus, and disabled
  states across the complete fluid width. Exact `#AARRGGBB` values remain in tooltip/accessibility
  data, and `QColorDialog::ShowAlphaChannel` preserves alpha editing. Do not replace this with QSS,
  custom painting, icon previews, or custom window chrome.

Affected owners/files:

- `src/ui/qt_auxiliary_window_state.*`
- `src/ui/qt_color_swatch_button.*`
- `src/ui/qt_settings_dialog.cpp`
- `src/ui/qt_tool_settings_window.*`
- `src/ui/ui_settings.*`
- `src/ui/ui_system.*`
- `tests/qt_auxiliary_window_state_tests.cpp`
- `tests/localization_tests.cpp`
- `tests/ui_layout_guardrail_tests.cpp`

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=QtAuxiliaryWindowStateTests.*:LocalizationResourceTests.*:LocalizationServiceTests.*` (17/17)
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=UiLayoutGuardrailTests.*` (19/19)

## 2026-07-12 - Qt Settings Stay Live, Reversible, And Event Driven

Task/context: after the Qt migration, settings controls could display or persist a value without
updating the live viewport/render behavior. Color buttons also needed preview, accept, and cancel
semantics without reintroducing a custom theme.

Invariant:

- The retained Settings dialog covers every viewport preference descriptor: locale, 20 colors,
  16 floating-point values, and 7 booleans. A control write must update the owner state immediately,
  notify retained consumers, and persist through the existing settings owner; the dialog is not a
  second source of truth.
- `QtSettingsController` emits a typed change kind. `UISystem`, the Settings dialog, menus, and
  viewport presentation synchronize from that event, not from per-frame dialog reconstruction or
  polling.
- A `QColorDialog` live preview applies through the same owner route. Accept keeps and persists the
  chosen color; cancel restores the exact pre-dialog value and live presentation. Alpha remains
  part of the value.
- Persistent selected-object and scene-wireframe overlay caches consume narrow immutable style
  snapshots/signatures. A settings edit invalidates only the affected cache; it must not alter
  overlay layer order, depth, blend, visibility, or flipped-mesh policy.
- Settings persistence failures are logged. Legacy/missing semantic-axis values migrate to the
  canonical X/Y/Z defaults without overwriting valid user values.

Affected owners/files:

- `src/ui/qt_settings_controller.*`
- `src/ui/qt_settings_dialog.*`
- `src/ui/qt_color_swatch_button.*`
- `src/ui/ui_system.*`
- `src/viewport/viewport_settings.*`
- `src/render/overlay/viewport_persistent_overlay_style.hpp`
- `src/render/overlay/selected_object_overlay_cache.*`
- `src/render/overlay/scene_wireframe_overlay_cache.*`

Verification:

- Headless control-contract coverage exercises all 44 controls plus color preview/accept/cancel,
  dialog/menu synchronization, and persistence.
- `QtSettingsControllerTests.*`, `ViewportSettingsTests.*`,
  `ViewportPersistentOverlayStyleSnapshotTests.*`, and the relevant UI/render guardrails pass.
- Manual Vulkan verification confirmed a grid-color edit updates the open viewport immediately and
  cancel restores it; the user's original settings file was restored afterward.

## 2026-07-12 - Large-Scene Navigation And Transforms Gate Stable Work

Task/context: `my_first_project.qdr` was the real stress fixture. With 403 selectable entities,
all-selected navigation and transform interaction became effectively unusable even though the GPU
timing was small. The bottleneck was repeated CPU extraction, projection, uniform, retained-widget,
and per-entity history work ahead of presentation.

Invariant:

- The fixture identity for this regression is 2,703,006 bytes with SHA-256
  `05604D3C885792458C0B748F25DC12E19DC7C59949AE0B522B1853CD65D1AB4B`: 418 entities, 416 mesh
  entities, 14 locked entities, and 403 entities selected by Select All. Benchmarks must never write
  the input and must verify its identity after the run.
- Stable owner revisions stop work before scene extraction. `RenderOverlaySnapshotCache` owns the
  renderer-neutral selected-object, scene-wireframe, and scene-overlay snapshots and their narrow
  revision/style signatures; `SceneRenderBridge` remains a snapshot consumer. One prepare pass
  updates the cache once and all visible panes reuse it.
- Mesh surface-grid uniforms use a narrow signature and are not rewritten on unchanged frames.
  Camera projection constants and the exact sRGB-byte conversion table are precomputed/reused for
  overlay projection. These are mechanical hot-path reductions; overlay draw/depth/blend/visibility
  policy is unchanged.
- Transform gizmo selection pivots are cached by selection/scene revisions. Qt navigation-gizmo
  setters ignore identical pointer/pose/visibility state. Outliner and Inspector refresh only for
  relevant scene/modeling/locale revisions and defer retained rebuilds during an active transform.
- Multi-entity transform preview applies one batch assignment per update and records one
  `ApplyEntityTransformsCommand` at commit. Cancel, accept, commit, undo, and redo preserve exact
  transforms while revision growth stays O(1), not O(selected entities).
- Expensive transformed picking projections are lazy. Only an active, previewable, valid Cut or
  Mirror consumer requests them, and a selection-overlay cache hit returns before projection.
  `ModelingSessionService` remains a thin adapter; tool-state gates stay in
  `ModelingOverlayService`.
- The opt-in `qdr_fixture_performance_baseline` target measures the exact load/select/navigation/
  unchanged-snapshot/overlay/transform workload, emits JSON, restores transforms bit-for-bit, and
  rejects any output path that resolves to the input QDR.

Affected owners/files:

- `src/render/scene_render_extraction.*`
- `src/render/scene_render_bridge.*`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/render/material_binding_service.*`
- `src/render/mesh_renderable_cache.*`
- `src/render/overlay/overlay_camera_projector.*`
- `src/core/color_space.hpp`
- `src/editor/viewport/transform_gizmo_interaction.*`
- `src/editor/history/scene_transform_preview_transaction.hpp`
- `src/editor/commands/document/apply_entity_transforms_command.hpp`
- `src/editor/modeling/services/modeling_overlay_service.*`
- `src/editor/modeling/services/modeling_selection_service.*`
- `src/ui/qt_viewport_panel.*`
- `src/ui/qt_navigation_gizmo.*`
- `src/ui/ui_system.*`
- `benchmarks/modeling/qdr_fixture_performance_baseline.cpp`

Verification:

- Release exact-fixture p50: Select All 0.1056 ms; navigation input 0.0003 ms; unchanged snapshot
  baseline 0.1654 ms; 4,824-line camera overlay build 0.3197 ms; 403-entity transform preview
  0.1321 ms. All 403 transforms changed during preview and cancel restored the original checksum.
- Debug 403-entity repeated-drag median is about 6.5-6.7 ms; the dedicated 200-sample fixture run
  measured 6.5006 ms p50 and 10.6451 ms p95.
- In the real Vulkan app, ten all-selected zoom inputs completed in 174 ms and the immediate UI
  state capture returned in 45 ms, replacing the prior multi-second/tens-of-seconds apparent freeze.
- Focused render, viewport, modeling-session, document-command, UI, guardrail, and native-app build
  checks pass; benchmark JSON lives under `build/perf/`.

## 2026-07-12 - All-Selected Gizmo Drag End-To-End Frame Fix

Task/context: the modeling-only stress benchmark was green while the real Qt viewport still let the
403-object selection trail the move gizmo. A genuine red-axis drag and Undo first proved that the
gizmo handle, transform transaction, and restored selection were the path under test. The missing
cost was the rest of the same frame: scene extraction, Filament proxy transforms, selection bounds,
semantic cage generation, camera projection, and GPU buffer updates.

Invariant:

- Qt may deliver press, threshold-crossing move, and release in one input frame. Transform gizmo
  interaction must establish the pending hit before release handling, begin/apply/commit a collapsed
  drag in that frame, and apply the newest pointer sample before any active-drag commit.
- Pure Move previews cache each target's parent-space world basis and reference-aware decomposition
  template at begin. Per-sample work updates local positions only. Rotate/Scale keep the generic
  matrix/decomposition path. Parented transforms, custom pivots, cancel, commit, undo, and redo must
  remain equivalent.
- `SceneDocument::render_component_revision()` covers render-structural/material/light changes.
  A retained `RenderSceneSnapshot` may refresh transforms in place only when asset, selection,
  visibility, mesh-binding, and render-component revisions prove all other sources stable.
- Filament mesh transform updates are dirty-record-only and run inside one local-transform
  transaction. Mesh/material/visibility/light rebuild paths remain conservative fallbacks.
- `RenderOverlaySnapshotCache` retains local bounds by mesh revision, world bounds by entity, and
  stable selected/group ordering. Pure affine translation moves cached AABBs; linear changes use the
  exact eight-corner fallback. Transform-only frames update the retained selection snapshot in place.
  Selection, visibility, hierarchy, mesh, render-component, asset, or mixed scene changes force a
  full rebuild.
- Generic line and edit-wire proxies retain canonical indices when kind and vertex/index counts are
  unchanged, uploading changed projected vertices only. Overlay line count/order, style, depth,
  blend, visibility, and picking policy are not performance knobs and must not change.
- The fixture revision used for this follow-up is 2,703,035 bytes, SHA-256
  `D5516A98E4C4222B2EA11207C066444C88B2D0A701FB1E9C2E8BC949FDE556B0`, FNV-1a64
  `0x28d995351cafb8d6`: 418 entities, 416 mesh entities, 14 locked entities, and 403 Select-All
  transform targets. The benchmark must restore transforms exactly and leave this input unchanged.

Affected owners/files:

- `src/editor/viewport/transform_gizmo_interaction.cpp`
- `src/editor/transform/scene_transform_drag_service.*`
- `src/editor/modeling/services/modeling_transform_drag_service.cpp`
- `src/scene/scene_document.*`
- `src/scene/scene_bounds.*`
- `src/render/render_scene_snapshot.hpp`
- `src/render/scene_render_extraction.*`
- `src/render/scene_render_sync_service.*`
- `src/render/overlay/render_overlay_snapshot.*`
- `src/render/overlay/overlay_proxy_factory.cpp`
- `benchmarks/modeling/qdr_fixture_performance_baseline.cpp`

Verification:

- Debug exact-fixture 200-sample end-to-end CPU median improved from 24.4116 ms to 14.6155 ms;
  final p95 was 18.7171 ms. Final medians: transform preview 2.6150 ms, scene snapshot refresh
  3.0657 ms, modeling overlay refresh 0.0491 ms, selection-overlay snapshot refresh 3.1339 ms,
  semantic/style build 1.4160 ms, and camera line build 4.2698 ms.
- The final run refreshed the transform-only overlay snapshot 210/210 times, reused translated world
  bounds 84,057 times, emitted the same 4,824 cage lines, changed all 403 target transforms, restored
  the original transform checksum exactly, and reported `inputUnchanged=true`.
- Focused render/overlay/scene/transform/viewport coverage passed 60/60 after a fresh full link. An
  earlier pre-main `0xC0000005` was corrupted concurrent MSVC incremental-link state; deleting only
  the test EXE/ILK/PDB and exclusively relinking fixed it without source rollback.
- Final report: `build/perf/my_first_project_debug_transform_frame_final.json`.

## 2026-07-12 - Qt Captured Fly Relative-Pointer Parity

Invariant:

- Captured right-mouse fly navigation drains retained genuine motion once per viewport input frame.
  A relative raw packet contributes its delta and suppresses its following duplicate Qt move; an
  absolute raw packet selects its following translated Qt delta. Packet-mode changes never discard
  genuine deltas already retained in the current frame.
- Legacy Qt mouse events remain enabled and continue to own button/focus state plus physical local
  coordinates used for picking. Failed raw registration uses accepted Qt deltas after cursor-warp
  suppression while the native filter remains installed to observe capture theft.
- A failed Qt mouse-grab enable must never advertise logical capture or hide/recenter the cursor.
  Verify Win32 transitions through `GetCapture()`; on the portable fallback use the boolean
  operation-success result returned by `QWindow::setMouseGrabEnabled()`. Disable failure still
  clears logical capture.
- Focus loss releases capture immediately and restores the pre-capture cursor position. External
  `WM_CAPTURECHANGED` follows the same release/restore lifecycle but cancels the RMB drag chord
  without synthesizing a click release that could open the context menu. Capture transitions and a
  queued generation check guard against synchronous focus re-entry and stale capture-loss delivery.

Owners:

- `src/platform/raw_mouse_input.*` owns Win32 registration and complete-packet decoding.
- `src/ui/qt_relative_pointer_input.*` owns the Qt native-event bridge, packet-ordered frame
  accumulator, capture transition policy, and warning gate.
- `src/ui/qt_viewport_surface.*` drains captured delta input, preserves Qt picking coordinates, and
  owns the queued capture-loss release/restore lifecycle.

Verification:

- `QtRelativePointerInputTests.*` passes 12/12, including native capture postconditions,
  mixed relative/absolute packet routing, per-frame reset, zero raw packets, and failure policy.
- Focused Qt input, fly camera, and viewport-router coverage passes 61/61.
- The two captured-pointer source guardrails pass with the native `quader` target built
  successfully.

## 2026-07-12 - Qt Widgets / ImGui Feature-Parity Closure

Task/context: compare the Qt Widgets editor against the last ImGui editor baseline at `8d47a86`
and restore user-facing behavior without changing the render-owned overlay policy or replacing
stock Qt Fusion styling.

Invariant:

- Parity covers the full editor workflow rather than visual imitation: the menu/command surface,
  outliner and inspector actions, console and status surfaces, settings descriptors and live
  consumers, one-to-four resizable viewport layouts, maximize/restore, per-viewport view and
  shading controls, navigation gizmo and fly HUD, focus, selection marquee, transform/pivot/array
  tools, context/delete popups, and tool preview/commit/cancel lifecycles.
- Narrow viewport chrome must compact and then hide controls instead of clipping them. Numeric
  tool settings use paired Fusion sliders and spin boxes; both paths dispatch the same representable
  value and floating controls preserve at least the old ImGui four-decimal precision.
- Non-viewport custom keybindings preserve the active tool and current selection state. Captured
  RMB fly navigation consumes retained relative-pointer motion while Qt remains responsible for
  button/focus state and physical picking coordinates.
- ImGui-only chrome, theme colors, and resources are not parity requirements. The Qt shell remains
  stock Fusion with no application stylesheet or custom global palette.
- Modeling and render semantics are not UI migration knobs. In particular, box footprint/cross
  selection remains dashed exactly as in the ImGui baseline, and overlay visibility, ordering,
  depth, blend, picking, and stale-geometry policy remain render-owned and unchanged by this pass.

Verification:

- Focused Qt viewport chrome, numeric tool settings, keybinding dispatch, captured-pointer input,
  input adapter/router, and navigation-gizmo coverage passes 75/75.
- Focused Fusion/UI/version guardrails pass 32/32; dependency-matrix and OOP documentation
  guardrails pass 3/3.
- Full CTest passes five of six executables. The only failure is the pre-existing
  `QdrGeometryTests.QdrGeometryHelpersRemainStable` ray-plane floating-point precision assertion
  (the modeling-domain executable otherwise passes 498/499 tests).
- The native `quader` v0.0.93 target builds successfully. A live stock-Fusion smoke check rendered
  all four viewports, resized both split regions interactively, and restored single-view before
  exit. Continuous RMB holding is not supported by the desktop automation API, so that hardware
  interaction remains covered by the captured-pointer tests and requires human smoke confirmation.

## 2026-07-12 - Four-Quad Native Pane First-Presentation Fix

Task/context: after switching from Single to Four Quad, Top/Front/Side displayed their Fusion
controls but their native Filament surfaces stayed blank until a splitter was dragged. The user
reserved the PC, so diagnosis and verification were source-, test-, and build-based only.

Invariant:

- `ViewportWorkspaceService` remains the layout source of truth. A Qt layout refresh first applies
  visibility to every pane, then assigns geometry to active panes. Newly activated native children
  must never be geometrized while hidden and shown only after their one cached geometry pass.
- `QWidget::createWindowContainer()` exclusively owns its embedded `QWindow` geometry and
  visibility. `QtViewportPanel` sizes the container; it never directly calls `resize()` on the
  embedded `QtViewportSurface`.
- Qt native resize delivery may be deferred. Before workspace render size, camera projection, or
  ground-grid preparation consumes dimensions, `QtViewportSurface` explicitly synchronizes the
  Filament `ViewportRenderer` with the physical-pixel QWindow size. The same guarded synchronization
  runs before readiness and submission; equal dimensions are a no-op.
- Layout placement caching is an optimization only after the activation pass has made every pane
  presentable. No splitter mutation may be required to initialize a newly visible pane.
- This fix changes Qt presentation lifecycle only. Swapchain ownership, render/scene dirty gates,
  grid policy, overlay ordering/depth/blend/picking, and modeling state remain unchanged.

Owners:

- `src/ui/qt_viewport_pane_activation.hpp` owns the visibility-before-geometry ordering primitive.
- `src/ui/qt_viewport_panel.cpp` applies the workspace layout and synchronizes active pane sizes
  before camera/grid preparation.
- `src/ui/qt_viewport_surface.*` owns idempotent physical render-size synchronization.

Verification:

- `QtViewportPaneActivationTests.SingleToFourMakesEveryPanePresentableWithoutSplitMutation` covers
  Single -> Four activation followed by the cached unchanged-layout path without splitter input.
- UI lifecycle guardrails enforce activation ordering, container-owned QWindow geometry, and
  renderer-size synchronization before camera preparation and Filament submission.
- Native GPU first-presentation smoke remains intentionally deferred until the user releases the PC.

## 2026-07-12 - Qt Four-Quad Lower-Pane Navigation Latency Fix

Task/context: Four Quad made RMB pan in Front and Right trail badly while Perspective remained much
more responsive. The Qt migration had changed the old single Filament frame into four native-window
frames per application tick, all driven through one Renderer in fixed Perspective, Top, Front, Side
order. That put the lower panes third and fourth in one frame-pacing queue.

Invariant:

- A Filament `Renderer` represents one operating-system window and owns that window's frame-latency
  state. Every Qt native viewport swapchain therefore owns a dedicated Renderer; render begin,
  viewport draw, end, performance-history access, move/reset, and failure cleanup use that paired
  surface context. The primary RenderSystem APIs remain available for non-Qt presentation paths.
- While `ViewportInputRouter::navigating()` is true, Qt submits only the active
  `interaction_pane_id_`. Unchanged native panes retain their last presented pixels. Release,
  cancel, or capture loss clears navigation before presentation, so every active pane refreshes in
  its original layout order on that same release frame.
- A failed active-pane `beginFrame` remains a zero-submission frame and retries the same pane next
  tick; it never falls through to static panes. A stale interaction pane conservatively falls back
  to all active panes.
- This is presentation scheduling and native-renderer ownership only. Camera math, scene/mesh
  extraction, grid behavior, overlay visibility/order/depth/blend/picking, and modeling state are
  unchanged.

Owners:

- `src/render/render_system.*` owns each native swapchain plus its dedicated Filament Renderer.
- `src/ui/qt_viewport_surface.cpp` presents through the paired native-surface context.
- `src/ui/qt_viewport_render_submission_policy.hpp` owns the camera-navigation submission rule.
- `src/ui/qt_viewport_panel.cpp` supplies router state, interaction pane, and active placements.

Verification:

- Held camera navigation changes native submissions from 4 to 1 per app tick: 3 fewer submissions,
  a 75% reduction. Focused policy coverage proves Front-only submission, no failure fallthrough,
  full release refresh, and stale-pane fallback.
- `RenderSystemTests.*`, `ViewportInputRouterTests.*`, `QtRelativePointerInputTests.*`, and
  `QtViewportRenderSubmissionPolicyTests.*` pass 59/59. The relevant Fusion/UI guardrails pass
  32/32, and the full guardrail executable passes 176/176 when run alone.
- Native `quader` v0.0.95 builds and boots with Vulkan/Fusion. Four Quad presents all panes
  immediately; rapid lower-left and lower-right navigation input updates both panes, and the log
  records four successful paired native surfaces with no errors.
- Full CTest still reproduces the unrelated pre-existing
  `QdrGeometryTests.QdrGeometryHelpersRemainStable` large-dot-product precision failure. Its first
  concurrent guardrail run also hit a temporary recursive-directory race with the SDK-only build;
  the complete standalone guardrail rerun passed. Desktop automation cannot hold RMB continuously,
  so final physical held-pan feel remains a human smoke check.
