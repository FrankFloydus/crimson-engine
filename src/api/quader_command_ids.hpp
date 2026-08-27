////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <span>
#include <string_view>

namespace quader {

/**
 * Represents a Quader Command ID value used by the public Quader API surface.
 */
struct QuaderCommandId {
  std::string_view id;

  constexpr QuaderCommandId() = default;
  constexpr explicit QuaderCommandId(std::string_view value) : id(value) {}

  [[nodiscard]] constexpr std::string_view string() const { return id; }
  [[nodiscard]] constexpr operator std::string_view() const { return id; }
};

namespace quader_command {

inline constexpr QuaderCommandId kEditUndo{"edit.undo"};
inline constexpr QuaderCommandId kEditRedo{"edit.redo"};
inline constexpr QuaderCommandId kEditRepeatLastAction{
    "edit.repeat_last_action"};
inline constexpr QuaderCommandId kEditClearSelection{"edit.clear_selection"};
inline constexpr QuaderCommandId kDeleteSelection{"edit.delete"};
inline constexpr QuaderCommandId kCutSelection{"edit.cut"};
inline constexpr QuaderCommandId kCopySelection{"edit.copy"};
inline constexpr QuaderCommandId kPasteSelection{"edit.paste"};
inline constexpr QuaderCommandId kDuplicateSelection{"edit.duplicate"};
inline constexpr QuaderCommandId kEditCreateInstance{"edit.create_instance"};
inline constexpr QuaderCommandId kEditFlattenInstance{"edit.flatten_instance"};
inline constexpr QuaderCommandId kEditExitInstanceEdit{
    "edit.exit_instance_edit"};
inline constexpr QuaderCommandId kGroupSelection{"edit.group"};
inline constexpr QuaderCommandId kUngroupSelection{"edit.ungroup"};
inline constexpr QuaderCommandId kApplyMaterial{"edit.apply_material"};

#ifndef NDEBUG
inline constexpr QuaderCommandId kDevCopySelectionJson{
    "dev.copy_selection_json"};
#endif

inline constexpr QuaderCommandId kProjectNew{"project.new"};
inline constexpr QuaderCommandId kProjectOpen{"project.open"};
inline constexpr QuaderCommandId kProjectClose{"project.close"};
inline constexpr QuaderCommandId kProjectExportSelectionObj{
    "project.export_selection_obj"};

inline constexpr QuaderCommandId kSceneDocumentNew{"scene_document.new"};
inline constexpr QuaderCommandId kSceneDocumentOpen{"scene_document.open"};
inline constexpr QuaderCommandId kSceneDocumentSave{"scene_document.save"};
inline constexpr QuaderCommandId kSceneDocumentSaveAs{
    "scene_document.save_as"};
inline constexpr QuaderCommandId kSceneDocumentSaveAll{
    "scene_document.save_all"};
inline constexpr QuaderCommandId kSceneDocumentClose{"scene_document.close"};
inline constexpr QuaderCommandId kSceneDocumentNext{"scene_document.next"};
inline constexpr QuaderCommandId kSceneDocumentPrevious{
    "scene_document.previous"};

inline constexpr QuaderCommandId kSceneCreatePointLight{
    "scene.create_point_light"};
inline constexpr QuaderCommandId kSceneCreateDirectionalLight{
    "scene.create_directional_light"};
inline constexpr QuaderCommandId kSceneLayersRemoveSelection{
    "scene.layers.remove_selection"};
inline constexpr QuaderCommandId kSceneHideSelection{
    "scene.hide_selection"};
inline constexpr QuaderCommandId kSceneUnhideHiddenObjects{
    "scene.unhide_hidden_objects"};

inline constexpr QuaderCommandId kSelectAll{"select.all"};
inline constexpr QuaderCommandId kSelectInverse{"select.inverse"};
inline constexpr QuaderCommandId kSelectToggleBackfaceSelection{
    "select.toggle_backface_selection"};
inline constexpr QuaderCommandId kSelectEdgeDirection{
    "select.edge.direction"};
inline constexpr QuaderCommandId kSelectEdgeLoops{"select.edge.loops"};
inline constexpr QuaderCommandId kSelectEdgeRing{"select.edge.ring"};
inline constexpr QuaderCommandId kSelectFacesWithSameMaterial{
    "select.face.same_material"};
inline constexpr QuaderCommandId kSelectCoplanarFaces{
    "select.face.coplanar"};
inline constexpr QuaderCommandId kSelectFaceNormal{"select.face.normal"};
inline constexpr QuaderCommandId kSelectMeshNGons{"select.mesh.ngons"};
inline constexpr QuaderCommandId kSelectSimilarMeshes{"select.mesh.similar"};
inline constexpr QuaderCommandId kSelectGrow{"select.grow"};
inline constexpr QuaderCommandId kSelectShrink{"select.shrink"};

/// Deprecated compatibility spelling. Use kEditUndo for new C++ callers.
inline constexpr QuaderCommandId kHistoryUndo{"edit.undo"};
/// Deprecated compatibility spelling. Use kEditRedo for new C++ callers.
inline constexpr QuaderCommandId kHistoryRedo{"edit.redo"};
/// Deprecated compatibility spelling. Use kEditRepeatLastAction for new C++
/// callers.
inline constexpr QuaderCommandId kHistoryRepeatLastAction{
    "edit.repeat_last_action"};

inline constexpr QuaderCommandId kViewportFrameSelection{
    "viewport.frame_selection"};
inline constexpr QuaderCommandId kViewportFocusAll{"viewport.focus_all"};
inline constexpr QuaderCommandId kViewportFocusOrigin{"viewport.focus_origin"};
inline constexpr QuaderCommandId kViewportSpawnWindow{"viewport.spawn_window"};
inline constexpr QuaderCommandId kViewportToggleLocalVisibility{
    "viewport.toggle_local_visibility"};
inline constexpr QuaderCommandId kViewportToggleTransformSpace{
    "viewport.toggle_transform_space"};
inline constexpr QuaderCommandId kViewportViewPerspective{
    "viewport.view.perspective"};
inline constexpr QuaderCommandId kViewportViewTop{"viewport.view.top"};
inline constexpr QuaderCommandId kViewportViewBottom{"viewport.view.bottom"};
inline constexpr QuaderCommandId kViewportViewLeft{"viewport.view.left"};
inline constexpr QuaderCommandId kViewportViewRight{"viewport.view.right"};
inline constexpr QuaderCommandId kViewportViewFront{"viewport.view.front"};
inline constexpr QuaderCommandId kViewportViewBack{"viewport.view.back"};
inline constexpr QuaderCommandId kViewportToggleGrid{"viewport.toggle_grid"};
inline constexpr QuaderCommandId kViewportToggleGizmos{
    "viewport.toggle_gizmos"};
inline constexpr QuaderCommandId kViewportToggleMeshGrid{
    "viewport.toggle_mesh_grid"};
inline constexpr QuaderCommandId kViewportToggleOverlays{
    "viewport.toggle_overlays"};
inline constexpr QuaderCommandId kViewportToggleTextureLock{
    "viewport.toggle_texture_lock"};
inline constexpr QuaderCommandId kViewportToggleTextureLockScale{
    "viewport.toggle_texture_lock_scale"};
inline constexpr QuaderCommandId kViewportShadingWireframe{
    "viewport.shading.wireframe"};
inline constexpr QuaderCommandId kViewportShadingShaded{
    "viewport.shading.shaded"};
inline constexpr QuaderCommandId kViewportShadingRendered{
    "viewport.shading.rendered"};
inline constexpr QuaderCommandId kViewportLayoutSingle{
    "viewport.layout.single"};
inline constexpr QuaderCommandId kViewportLayoutTwoVertical{
    "viewport.layout.two_vertical"};
inline constexpr QuaderCommandId kViewportLayoutTwoHorizontal{
    "viewport.layout.two_horizontal"};
inline constexpr QuaderCommandId kViewportLayoutFourQuad{
    "viewport.layout.four_quad"};
inline constexpr QuaderCommandId kViewportGridIncrement{
    "viewport.grid.increment"};
inline constexpr QuaderCommandId kViewportGridDecrement{
    "viewport.grid.decrement"};
inline constexpr QuaderCommandId kViewportGridReset{
    "viewport.grid.reset"};

/// Deprecated compatibility spelling. Use kDeleteSelection for new C++ callers.
inline constexpr QuaderCommandId kModelingDeleteSelection{"edit.delete"};
inline constexpr QuaderCommandId kModelingDuplicateMeshes{
    "modeling.duplicate_meshes"};
inline constexpr QuaderCommandId kModelingBridge{"modeling.bridge"};
inline constexpr QuaderCommandId kModelingExtractFaces{
    "modeling.extract_faces"};
inline constexpr QuaderCommandId kModelingDetachFaces{"modeling.detach_faces"};
inline constexpr QuaderCommandId kModelingSnapVerticesToActive{
    "modeling.snap_vertices_to_active"};
inline constexpr QuaderCommandId kModelingConnectVertices{
    "modeling.connect_vertices"};
inline constexpr QuaderCommandId kModelingDissolveEdges{
    "modeling.dissolve_edges"};
inline constexpr QuaderCommandId kModelingDissolveVertices{
    "modeling.dissolve_vertices"};
inline constexpr QuaderCommandId kModelingBevelVertices{
    "modeling.bevel_vertices"};
inline constexpr QuaderCommandId kModelingCombineMeshes{
    "modeling.combine_meshes"};
inline constexpr QuaderCommandId kModelingCopyPivotFromActive{
    "modeling.copy_pivot_from_active"};
inline constexpr QuaderCommandId kModelingSnapMeshesToFloor{
    "modeling.snap_meshes_to_floor"};
inline constexpr QuaderCommandId kModelingSnapVerticesToFloor{
    "modeling.snap_vertices_to_floor"};
inline constexpr QuaderCommandId kModelingMergeVerticesToActive{
    "modeling.merge_vertices_to_active"};
inline constexpr QuaderCommandId kModelingMergeVerticesToCenter{
    "modeling.merge_vertices_to_center"};
inline constexpr QuaderCommandId kModelingRemoveDoubles{
    "modeling.remove_doubles"};
inline constexpr QuaderCommandId kModelingSnapEdgesToActive{
    "modeling.snap_edges_to_active"};
inline constexpr QuaderCommandId kModelingConnectEdges{
    "modeling.connect_edges"};
inline constexpr QuaderCommandId kModelingSplitEdges{"modeling.split_edges"};
inline constexpr QuaderCommandId kModelingMergeEdges{"modeling.merge_edges"};
inline constexpr QuaderCommandId kModelingCollapseEdges{
    "modeling.collapse_edges"};
inline constexpr QuaderCommandId kModelingFillHole{"modeling.fill_hole"};
inline constexpr QuaderCommandId kModelingCombineFaces{
    "modeling.combine_faces"};
inline constexpr QuaderCommandId kModelingCollapseFaces{
    "modeling.collapse_faces"};
inline constexpr QuaderCommandId kModelingRadialAlign{"modeling.radial_align"};
inline constexpr QuaderCommandId kModelingFlipFaceNormals{
    "modeling.flip_face_normals"};
inline constexpr QuaderCommandId kModelingRecalculateFaceNormals{
    "modeling.recalculate_face_normals"};
inline constexpr QuaderCommandId kModelingShadeFacesSmooth{
    "modeling.shade_faces_smooth"};
inline constexpr QuaderCommandId kModelingShadeFacesFlat{
    "modeling.shade_faces_flat"};
inline constexpr QuaderCommandId kModelingHardenEdgeNormals{
    "modeling.harden_edge_normals"};
inline constexpr QuaderCommandId kModelingSoftenEdgeNormals{
    "modeling.soften_edge_normals"};
inline constexpr QuaderCommandId kModelingAlignXMin{"modeling.align.x.min"};
inline constexpr QuaderCommandId kModelingAlignXCenter{
    "modeling.align.x.center"};
inline constexpr QuaderCommandId kModelingAlignXMax{"modeling.align.x.max"};
inline constexpr QuaderCommandId kModelingAlignYMin{"modeling.align.y.min"};
inline constexpr QuaderCommandId kModelingAlignYCenter{
    "modeling.align.y.center"};
inline constexpr QuaderCommandId kModelingAlignYMax{"modeling.align.y.max"};
inline constexpr QuaderCommandId kModelingAlignZMin{"modeling.align.z.min"};
inline constexpr QuaderCommandId kModelingAlignZCenter{
    "modeling.align.z.center"};
inline constexpr QuaderCommandId kModelingAlignZMax{"modeling.align.z.max"};
inline constexpr QuaderCommandId kModelingDistributeCentersX{
    "modeling.distribute.centers.x"};
inline constexpr QuaderCommandId kModelingDistributeCentersY{
    "modeling.distribute.centers.y"};
inline constexpr QuaderCommandId kModelingDistributeCentersZ{
    "modeling.distribute.centers.z"};
inline constexpr QuaderCommandId kModelingDistributeGapsX{
    "modeling.distribute.gaps.x"};
inline constexpr QuaderCommandId kModelingDistributeGapsY{
    "modeling.distribute.gaps.y"};
inline constexpr QuaderCommandId kModelingDistributeGapsZ{
    "modeling.distribute.gaps.z"};
inline constexpr QuaderCommandId kModelingMirrorSelectionX{
    "modeling.mirror_selection_x"};
inline constexpr QuaderCommandId kModelingMirrorSelectionY{
    "modeling.mirror_selection_y"};
inline constexpr QuaderCommandId kModelingMirrorSelectionZ{
    "modeling.mirror_selection_z"};
inline constexpr QuaderCommandId kModelingInvertMesh{"modeling.invert_mesh"};
inline constexpr QuaderCommandId kModelingShadeSmoothMesh{
    "modeling.shade_smooth_mesh"};
inline constexpr QuaderCommandId kModelingShadeFlatMesh{
    "modeling.shade_flat_mesh"};
inline constexpr QuaderCommandId kModelingCombineMesh{"modeling.combine_mesh"};
inline constexpr QuaderCommandId kModelingMergeVertices{
    "modeling.merge_vertices"};
inline constexpr QuaderCommandId kModelingMergeVerticesAtCenter{
    "modeling.merge_vertices_at_center"};
inline constexpr QuaderCommandId kModelingRadialAlignVertices{
    "modeling.radial_align_vertices"};
inline constexpr QuaderCommandId kModelingBridgeEdges{"modeling.bridge_edges"};
inline constexpr QuaderCommandId kModelingRadialAlignEdges{
    "modeling.radial_align_edges"};
inline constexpr QuaderCommandId kModelingInvertFaces{"modeling.invert_faces"};
inline constexpr QuaderCommandId kModelingBridgeFaces{"modeling.bridge_faces"};
inline constexpr QuaderCommandId kModelingRadialAlignFaces{
    "modeling.radial_align_faces"};
inline constexpr QuaderCommandId kModelingBevelEdges{"modeling.bevel_edges"};
inline constexpr QuaderCommandId kModelingInterpolatedBridgeEdges{
    "modeling.interpolated_bridge_edges"};
inline constexpr QuaderCommandId kModelingInterpolatedBridgeFaces{
    "modeling.interpolated_bridge_faces"};
inline constexpr QuaderCommandId kModelingThickenFaces{
    "modeling.thicken_faces"};
inline constexpr QuaderCommandId kModelingCreateOuterHull{
    "modeling.create_outer_hull"};
inline constexpr QuaderCommandId kModelingHalfCut{"modeling.half_cut"};
inline constexpr QuaderCommandId kModelingSliceQuad{"modeling.slice_quad"};
inline constexpr QuaderCommandId kModelingMergeByDistance{
    "modeling.merge_by_distance"};
inline constexpr QuaderCommandId kModelingFlipHorizontal{
    "modeling.flip_horizontal"};
inline constexpr QuaderCommandId kModelingFlipVertical{
    "modeling.flip_vertical"};
inline constexpr QuaderCommandId kModelingTextureAlignToGrid{
    "modeling.texture.align_to_grid"};
inline constexpr QuaderCommandId kModelingTextureAlignToFace{
    "modeling.texture.align_to_face"};
inline constexpr QuaderCommandId kModelingTextureAlignToView{
    "modeling.texture.align_to_view"};
inline constexpr QuaderCommandId kModelingTextureScaleXUp{
    "modeling.texture.scale_x_up"};
inline constexpr QuaderCommandId kModelingTextureScaleXDown{
    "modeling.texture.scale_x_down"};
inline constexpr QuaderCommandId kModelingTextureScaleYUp{
    "modeling.texture.scale_y_up"};
inline constexpr QuaderCommandId kModelingTextureScaleYDown{
    "modeling.texture.scale_y_down"};
inline constexpr QuaderCommandId kModelingTextureShiftLeft{
    "modeling.texture.shift_left"};
inline constexpr QuaderCommandId kModelingTextureShiftRight{
    "modeling.texture.shift_right"};
inline constexpr QuaderCommandId kModelingTextureShiftUp{
    "modeling.texture.shift_up"};
inline constexpr QuaderCommandId kModelingTextureShiftDown{
    "modeling.texture.shift_down"};
inline constexpr QuaderCommandId kModelingTextureRotateClockwise{
    "modeling.texture.rotate_clockwise"};
inline constexpr QuaderCommandId kModelingTextureRotateCounterClockwise{
    "modeling.texture.rotate_counter_clockwise"};
inline constexpr QuaderCommandId kModelingTextureFlipTextureHorizontal{
    "modeling.texture.flip_texture_horizontal"};
inline constexpr QuaderCommandId kModelingTextureFlipTextureVertical{
    "modeling.texture.flip_texture_vertical"};
inline constexpr QuaderCommandId kModelingTextureFlipFaceHorizontal{
    "modeling.texture.flip_face_horizontal"};
inline constexpr QuaderCommandId kModelingTextureFlipFaceVertical{
    "modeling.texture.flip_face_vertical"};
inline constexpr QuaderCommandId kModelingTextureFitBoth{
    "modeling.texture.fit_both"};
inline constexpr QuaderCommandId kModelingTextureFitX{
    "modeling.texture.fit_x"};
inline constexpr QuaderCommandId kModelingTextureFitY{
    "modeling.texture.fit_y"};
inline constexpr QuaderCommandId kModelingTextureJustifyLeft{
    "modeling.texture.justify_left"};
inline constexpr QuaderCommandId kModelingTextureJustifyRight{
    "modeling.texture.justify_right"};
inline constexpr QuaderCommandId kModelingTextureJustifyTop{
    "modeling.texture.justify_top"};
inline constexpr QuaderCommandId kModelingTextureJustifyBottom{
    "modeling.texture.justify_bottom"};
inline constexpr QuaderCommandId kModelingTextureJustifyCenter{
    "modeling.texture.justify_center"};
/// Deprecated compatibility spelling. Use kApplyMaterial for new C++ callers.
inline constexpr QuaderCommandId kModelingApplyActiveMaterial{
    "edit.apply_material"};
inline constexpr QuaderCommandId kModelingApplyActivePreview{
    "modeling.apply_active_preview"};
inline constexpr QuaderCommandId kModelingCancelActivePreview{
    "modeling.cancel_active_preview"};
inline constexpr QuaderCommandId kToolsApplyPrimitiveCreation{
    "tools.primitive.apply"};
inline constexpr QuaderCommandId kToolsCancelPrimitiveCreation{
    "tools.primitive.cancel"};

inline constexpr QuaderCommandId kToolsActivateSelect{"tools.activate.select"};
inline constexpr QuaderCommandId kToolsActivateMove{"tools.activate.move"};
inline constexpr QuaderCommandId kToolsActivateRotate{"tools.activate.rotate"};
inline constexpr QuaderCommandId kToolsActivateScale{"tools.activate.scale"};
inline constexpr QuaderCommandId kToolsActivateCreatePrimitive{
    "tools.activate.create_primitive"};
inline constexpr QuaderCommandId kToolsActivateExtrude{
    "tools.activate.extrude"};
inline constexpr QuaderCommandId kToolsActivateInsertEdgeLoop{
    "tools.activate.insert_edge_loop"};
inline constexpr QuaderCommandId kToolsActivateKnife{"tools.activate.knife"};
inline constexpr QuaderCommandId kToolsActivateCut{"tools.activate.cut"};
inline constexpr QuaderCommandId kToolsActivatePoly{"tools.activate.poly"};
inline constexpr QuaderCommandId kToolsActivateMirror{"tools.activate.mirror"};
inline constexpr QuaderCommandId kToolsActivatePivot{"tools.activate.pivot"};
inline constexpr QuaderCommandId kToolsActivateLinearArray{
    "tools.activate.linear_array"};
inline constexpr QuaderCommandId kToolsActivateRadialArray{
    "tools.activate.radial_array"};

inline constexpr QuaderCommandId kTypedCommandIds[] = {
    kEditUndo,
    kEditRedo,
    kEditRepeatLastAction,
    kEditClearSelection,
    kSelectAll,
    kSelectInverse,
    kSelectToggleBackfaceSelection,
    kSelectEdgeDirection,
    kSelectEdgeLoops,
    kSelectEdgeRing,
    kSelectFacesWithSameMaterial,
    kSelectCoplanarFaces,
    kSelectFaceNormal,
    kSelectMeshNGons,
    kSelectSimilarMeshes,
    kSelectGrow,
    kSelectShrink,
    kDeleteSelection,
    kCutSelection,
    kCopySelection,
    kPasteSelection,
    kDuplicateSelection,
    kEditCreateInstance,
    kEditFlattenInstance,
    kEditExitInstanceEdit,
    kGroupSelection,
    kUngroupSelection,
    kApplyMaterial,
#ifndef NDEBUG
    kDevCopySelectionJson,
#endif
    kProjectNew,
    kProjectOpen,
    kProjectClose,
    kProjectExportSelectionObj,
    kSceneDocumentNew,
    kSceneDocumentOpen,
    kSceneDocumentSave,
    kSceneDocumentSaveAs,
    kSceneDocumentSaveAll,
    kSceneDocumentClose,
    kSceneDocumentNext,
    kSceneDocumentPrevious,
    kSceneCreatePointLight,
    kSceneCreateDirectionalLight,
    kSceneLayersRemoveSelection,
    kSceneHideSelection,
    kSceneUnhideHiddenObjects,
    kViewportFrameSelection,
    kViewportFocusAll,
    kViewportFocusOrigin,
    kViewportSpawnWindow,
    kViewportToggleLocalVisibility,
    kViewportToggleTransformSpace,
    kViewportViewPerspective,
    kViewportViewTop,
    kViewportViewBottom,
    kViewportViewLeft,
    kViewportViewRight,
    kViewportViewFront,
    kViewportViewBack,
    kViewportToggleGrid,
    kViewportToggleGizmos,
    kViewportToggleMeshGrid,
    kViewportToggleOverlays,
    kViewportToggleTextureLock,
    kViewportToggleTextureLockScale,
    kViewportShadingWireframe,
    kViewportShadingShaded,
    kViewportShadingRendered,
    kViewportLayoutSingle,
    kViewportLayoutTwoVertical,
    kViewportLayoutTwoHorizontal,
    kViewportLayoutFourQuad,
    kViewportGridIncrement,
    kViewportGridDecrement,
    kViewportGridReset,
    kModelingDuplicateMeshes,
    kModelingBridge,
    kModelingExtractFaces,
    kModelingDetachFaces,
    kModelingSnapVerticesToActive,
    kModelingConnectVertices,
    kModelingDissolveEdges,
    kModelingDissolveVertices,
    kModelingBevelVertices,
    kModelingCombineMeshes,
    kModelingCopyPivotFromActive,
    kModelingSnapMeshesToFloor,
    kModelingSnapVerticesToFloor,
    kModelingMergeVerticesToActive,
    kModelingMergeVerticesToCenter,
    kModelingRemoveDoubles,
    kModelingSnapEdgesToActive,
    kModelingConnectEdges,
    kModelingSplitEdges,
    kModelingMergeEdges,
    kModelingCollapseEdges,
    kModelingFillHole,
    kModelingCombineFaces,
    kModelingCollapseFaces,
    kModelingRadialAlign,
    kModelingFlipFaceNormals,
    kModelingRecalculateFaceNormals,
    kModelingShadeFacesSmooth,
    kModelingShadeFacesFlat,
    kModelingHardenEdgeNormals,
    kModelingSoftenEdgeNormals,
    kModelingAlignXMin,
    kModelingAlignXCenter,
    kModelingAlignXMax,
    kModelingAlignYMin,
    kModelingAlignYCenter,
    kModelingAlignYMax,
    kModelingAlignZMin,
    kModelingAlignZCenter,
    kModelingAlignZMax,
    kModelingDistributeCentersX,
    kModelingDistributeCentersY,
    kModelingDistributeCentersZ,
    kModelingDistributeGapsX,
    kModelingDistributeGapsY,
    kModelingDistributeGapsZ,
    kModelingMirrorSelectionX,
    kModelingMirrorSelectionY,
    kModelingMirrorSelectionZ,
    kModelingInvertMesh,
    kModelingShadeSmoothMesh,
    kModelingShadeFlatMesh,
    kModelingCombineMesh,
    kModelingMergeVertices,
    kModelingMergeVerticesAtCenter,
    kModelingRadialAlignVertices,
    kModelingBridgeEdges,
    kModelingRadialAlignEdges,
    kModelingInvertFaces,
    kModelingBridgeFaces,
    kModelingRadialAlignFaces,
    kModelingBevelEdges,
    kModelingInterpolatedBridgeEdges,
    kModelingInterpolatedBridgeFaces,
    kModelingThickenFaces,
    kModelingCreateOuterHull,
    kModelingHalfCut,
    kModelingSliceQuad,
    kModelingMergeByDistance,
    kModelingFlipHorizontal,
    kModelingFlipVertical,
    kModelingTextureAlignToGrid,
    kModelingTextureAlignToFace,
    kModelingTextureAlignToView,
    kModelingTextureScaleXUp,
    kModelingTextureScaleXDown,
    kModelingTextureScaleYUp,
    kModelingTextureScaleYDown,
    kModelingTextureShiftLeft,
    kModelingTextureShiftRight,
    kModelingTextureShiftUp,
    kModelingTextureShiftDown,
    kModelingTextureRotateClockwise,
    kModelingTextureRotateCounterClockwise,
    kModelingTextureFlipTextureHorizontal,
    kModelingTextureFlipTextureVertical,
    kModelingTextureFlipFaceHorizontal,
    kModelingTextureFlipFaceVertical,
    kModelingTextureFitBoth,
    kModelingTextureFitX,
    kModelingTextureFitY,
    kModelingTextureJustifyLeft,
    kModelingTextureJustifyRight,
    kModelingTextureJustifyTop,
    kModelingTextureJustifyBottom,
    kModelingTextureJustifyCenter,
    kModelingApplyActivePreview,
    kModelingCancelActivePreview,
    kToolsApplyPrimitiveCreation,
    kToolsCancelPrimitiveCreation,
    kToolsActivateSelect,
    kToolsActivateMove,
    kToolsActivateRotate,
    kToolsActivateScale,
    kToolsActivateCreatePrimitive,
    kToolsActivateExtrude,
    kToolsActivateInsertEdgeLoop,
    kToolsActivateKnife,
    kToolsActivateCut,
    kToolsActivatePoly,
    kToolsActivateMirror,
    kToolsActivatePivot,
    kToolsActivateLinearArray,
    kToolsActivateRadialArray,
};

[[nodiscard]] inline constexpr std::span<const QuaderCommandId>
typed_command_ids() {
  return std::span<const QuaderCommandId>{kTypedCommandIds};
}

} // namespace quader_command

} // namespace quader
