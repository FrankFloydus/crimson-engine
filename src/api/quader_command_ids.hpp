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
inline constexpr QuaderCommandId kEditSelectAll{"edit.select_all"};
inline constexpr QuaderCommandId kEditInvertSelection{"edit.invert_selection"};
inline constexpr QuaderCommandId kDeleteSelection{"edit.delete"};
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

/// Deprecated compatibility spelling. Use kEditUndo for new C++ callers.
inline constexpr QuaderCommandId kHistoryUndo{"edit.undo"};
/// Deprecated compatibility spelling. Use kEditRedo for new C++ callers.
inline constexpr QuaderCommandId kHistoryRedo{"edit.redo"};
/// Deprecated compatibility spelling. Use kEditRepeatLastAction for new C++
/// callers.
inline constexpr QuaderCommandId kHistoryRepeatLastAction{
    "edit.repeat_last_action"};

/// Deprecated compatibility spelling. Use kEditClearSelection for new C++
/// callers.
inline constexpr QuaderCommandId kSelectionClear{"edit.clear_selection"};
/// Deprecated compatibility spelling. Use kEditSelectAll for new C++ callers.
inline constexpr QuaderCommandId kSelectionSelectAll{"edit.select_all"};
/// Deprecated compatibility spelling. Use kEditInvertSelection for new C++
/// callers.
inline constexpr QuaderCommandId kSelectionInvert{"edit.invert_selection"};

inline constexpr QuaderCommandId kViewportFrameSelection{
    "viewport.frame_selection"};
inline constexpr QuaderCommandId kViewportFocusAll{"viewport.focus_all"};
inline constexpr QuaderCommandId kViewportFocusOrigin{"viewport.focus_origin"};
inline constexpr QuaderCommandId kViewportSpawnWindow{"viewport.spawn_window"};
inline constexpr QuaderCommandId kViewportViewPerspective{
    "viewport.view.perspective"};
inline constexpr QuaderCommandId kViewportViewTop{"viewport.view.top"};
inline constexpr QuaderCommandId kViewportViewBottom{"viewport.view.bottom"};
inline constexpr QuaderCommandId kViewportViewLeft{"viewport.view.left"};
inline constexpr QuaderCommandId kViewportViewRight{"viewport.view.right"};
inline constexpr QuaderCommandId kViewportViewFront{"viewport.view.front"};
inline constexpr QuaderCommandId kViewportViewBack{"viewport.view.back"};

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
inline constexpr QuaderCommandId kToolsActivateExtend{"tools.activate.extend"};
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
    kEditSelectAll,
    kEditInvertSelection,
    kDeleteSelection,
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
    kViewportFrameSelection,
    kViewportFocusAll,
    kViewportFocusOrigin,
    kViewportSpawnWindow,
    kViewportViewPerspective,
    kViewportViewTop,
    kViewportViewBottom,
    kViewportViewLeft,
    kViewportViewRight,
    kViewportViewFront,
    kViewportViewBack,
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
    kModelingApplyActivePreview,
    kModelingCancelActivePreview,
    kToolsApplyPrimitiveCreation,
    kToolsCancelPrimitiveCreation,
    kToolsActivateSelect,
    kToolsActivateMove,
    kToolsActivateExtend,
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
