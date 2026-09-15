////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"

#include "editor/modeling/services/modeling_operation_settings.hpp"

#include <algorithm>
#include <array>

namespace quader {
namespace {

[[nodiscard]] ModelingInstantOperationCatalogEntry
runtime_entry(ModelingInstantOperationId operation, std::string_view id,
              std::string_view command_label, std::string_view live_label,
              ModelingActionAvailabilityRule availability,
              quader_editor_core::EditorOperationId runtime_operation,
              std::string_view public_command_id = {},
              bool registers_public_command = true) {
  return {
      .operation = operation,
      .id = id,
      .operation_id = id,
      .command_label = command_label,
      .live_label = live_label,
      .availability = availability,
      .runtime_operation = runtime_operation,
      .public_command_id = public_command_id,
      .registers_public_command = registers_public_command,
  };
}

[[nodiscard]] ModelingActionAvailabilityRule
mode_selection(ModelingSelectionMode mode, std::uint32_t minimum_objects = 0,
               std::uint32_t minimum_vertices = 0,
               std::uint32_t minimum_edges = 0, std::uint32_t minimum_faces = 0,
               ModelingActiveSelectionKind required_active =
                   ModelingActiveSelectionKind::None) {
  return {
      .requirement = ModelingActionSelectionRequirement::SelectionMode,
      .mode = mode,
      .minimum_selected_vertices = minimum_vertices,
      .minimum_selected_edges = minimum_edges,
      .minimum_selected_faces = minimum_faces,
      .minimum_selected_objects = minimum_objects,
      .required_active_kind = required_active,
  };
}

[[nodiscard]] ModelingActionAvailabilityRule
object_selection(std::uint32_t minimum_objects = 1) {
  return mode_selection(ModelingSelectionMode::Object, minimum_objects);
}

[[nodiscard]] ModelingActionAvailabilityRule
object_or_group_selection(std::uint32_t minimum_objects = 1) {
  return {
      .requirement =
          ModelingActionSelectionRequirement::ObjectOrGroupSelection,
      .minimum_selected_objects = minimum_objects,
  };
}

[[nodiscard]] ModelingActionAvailabilityRule
vertex_selection(std::uint32_t minimum_vertices = 1,
                 ModelingActiveSelectionKind required_active =
                     ModelingActiveSelectionKind::None) {
  return mode_selection(ModelingSelectionMode::Vertex, 0, minimum_vertices, 0,
                        0, required_active);
}

[[nodiscard]] ModelingActionAvailabilityRule
edge_selection(std::uint32_t minimum_edges = 1,
               ModelingActiveSelectionKind required_active =
                   ModelingActiveSelectionKind::None) {
  return mode_selection(ModelingSelectionMode::Edge, 0, 0, minimum_edges, 0,
                        required_active);
}

[[nodiscard]] ModelingActionAvailabilityRule
face_selection(std::uint32_t minimum_faces = 1) {
  return mode_selection(ModelingSelectionMode::Face, 0, 0, 0, minimum_faces);
}

[[nodiscard]] ModelingActionAvailabilityRule edge_or_face_selection() {
  return {.requirement =
              ModelingActionSelectionRequirement::EdgeOrFaceSelection};
}

[[nodiscard]] ModelingActionAvailabilityRule component_selection() {
  return {.requirement =
              ModelingActionSelectionRequirement::ComponentSelection};
}

[[nodiscard]] ModelingActionAvailabilityRule object_or_face_selection() {
  return {.requirement =
              ModelingActionSelectionRequirement::ObjectOrFaceSelection};
}

[[nodiscard]] ModelingActionAvailabilityRule any_selection() {
  return {.requirement = ModelingActionSelectionRequirement::AnySelection};
}

} // namespace

std::span<const ModelingInstantOperationCatalogEntry>
modeling_instant_operation_catalog() {
  using RuntimeId = quader_editor_core::EditorOperationId;

  static const std::array kEntries{
      runtime_entry(ModelingInstantOperationId::DuplicateMeshes,
                    "duplicate_meshes", "Duplicate Meshes",
                    "Duplicate Meshes",
                    object_or_group_selection(), RuntimeId::DuplicateMeshes),
      runtime_entry(ModelingInstantOperationId::CombineMeshes, "combine_meshes",
                    "Combine Meshes", "Combine Meshes",
                    object_selection(2),
                    RuntimeId::CombineMeshes),
      runtime_entry(ModelingInstantOperationId::ExtractFaces, "extract_faces",
                    "Extract Faces", "Extract Faces",
                    face_selection(),
                    RuntimeId::ExtractFaces),
      runtime_entry(ModelingInstantOperationId::DetachFaces, "detach_faces",
                    "Detach Faces", "Detach Faces",
                    face_selection(),
                    RuntimeId::DetachFaces),
      runtime_entry(ModelingInstantOperationId::Bridge, "bridge",
                    "Bridge Edges or Faces",
                    "Bridge Edges or Faces",
                    edge_or_face_selection(), RuntimeId::Bridge),
      runtime_entry(ModelingInstantOperationId::SnapVerticesToActive,
                    "snap_vertices_to_active",
                    "Snap Vertices to Active",
                    "Snap Vertices to Active",
                    vertex_selection(2, ModelingActiveSelectionKind::Vertex),
                    RuntimeId::SnapVerticesToActive),
      runtime_entry(ModelingInstantOperationId::ConnectVertices,
                    "connect_vertices", "Connect Vertices",
                    "Connect Vertices",
                    vertex_selection(2), RuntimeId::ConnectVertices),
      runtime_entry(ModelingInstantOperationId::DissolveVertices,
                    "dissolve_vertices", "Dissolve Vertices",
                    "Dissolve Vertices", vertex_selection(),
                    RuntimeId::DissolveVertices),
      runtime_entry(ModelingInstantOperationId::BevelVertices, "bevel_vertices",
                    "Bevel Vertices", "Bevel Vertices",
                    vertex_selection(),
                    RuntimeId::BevelVertices),
      runtime_entry(ModelingInstantOperationId::MergeVerticesToActive,
                    "merge_vertices_to_active",
                    "Merge Vertices to Active",
                    "Merge Vertices to Active",
                    vertex_selection(2),
                    RuntimeId::MergeVerticesToActive),
      runtime_entry(ModelingInstantOperationId::MergeVerticesToCenter,
                    "merge_vertices_to_center",
                    "Merge Vertices at Center",
                    "Merge Vertices at Center", vertex_selection(2),
                    RuntimeId::MergeVerticesToCenter),
      runtime_entry(ModelingInstantOperationId::RemoveDoubles, "remove_doubles",
                    "Remove Doubles", "Remove Doubles", any_selection(),
                    RuntimeId::RemoveDoubles, "modeling.remove_doubles"),
      runtime_entry(ModelingInstantOperationId::SnapEdgesToActive,
                    "snap_edges_to_active",
                    "Snap Edges to Active",
                    "Snap Edges to Active",
                    edge_selection(2, ModelingActiveSelectionKind::Edge),
                    RuntimeId::SnapEdgesToActive),
      runtime_entry(ModelingInstantOperationId::ConnectEdges, "connect_edges",
                    "Connect Edges", "Connect Edges",
                    edge_selection(2),
                    RuntimeId::ConnectEdges),
      runtime_entry(ModelingInstantOperationId::SplitEdges, "split_edges",
                    "Split Edges", "Split Edges",
                    edge_selection(),
                    RuntimeId::SplitEdges),
      runtime_entry(ModelingInstantOperationId::DissolveEdges, "dissolve_edges",
                    "Dissolve Edges", "Dissolve Edges",
                    edge_selection(),
                    RuntimeId::DissolveEdges),
      runtime_entry(ModelingInstantOperationId::MergeEdges, "merge_edges",
                    "Merge Edges", "Merge Edges",
                    edge_selection(2),
                    RuntimeId::MergeEdges),
      runtime_entry(ModelingInstantOperationId::CollapseEdges, "collapse_edges",
                    "Collapse Edges", "Collapse Edges",
                    edge_selection(),
                    RuntimeId::CollapseEdges),
      runtime_entry(ModelingInstantOperationId::FillHole, "fill_hole",
                    "Fill Hole",
                    "Fill Hole", edge_selection(3),
                    RuntimeId::FillHole),
      runtime_entry(ModelingInstantOperationId::CombineFaces, "combine_faces",
                    "Combine Faces", "Combine Faces",
                    face_selection(2),
                    RuntimeId::CombineFaces),
      runtime_entry(ModelingInstantOperationId::CollapseFaces, "collapse_faces",
                    "Collapse Faces", "Collapse Faces",
                    face_selection(),
                    RuntimeId::CollapseFaces),
      runtime_entry(ModelingInstantOperationId::RadialAlign, "radial_align",
                    "Radial Align Components",
                    "Radial Align Components",
                    component_selection(),
                    RuntimeId::RadialAlign),
      runtime_entry(ModelingInstantOperationId::FlipFaceNormals,
                    "flip_face_normals",
                    "Flip Normals",
                    "Flip Normals",
                    object_or_face_selection(),
                    RuntimeId::FlipFaceNormals),
      runtime_entry(ModelingInstantOperationId::RecalculateFaceNormals,
                    "recalculate_face_normals",
                    "Recalculate Normals",
                    "Recalculate Normals",
                    object_or_face_selection(),
                    RuntimeId::RecalculateFaceNormals),
      runtime_entry(ModelingInstantOperationId::ShadeFacesSmooth,
                    "shade_faces_smooth",
                    "Shade Smooth",
                    "Shade Smooth",
                    object_or_face_selection(), RuntimeId::ShadeFacesSmooth),
      runtime_entry(ModelingInstantOperationId::ShadeFacesFlat,
                    "shade_faces_flat",
                    "Shade Flat",
                    "Shade Flat",
                    object_or_face_selection(), RuntimeId::ShadeFacesFlat),
      runtime_entry(ModelingInstantOperationId::DefaultEdgeNormals,
                    "default_edge_normals", "Reset Edge Normals",
                    "Reset Edge Normals", edge_selection(),
                    RuntimeId::DefaultEdgeNormals),
      runtime_entry(ModelingInstantOperationId::HardenEdgeNormals,
                    "harden_edge_normals", "Harden Edge Normals",
                    "Harden Edge Normals", edge_selection(),
                    RuntimeId::HardenEdgeNormals),
      runtime_entry(ModelingInstantOperationId::SoftenEdgeNormals,
                    "soften_edge_normals", "Soften Edge Normals",
                    "Soften Edge Normals", edge_selection(),
                    RuntimeId::SoftenEdgeNormals),
  };
  return {kEntries.data(), kEntries.size()};
}

const ModelingInstantOperationCatalogEntry *
find_modeling_instant_operation_catalog_entry(
    ModelingInstantOperationId operation) {
  const std::span<const ModelingInstantOperationCatalogEntry> entries =
      modeling_instant_operation_catalog();
  const auto found =
      std::find_if(entries.begin(), entries.end(),
                   [&](const ModelingInstantOperationCatalogEntry &entry) {
                     return entry.operation == operation;
                   });
  return found == entries.end() ? nullptr : &*found;
}

std::optional<quader_editor_core::EditorOperationCommand>
modeling_instant_operation_command(
    ModelingInstantOperationId operation,
    const ModelingInteractionSettings &interaction_settings) {
  const ModelingInstantOperationCatalogEntry *entry =
      find_modeling_instant_operation_catalog_entry(operation);
  if (entry == nullptr || !entry->runtime_operation.has_value()) {
    return std::nullopt;
  }

  quader_editor_core::EditorOperationCommand command;
  command.operation_id = *entry->runtime_operation;
  command.phase = quader_editor_core::EditorOperationCommandPhase::Execute;
  command.settings =
      settings_for_operation(command.operation_id, interaction_settings);
  if (operation == ModelingInstantOperationId::Bridge) {
    command.settings = quader_editor_core::BridgeOperationSettings{.steps = 1};
  }
  return command;
}

bool modeling_instant_operation_enabled(
    const ModelingInstantOperationCatalogEntry &entry,
    const ModelingSelectionSummary &summary) {
  return ModelingActionCatalog::evaluate_operation_availability(
             summary, entry.operation_id, entry.availability)
             .visibility == ModelingActionVisibility::Enabled;
}

} // namespace quader
