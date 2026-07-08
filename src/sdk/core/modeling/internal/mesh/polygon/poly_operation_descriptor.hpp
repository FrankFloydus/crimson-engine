////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/poly_operation.hpp>

#include <array>
#include <cstddef>
#include <span>

namespace quader_poly {

/**
 * Enumerates the stable descriptor slots for public polygon operations.
 */
enum class PolyOperationKey : std::size_t {
  AssignSelectedFaceMaterialSlot,
  TranslateSelection,
  TransformSelection,
  SnapSelectedVerticesToActive,
  MergeSelectedVerticesToActive,
  MergeSelectedVerticesToCenter,
  MergeSelectedVerticesByDistance,
  RemoveDoubleVertices,
  BevelSelectedVertices,
  ConnectSelectedVertices,
  DissolveSelectedVertices,
  ConnectSelectedEdges,
  SplitSelectedEdges,
  DissolveSelectedEdges,
  MergeSelectedEdges,
  CollapseSelectedEdges,
  FillHoleFromSelectedEdges,
  BevelSelectedEdges,
  BridgeEdgePairs,
  BridgeEdgeBoundaries,
  BridgeSelectedEdges,
  CombineSelectedFaces,
  CollapseSelectedFaces,
  RadialAlignSelection,
  FlipSelectedFaceNormals,
  RecalculateSelectedFaceNormals,
  ShadeSelectedFacesSmooth,
  ShadeSelectedFacesFlat,
  HardenSelectedEdgeNormals,
  SoftenSelectedEdgeNormals,
  DeleteSelection,
  PlaneCut,
  InsertEdgeLoop,
  SliceSelectedQuads,
  KnifeSegment,
  KnifeStroke,
  ExtrudeSelectedElements,
  InsetSelectedElements,
  InsetSelectedFaces,
  ExtrudeSelectedFaces,
  DetachSelectedFaces,
  ExtractSelectedFaces,
  BridgeSelectedFaces,
  ThickenSelectedFaces,
  Count,
};

inline constexpr std::array<PolyOperationMetadata,
                            static_cast<std::size_t>(PolyOperationKey::Count)>
    kPolyOperationDescriptors{{
        {"assign_selected_face_material_slot", "Assign Face Material Slot",
         "AssignMaterialSlotOperation"},
        {"translate_selection", "Translate Selection",
         "TranslateSelectionOperation"},
        {"transform_selection", "Transform Selection",
         "TransformSelectionOperation"},
        {"snap_selected_vertices_to_active", "Snap Selected Vertices To Active",
         "SnapVerticesToActiveOperation"},
        {"merge_selected_vertices_to_active",
         "Merge Selected Vertices To Active", "MergeVerticesToActiveOperation"},
        {"merge_selected_vertices_to_center",
         "Merge Selected Vertices To Center", "MergeVerticesToCenterOperation"},
        {"merge_selected_vertices_by_distance",
         "Merge Selected Vertices By Distance",
         "MergeVerticesByDistanceOperation"},
        {"remove_double_vertices", "Remove Double Vertices",
         "RemoveDoubleVerticesOperation"},
        {"bevel_selected_vertices", "Bevel Selected Vertices",
         "BevelVerticesOperation"},
        {"connect_selected_vertices", "Connect Selected Vertices",
         "ConnectVerticesOperation"},
        {"dissolve_selected_vertices", "Dissolve Selected Vertices",
         "DissolveVerticesOperation"},
        {"connect_selected_edges", "Connect Selected Edges",
         "ConnectEdgesOperation"},
        {"split_selected_edges", "Split Selected Edges", "SplitEdgesOperation"},
        {"dissolve_selected_edges", "Dissolve Selected Edges",
         "DissolveEdgesOperation"},
        {"merge_selected_edges", "Merge Selected Edges", "MergeEdgesOperation"},
        {"collapse_selected_edges", "Collapse Selected Edges",
         "CollapseEdgesOperation"},
        {"fill_hole_from_selected_edges", "Fill Hole From Selected Edges",
         "FillHoleFromEdgesOperation"},
        {"bevel_selected_edges", "Bevel Selected Edges", "BevelEdgesOperation"},
        {"bridge_edge_pairs", "Bridge Edge Pairs", "BridgeEdgePairsOperation"},
        {"bridge_edge_boundaries", "Bridge Edge Boundaries",
         "BridgeEdgeBoundariesOperation"},
        {"bridge_selected_edges", "Bridge Selected Edges",
         "BridgeEdgesOperation"},
        {"combine_selected_faces", "Combine Selected Faces",
         "CombineFacesOperation"},
        {"collapse_selected_faces", "Collapse Selected Faces",
         "CollapseFacesOperation"},
        {"radial_align_selection", "Radial Align Selection",
         "RadialAlignOperation"},
        {"flip_selected_face_normals", "Flip Selected Face Normals",
         "FlipFaceNormalsOperation"},
        {"recalculate_selected_face_normals",
         "Recalculate Selected Face Normals",
         "RecalculateFaceNormalsOperation"},
        {"shade_selected_faces_smooth", "Shade Selected Faces Smooth",
         "ShadeFacesOperation"},
        {"shade_selected_faces_flat", "Shade Selected Faces Flat",
         "ShadeFacesOperation"},
        {"harden_selected_edge_normals", "Harden Selected Edge Normals",
         "EdgeNormalHardnessOperation"},
        {"soften_selected_edge_normals", "Soften Selected Edge Normals",
         "EdgeNormalHardnessOperation"},
        {"delete_selection", "Delete Selection", "DeleteSelectionOperation"},
        {"plane_cut", "Plane Cut", "PlaneCutOperation"},
        {"insert_edge_loop", "Insert Edge Loop", "InsertEdgeLoopOperation"},
        {"slice_selected_quads", "Slice Selected Quads", "SliceQuadsOperation"},
        {"knife_segment", "Knife Segment", "KnifeSegmentOperation"},
        {"knife_stroke", "Knife Stroke", "KnifeStrokeOperation"},
        {"extrude_selected_elements", "Extrude Selected Elements",
         "ExtrudeElementsOperation"},
        {"inset_selected_elements", "Inset Selected Elements",
         "InsetElementsOperation"},
        {"inset_selected_faces", "Inset Selected Faces", "InsetFacesOperation"},
        {"extrude_selected_faces", "Extrude Selected Faces",
         "ExtrudeFacesOperation"},
        {"detach_selected_faces", "Detach Selected Faces",
         "DetachFacesOperation"},
        {"extract_selected_faces", "Extract Selected Faces",
         "ExtractFacesOperation"},
        {"bridge_selected_faces", "Bridge Selected Faces",
         "BridgeFacesOperation"},
        {"thicken_selected_faces", "Thicken Selected Faces",
         "ThickenFacesOperation"},
    }};

[[nodiscard]] inline const PolyOperationMetadata &
poly_operation_descriptor(PolyOperationKey key) {
  return kPolyOperationDescriptors[static_cast<std::size_t>(key)];
}

[[nodiscard]] inline std::span<const PolyOperationMetadata>
poly_operation_descriptors() {
  return kPolyOperationDescriptors;
}

} // namespace quader_poly
