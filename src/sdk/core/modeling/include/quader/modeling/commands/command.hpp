////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/payloads.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace quader::modeling {

/**
 * Describes one SDK command without exposing native app command IDs.
 */
struct CommandDescriptor {
  std::string id;
  std::string label;
  std::string category;
  bool enabled = true;
  std::string disabled_reason;
  bool starts_preview = false;
  bool commits_preview = false;
  bool rebuilds_mesh_on_success = true;
};

/// Identifies one canonical portable modeling semantic.
enum class OperationKind : std::uint8_t {
  Bridge,
  EdgeBevel,
  Thicken,
  SliceQuad,
  MergeByDistance,
  HalfCut,
  RemoveDoubles,
  BevelVertices,
  DuplicateMeshes,
  ConnectVertices,
  DissolveVertices,
  ConnectEdges,
  SplitEdges,
  DissolveEdges,
  MergeEdges,
  CollapseEdges,
  FillHole,
  CombineFaces,
  CollapseFaces,
  RadialAlign,
  FlipFaceNormals,
  ShadeFacesSmooth,
  ShadeFacesFlat,
  HardenEdgeNormals,
  SoftenEdgeNormals,
  CombineMeshes,
  ExtractFaces,
  DetachFaces,
  SnapVerticesToActive,
  SnapEdgesToActive,
  MergeVerticesToActive,
  MergeVerticesToCenter,
  RecalculateFaceNormals,
  AssignMaterialSlot,
  DeleteSelection,
  DefaultEdgeNormals,
};

/// Classifies a portable operation parameter's scalar representation.
enum class OperationParameterKind : std::uint8_t {
  Integer,
  Floating,
  Boolean,
};

/// Describes one portable operation parameter without native UI policy.
struct OperationParameterDescriptor {
  std::string_view id;
  OperationParameterKind kind = OperationParameterKind::Integer;
  double default_value = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  double step = 1.0;
  bool visible = true;
  bool hard_limits = true;
};

/**
 * Owns the stable portable identity, dispatch target, schema, capabilities,
 * and dirty expectation for one native-exposed modeling operation.
 */
struct OperationDescriptor {
  OperationKind kind = OperationKind::Bridge;
  std::string_view id;
  std::string_view dispatch_id;
  std::string_view diagnostic_id;
  bool supports_preview = false;
  bool host_owned = false;
  DirtyFlags dirty_on_change;
  std::array<OperationParameterDescriptor, 4> parameters{};
  std::uint8_t parameter_count = 0;
};

namespace detail {

inline constexpr DirtyFlags kTopologyDirty{
    .topology = true,
    .geometry = true,
    .selection = true,
    .overlays = true,
};

inline constexpr DirtyFlags kGeometryDirty{
    .geometry = true,
    .selection = true,
    .overlays = true,
};

inline constexpr std::array<OperationDescriptor, 36>
    kOperationDescriptors{{
        {OperationKind::Bridge, "bridge", "bridge", "bridge", true, false,
         kTopologyDirty,
         {{{"steps", OperationParameterKind::Integer, 1.0, 1.0, 64.0,
            1.0},
           {"start_tension", OperationParameterKind::Floating, 1.0, 0.0, 3.0,
            0.01},
           {"end_tension", OperationParameterKind::Floating, 1.0, 0.0, 3.0,
            0.01}}},
         3},
        {OperationKind::EdgeBevel, "edge_bevel", "bevel_edges",
         "edge_bevel", true, false, kTopologyDirty,
         {{{"width", OperationParameterKind::Floating, 1.0, 0.0, 512.0,
            0.001},
           {"profile", OperationParameterKind::Floating, 0.5, 0.0, 1.0,
            0.01},
           {"segments", OperationParameterKind::Integer, 1.0, 1.0, 16.0,
            1.0}}},
         3},
        {OperationKind::Thicken, "thicken", "thicken_faces", "thicken",
         true, false, kTopologyDirty,
         {{{"thickness", OperationParameterKind::Floating, 0.25, -4.0, 4.0,
            0.01, true, false},
           {"from_center", OperationParameterKind::Boolean, 0.0, 0.0, 1.0,
            1.0, false}}},
         2},
        {OperationKind::SliceQuad, "slice_quad", "slice_quads",
         "slice_quad", true, false, kTopologyDirty,
         {{{"x_slices", OperationParameterKind::Integer, 1.0, 0.0, 64.0,
            1.0},
           {"y_slices", OperationParameterKind::Integer, 1.0, 0.0, 64.0,
            1.0}}},
         2},
        {OperationKind::MergeByDistance, "merge_by_distance",
         "merge_vertices_by_distance", "merge_by_distance", true, false,
         kTopologyDirty,
         {{{"tolerance", OperationParameterKind::Floating, 0.125, 0.0,
            128.0, 0.001}}},
         1},
        {OperationKind::HalfCut, "half_cut", "plane_cut", "half_cut", true,
         false, kTopologyDirty,
         {{{"axis", OperationParameterKind::Integer, 0.0, 0.0, 2.0, 1.0},
           {"separate_parts", OperationParameterKind::Boolean, 0.0, 0.0, 1.0,
            1.0},
           {"fill_parts", OperationParameterKind::Boolean, 1.0, 0.0, 1.0,
            1.0}}},
         3},
        {OperationKind::RemoveDoubles, "remove_doubles", "remove_doubles",
         "remove_doubles", false, false, kTopologyDirty},
        {OperationKind::BevelVertices, "bevel_vertices", "bevel_vertices",
         "bevel_vertices", false, false, kTopologyDirty},
        {OperationKind::DuplicateMeshes, "duplicate_meshes",
         "duplicate_meshes", "duplicate_meshes", false, false,
         kTopologyDirty},
        {OperationKind::ConnectVertices, "connect_vertices",
         "connect_vertices", "connect_vertices", false, false,
         kTopologyDirty},
        {OperationKind::DissolveVertices, "dissolve_vertices",
         "dissolve_vertices", "dissolve_vertices", false, false,
         kTopologyDirty},
        {OperationKind::ConnectEdges, "connect_edges", "connect_edges",
         "connect_edges", false, false, kTopologyDirty},
        {OperationKind::SplitEdges, "split_edges", "split_edges",
         "split_edges", false, false, kTopologyDirty},
        {OperationKind::DissolveEdges, "dissolve_edges", "dissolve_edges",
         "dissolve_edges", false, false, kTopologyDirty},
        {OperationKind::MergeEdges, "merge_edges", "merge_edges",
         "merge_edges", false, false, kTopologyDirty},
        {OperationKind::CollapseEdges, "collapse_edges", "collapse_edges",
         "collapse_edges", false, false, kTopologyDirty},
        {OperationKind::FillHole, "fill_hole", "fill_hole", "fill_hole",
         false, false, kTopologyDirty},
        {OperationKind::CombineFaces, "combine_faces", "combine_faces",
         "combine_faces", false, false, kTopologyDirty},
        {OperationKind::CollapseFaces, "collapse_faces", "collapse_faces",
         "collapse_faces", false, false, kTopologyDirty},
        {OperationKind::RadialAlign, "radial_align", "radial_align",
         "radial_align", false, false, kGeometryDirty},
        {OperationKind::FlipFaceNormals, "flip_face_normals",
         "flip_face_normals", "flip_face_normals", false, false,
         kGeometryDirty},
        {OperationKind::ShadeFacesSmooth, "shade_faces_smooth",
         "shade_faces_smooth", "shade_faces_smooth", false, false,
         kGeometryDirty},
        {OperationKind::ShadeFacesFlat, "shade_faces_flat",
         "shade_faces_flat", "shade_faces_flat", false, false,
         kGeometryDirty},
        {OperationKind::DefaultEdgeNormals, "default_edge_normals",
         "default_edge_normals", "default_edge_normals", false, false,
         kGeometryDirty},
        {OperationKind::HardenEdgeNormals, "harden_edge_normals",
         "harden_edge_normals", "harden_edge_normals", false, false,
         kGeometryDirty},
        {OperationKind::SoftenEdgeNormals, "soften_edge_normals",
         "soften_edge_normals", "soften_edge_normals", false, false,
         kGeometryDirty},
        {OperationKind::CombineMeshes, "combine_meshes", "combine_meshes",
         "combine_meshes", false, false, kTopologyDirty},
        {OperationKind::ExtractFaces, "extract_faces", "extract_faces",
         "extract_faces", false, false, kTopologyDirty},
        {OperationKind::DetachFaces, "detach_faces", "detach_faces",
         "detach_faces", false, false, kTopologyDirty},
        {OperationKind::SnapVerticesToActive, "snap_vertices_to_active",
         "snap_vertices_to_active", "snap_vertices_to_active", false, false,
         kGeometryDirty},
        {OperationKind::SnapEdgesToActive, "snap_edges_to_active",
         "snap_edges_to_active", "snap_edges_to_active", false, false,
         kGeometryDirty},
        {OperationKind::MergeVerticesToActive, "merge_vertices_to_active",
         "merge_vertices_to_active", "merge_vertices_to_active", false,
         false, kTopologyDirty},
        {OperationKind::MergeVerticesToCenter, "merge_vertices_to_center",
         "merge_vertices_to_center", "merge_vertices_to_center", false,
         false, kTopologyDirty},
        {OperationKind::RecalculateFaceNormals, "recalculate_face_normals",
         "recalculate_face_normals", "recalculate_face_normals", false,
         false, kGeometryDirty},
        {OperationKind::AssignMaterialSlot, "assign_material",
         "assign_face_material_slot", "assign_material", false, true,
         {.materials = true, .overlays = true}},
        {OperationKind::DeleteSelection, "delete_selection",
         "delete_selection", "delete_selection", false, false,
         kTopologyDirty},
    }};

} // namespace detail

[[nodiscard]] inline constexpr std::span<const OperationDescriptor>
operation_descriptors() noexcept {
  return detail::kOperationDescriptors;
}

[[nodiscard]] inline constexpr const OperationDescriptor *
operation_descriptor(OperationKind kind) noexcept {
  for (const OperationDescriptor &descriptor : detail::kOperationDescriptors) {
    if (descriptor.kind == kind) {
      return &descriptor;
    }
  }
  return nullptr;
}

[[nodiscard]] inline constexpr const OperationDescriptor *
operation_descriptor(std::string_view id) noexcept {
  for (const OperationDescriptor &descriptor : detail::kOperationDescriptors) {
    if (descriptor.id == id || descriptor.dispatch_id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

} // namespace quader::modeling
