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
    constexpr explicit QuaderCommandId(std::string_view value)
        : id(value) {}

    [[nodiscard]] constexpr std::string_view string() const { return id; }
    [[nodiscard]] constexpr operator std::string_view() const { return id; }
};

namespace QuaderCommand {

inline constexpr QuaderCommandId EditUndo{"edit.undo"};
inline constexpr QuaderCommandId EditRedo{"edit.redo"};
inline constexpr QuaderCommandId EditRepeatLastAction{"edit.repeat_last_action"};
inline constexpr QuaderCommandId EditClearSelection{"edit.clear_selection"};
inline constexpr QuaderCommandId EditSelectAll{"edit.select_all"};
inline constexpr QuaderCommandId EditInvertSelection{"edit.invert_selection"};
inline constexpr QuaderCommandId DeleteSelection{"edit.delete"};
inline constexpr QuaderCommandId CopySelection{"edit.copy"};
inline constexpr QuaderCommandId PasteSelection{"edit.paste"};
inline constexpr QuaderCommandId DuplicateSelection{"edit.duplicate"};
inline constexpr QuaderCommandId GroupSelection{"edit.group"};
inline constexpr QuaderCommandId UngroupSelection{"edit.ungroup"};
inline constexpr QuaderCommandId ApplyMaterial{"edit.apply_material"};

inline constexpr QuaderCommandId ProjectNew{"project.new"};
inline constexpr QuaderCommandId ProjectOpen{"project.open"};
inline constexpr QuaderCommandId ProjectSave{"project.save"};
inline constexpr QuaderCommandId ProjectSaveAs{"project.save_as"};

inline constexpr QuaderCommandId HistoryUndo{"edit.undo"};
inline constexpr QuaderCommandId HistoryRedo{"edit.redo"};
inline constexpr QuaderCommandId HistoryRepeatLastAction{"edit.repeat_last_action"};

inline constexpr QuaderCommandId SelectionClear{"edit.clear_selection"};
inline constexpr QuaderCommandId SelectionSelectAll{"edit.select_all"};
inline constexpr QuaderCommandId SelectionInvert{"edit.invert_selection"};

inline constexpr QuaderCommandId ViewportFrameSelection{"viewport.frame_selection"};
inline constexpr QuaderCommandId ViewportFocusOrigin{"viewport.focus_origin"};
inline constexpr QuaderCommandId ViewportSpawnWindow{"viewport.spawn_window"};
inline constexpr QuaderCommandId ViewportViewPerspective{"viewport.view.perspective"};
inline constexpr QuaderCommandId ViewportViewTop{"viewport.view.top"};
inline constexpr QuaderCommandId ViewportViewBottom{"viewport.view.bottom"};
inline constexpr QuaderCommandId ViewportViewLeft{"viewport.view.left"};
inline constexpr QuaderCommandId ViewportViewRight{"viewport.view.right"};
inline constexpr QuaderCommandId ViewportViewFront{"viewport.view.front"};
inline constexpr QuaderCommandId ViewportViewBack{"viewport.view.back"};

inline constexpr QuaderCommandId ModelingDeleteSelection{"edit.delete"};
inline constexpr QuaderCommandId ModelingDuplicateMeshes{"edit.duplicate"};
inline constexpr QuaderCommandId ModelingBridge{"modeling.bridge"};
inline constexpr QuaderCommandId ModelingExtractFaces{"modeling.extract_faces"};
inline constexpr QuaderCommandId ModelingDetachFaces{"modeling.detach_faces"};
inline constexpr QuaderCommandId ModelingSnapVerticesToActive{"modeling.snap_vertices_to_active"};
inline constexpr QuaderCommandId ModelingConnectVertices{"modeling.connect_vertices"};
inline constexpr QuaderCommandId ModelingDissolveEdges{"modeling.dissolve_edges"};
inline constexpr QuaderCommandId ModelingDissolveVertices{"modeling.dissolve_vertices"};
inline constexpr QuaderCommandId ModelingBevelVertices{"modeling.bevel_vertices"};
inline constexpr QuaderCommandId ModelingCombineMeshes{"modeling.combine_meshes"};
inline constexpr QuaderCommandId ModelingMergeVerticesToActive{"modeling.merge_vertices_to_active"};
inline constexpr QuaderCommandId ModelingMergeVerticesToCenter{"modeling.merge_vertices_to_center"};
inline constexpr QuaderCommandId ModelingSnapEdgesToActive{"modeling.snap_edges_to_active"};
inline constexpr QuaderCommandId ModelingConnectEdges{"modeling.connect_edges"};
inline constexpr QuaderCommandId ModelingSplitEdges{"modeling.split_edges"};
inline constexpr QuaderCommandId ModelingMergeEdges{"modeling.merge_edges"};
inline constexpr QuaderCommandId ModelingCollapseEdges{"modeling.collapse_edges"};
inline constexpr QuaderCommandId ModelingFillHole{"modeling.fill_hole"};
inline constexpr QuaderCommandId ModelingCombineFaces{"modeling.combine_faces"};
inline constexpr QuaderCommandId ModelingCollapseFaces{"modeling.collapse_faces"};
inline constexpr QuaderCommandId ModelingRadialAlign{"modeling.radial_align"};
inline constexpr QuaderCommandId ModelingFlipFaceNormals{"modeling.flip_face_normals"};
inline constexpr QuaderCommandId ModelingRecalculateFaceNormals{"modeling.recalculate_face_normals"};
inline constexpr QuaderCommandId ModelingShadeFacesSmooth{"modeling.shade_faces_smooth"};
inline constexpr QuaderCommandId ModelingShadeFacesFlat{"modeling.shade_faces_flat"};
inline constexpr QuaderCommandId ModelingHardenEdgeNormals{"modeling.harden_edge_normals"};
inline constexpr QuaderCommandId ModelingSoftenEdgeNormals{"modeling.soften_edge_normals"};
inline constexpr QuaderCommandId ModelingMirrorSelectionX{"modeling.mirror_selection_x"};
inline constexpr QuaderCommandId ModelingMirrorSelectionY{"modeling.mirror_selection_y"};
inline constexpr QuaderCommandId ModelingMirrorSelectionZ{"modeling.mirror_selection_z"};
inline constexpr QuaderCommandId ModelingInvertMesh{"modeling.invert_mesh"};
inline constexpr QuaderCommandId ModelingShadeSmoothMesh{"modeling.shade_smooth_mesh"};
inline constexpr QuaderCommandId ModelingShadeFlatMesh{"modeling.shade_flat_mesh"};
inline constexpr QuaderCommandId ModelingCombineMesh{"modeling.combine_mesh"};
inline constexpr QuaderCommandId ModelingMergeVertices{"modeling.merge_vertices"};
inline constexpr QuaderCommandId ModelingMergeVerticesAtCenter{"modeling.merge_vertices_at_center"};
inline constexpr QuaderCommandId ModelingRadialAlignVertices{"modeling.radial_align_vertices"};
inline constexpr QuaderCommandId ModelingBridgeEdges{"modeling.bridge_edges"};
inline constexpr QuaderCommandId ModelingRadialAlignEdges{"modeling.radial_align_edges"};
inline constexpr QuaderCommandId ModelingInvertFaces{"modeling.invert_faces"};
inline constexpr QuaderCommandId ModelingBridgeFaces{"modeling.bridge_faces"};
inline constexpr QuaderCommandId ModelingRadialAlignFaces{"modeling.radial_align_faces"};
inline constexpr QuaderCommandId ModelingBevelEdges{"modeling.bevel_edges"};
inline constexpr QuaderCommandId ModelingInterpolatedBridgeEdges{"modeling.interpolated_bridge_edges"};
inline constexpr QuaderCommandId ModelingInterpolatedBridgeFaces{"modeling.interpolated_bridge_faces"};
inline constexpr QuaderCommandId ModelingThickenFaces{"modeling.thicken_faces"};
inline constexpr QuaderCommandId ModelingCreateOuterHull{"modeling.create_outer_hull"};
inline constexpr QuaderCommandId ModelingSliceQuad{"modeling.slice_quad"};
inline constexpr QuaderCommandId ModelingMergeByDistance{"modeling.merge_by_distance"};
inline constexpr QuaderCommandId ModelingFlipHorizontal{"modeling.flip_horizontal"};
inline constexpr QuaderCommandId ModelingFlipVertical{"modeling.flip_vertical"};
inline constexpr QuaderCommandId ModelingApplyActiveMaterial{"edit.apply_material"};
inline constexpr QuaderCommandId ModelingApplyActivePreview{"modeling.apply_active_preview"};
inline constexpr QuaderCommandId ModelingCancelActivePreview{"modeling.cancel_active_preview"};

inline constexpr QuaderCommandId ToolsActivateSelect{"tools.activate.select"};
inline constexpr QuaderCommandId ToolsActivateMove{"tools.activate.move"};
inline constexpr QuaderCommandId ToolsActivateExtend{"tools.activate.extend"};
inline constexpr QuaderCommandId ToolsActivateRotate{"tools.activate.rotate"};
inline constexpr QuaderCommandId ToolsActivateScale{"tools.activate.scale"};
inline constexpr QuaderCommandId ToolsActivateBox{"tools.activate.box"};
inline constexpr QuaderCommandId ToolsActivateExtrude{"tools.activate.extrude"};
inline constexpr QuaderCommandId ToolsActivateInsertEdgeLoop{"tools.activate.insert_edge_loop"};
inline constexpr QuaderCommandId ToolsActivateKnife{"tools.activate.knife"};
inline constexpr QuaderCommandId ToolsActivateCut{"tools.activate.cut"};
inline constexpr QuaderCommandId ToolsActivatePoly{"tools.activate.poly"};
inline constexpr QuaderCommandId ToolsActivateMirror{"tools.activate.mirror"};

inline constexpr QuaderCommandId TypedCommandIds[] = {
        EditUndo,
        EditRedo,
        EditRepeatLastAction,
        EditClearSelection,
        EditSelectAll,
        EditInvertSelection,
        DeleteSelection,
        CopySelection,
        PasteSelection,
        DuplicateSelection,
        GroupSelection,
        UngroupSelection,
        ApplyMaterial,
        ProjectNew,
        ProjectOpen,
        ProjectSave,
        ProjectSaveAs,
        HistoryUndo,
        HistoryRedo,
        HistoryRepeatLastAction,
        SelectionClear,
        SelectionSelectAll,
        SelectionInvert,
        ViewportFrameSelection,
        ViewportFocusOrigin,
        ViewportSpawnWindow,
        ViewportViewPerspective,
        ViewportViewTop,
        ViewportViewBottom,
        ViewportViewLeft,
        ViewportViewRight,
        ViewportViewFront,
        ViewportViewBack,
        ModelingDeleteSelection,
        ModelingDuplicateMeshes,
        ModelingBridge,
        ModelingExtractFaces,
        ModelingDetachFaces,
        ModelingSnapVerticesToActive,
        ModelingConnectVertices,
        ModelingDissolveEdges,
        ModelingDissolveVertices,
        ModelingBevelVertices,
        ModelingCombineMeshes,
        ModelingMergeVerticesToActive,
        ModelingMergeVerticesToCenter,
        ModelingSnapEdgesToActive,
        ModelingConnectEdges,
        ModelingSplitEdges,
        ModelingMergeEdges,
        ModelingCollapseEdges,
        ModelingFillHole,
        ModelingCombineFaces,
        ModelingCollapseFaces,
        ModelingRadialAlign,
        ModelingFlipFaceNormals,
        ModelingRecalculateFaceNormals,
        ModelingShadeFacesSmooth,
        ModelingShadeFacesFlat,
        ModelingHardenEdgeNormals,
        ModelingSoftenEdgeNormals,
        ModelingMirrorSelectionX,
        ModelingMirrorSelectionY,
        ModelingMirrorSelectionZ,
        ModelingInvertMesh,
        ModelingShadeSmoothMesh,
        ModelingShadeFlatMesh,
        ModelingCombineMesh,
        ModelingMergeVertices,
        ModelingMergeVerticesAtCenter,
        ModelingRadialAlignVertices,
        ModelingBridgeEdges,
        ModelingRadialAlignEdges,
        ModelingInvertFaces,
        ModelingBridgeFaces,
        ModelingRadialAlignFaces,
        ModelingBevelEdges,
        ModelingInterpolatedBridgeEdges,
        ModelingInterpolatedBridgeFaces,
        ModelingThickenFaces,
        ModelingCreateOuterHull,
        ModelingSliceQuad,
        ModelingMergeByDistance,
        ModelingFlipHorizontal,
        ModelingFlipVertical,
        ModelingApplyActiveMaterial,
        ModelingApplyActivePreview,
        ModelingCancelActivePreview,
        ToolsActivateSelect,
        ToolsActivateMove,
        ToolsActivateExtend,
        ToolsActivateRotate,
        ToolsActivateScale,
        ToolsActivateBox,
        ToolsActivateExtrude,
        ToolsActivateInsertEdgeLoop,
        ToolsActivateKnife,
        ToolsActivateCut,
        ToolsActivatePoly,
        ToolsActivateMirror,
};

[[nodiscard]] inline constexpr std::span<const QuaderCommandId> typedCommandIds() {
    return std::span<const QuaderCommandId>{TypedCommandIds};
}

} // namespace QuaderCommand

} // namespace quader
