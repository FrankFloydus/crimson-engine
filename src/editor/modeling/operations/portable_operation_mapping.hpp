////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/operations/operation_schema.hpp>

#include <quader/modeling/commands/command.hpp>

#include <array>
#include <span>
#include <string_view>

namespace quader_editor_core {

/**
 * Maps native presentation identity to the portable operation source of truth.
 * Labels remain native because localization and presentation are host-owned.
 */
struct PortableOperationMapping {
  EditorOperationId native_id = EditorOperationId::Bridge;
  quader::modeling::OperationKind portable_kind =
      quader::modeling::OperationKind::Bridge;
  std::string_view native_label;
};

namespace detail {

inline constexpr std::array<PortableOperationMapping, 36>
    kPortableOperationMappings{{
        {EditorOperationId::Bridge, quader::modeling::OperationKind::Bridge,
         "Bridge"},
        {EditorOperationId::EdgeBevel,
         quader::modeling::OperationKind::EdgeBevel, "Edge Bevel"},
        {EditorOperationId::Thicken, quader::modeling::OperationKind::Thicken,
         "Thicken"},
        {EditorOperationId::SliceQuad,
         quader::modeling::OperationKind::SliceQuad, "Slice Quad"},
        {EditorOperationId::MergeByDistance,
         quader::modeling::OperationKind::MergeByDistance,
         "Merge by Distance"},
        {EditorOperationId::HalfCut, quader::modeling::OperationKind::HalfCut,
         "Half-Cut"},
        {EditorOperationId::RemoveDoubles,
         quader::modeling::OperationKind::RemoveDoubles, "Remove Doubles"},
        {EditorOperationId::BevelVertices,
         quader::modeling::OperationKind::BevelVertices, "Bevel Vertices"},
        {EditorOperationId::DuplicateMeshes,
         quader::modeling::OperationKind::DuplicateMeshes,
         "Duplicate Meshes"},
        {EditorOperationId::ConnectVertices,
         quader::modeling::OperationKind::ConnectVertices,
         "Connect Vertices"},
        {EditorOperationId::DissolveVertices,
         quader::modeling::OperationKind::DissolveVertices,
         "Dissolve Vertices"},
        {EditorOperationId::ConnectEdges,
         quader::modeling::OperationKind::ConnectEdges, "Connect Edges"},
        {EditorOperationId::SplitEdges,
         quader::modeling::OperationKind::SplitEdges, "Split Edges"},
        {EditorOperationId::DissolveEdges,
         quader::modeling::OperationKind::DissolveEdges, "Dissolve Edges"},
        {EditorOperationId::MergeEdges,
         quader::modeling::OperationKind::MergeEdges, "Merge Edges"},
        {EditorOperationId::CollapseEdges,
         quader::modeling::OperationKind::CollapseEdges, "Collapse Edges"},
        {EditorOperationId::FillHole,
         quader::modeling::OperationKind::FillHole, "Fill Hole"},
        {EditorOperationId::CombineFaces,
         quader::modeling::OperationKind::CombineFaces, "Combine Faces"},
        {EditorOperationId::CollapseFaces,
         quader::modeling::OperationKind::CollapseFaces, "Collapse Faces"},
        {EditorOperationId::RadialAlign,
         quader::modeling::OperationKind::RadialAlign, "Radial Align"},
        {EditorOperationId::FlipFaceNormals,
         quader::modeling::OperationKind::FlipFaceNormals,
         "Flip Face Normals"},
        {EditorOperationId::ShadeFacesSmooth,
         quader::modeling::OperationKind::ShadeFacesSmooth,
         "Shade Faces Smooth"},
        {EditorOperationId::ShadeFacesFlat,
         quader::modeling::OperationKind::ShadeFacesFlat,
         "Shade Faces Flat"},
        {EditorOperationId::DefaultEdgeNormals,
         quader::modeling::OperationKind::DefaultEdgeNormals,
         "Set Edge Normals to Default"},
        {EditorOperationId::HardenEdgeNormals,
         quader::modeling::OperationKind::HardenEdgeNormals,
         "Harden Edge Normals"},
        {EditorOperationId::SoftenEdgeNormals,
         quader::modeling::OperationKind::SoftenEdgeNormals,
         "Soften Edge Normals"},
        {EditorOperationId::CombineMeshes,
         quader::modeling::OperationKind::CombineMeshes, "Combine Meshes"},
        {EditorOperationId::ExtractFaces,
         quader::modeling::OperationKind::ExtractFaces, "Extract Faces"},
        {EditorOperationId::DetachFaces,
         quader::modeling::OperationKind::DetachFaces, "Detach Faces"},
        {EditorOperationId::SnapVerticesToActive,
         quader::modeling::OperationKind::SnapVerticesToActive,
         "Snap Vertices To Active"},
        {EditorOperationId::SnapEdgesToActive,
         quader::modeling::OperationKind::SnapEdgesToActive,
         "Snap Edges To Active"},
        {EditorOperationId::MergeVerticesToActive,
         quader::modeling::OperationKind::MergeVerticesToActive,
         "Merge Vertices To Active"},
        {EditorOperationId::MergeVerticesToCenter,
         quader::modeling::OperationKind::MergeVerticesToCenter,
         "Merge Vertices To Center"},
        {EditorOperationId::RecalculateFaceNormals,
         quader::modeling::OperationKind::RecalculateFaceNormals,
         "Recalculate Face Normals"},
        {EditorOperationId::AssignMaterial,
         quader::modeling::OperationKind::AssignMaterialSlot,
         "Apply Material"},
        {EditorOperationId::DeleteSelection,
         quader::modeling::OperationKind::DeleteSelection,
         "Delete Selection"},
    }};

} // namespace detail

[[nodiscard]] inline constexpr std::span<const PortableOperationMapping>
portable_operation_mappings() noexcept {
  return detail::kPortableOperationMappings;
}

[[nodiscard]] inline constexpr const PortableOperationMapping *
portable_operation_mapping(EditorOperationId native_id) noexcept {
  for (const PortableOperationMapping &mapping :
       detail::kPortableOperationMappings) {
    if (mapping.native_id == native_id) {
      return &mapping;
    }
  }
  return nullptr;
}

[[nodiscard]] inline constexpr const quader::modeling::OperationDescriptor *
portable_operation_descriptor(EditorOperationId native_id) noexcept {
  const PortableOperationMapping *mapping =
      portable_operation_mapping(native_id);
  return mapping != nullptr
             ? quader::modeling::operation_descriptor(mapping->portable_kind)
             : nullptr;
}

} // namespace quader_editor_core
