////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/ModelingInstantOperationCatalog.h"

#include <algorithm>
#include <array>

namespace quader {
namespace {

[[nodiscard]] ModelingInstantOperationCatalogEntry runtimeEntry(
        ModelingInstantOperationId operation,
        std::string_view id,
        std::string_view commandLabel,
        std::string_view liveLabel,
        ModelingActionAvailabilityRule availability,
        quader_editor_core::EditorOperationId runtimeOperation,
        std::string_view publicCommandId = {},
        bool registersPublicCommand = true,
        std::string_view publicCompatibilityAlias = {}) {
    return {
            .operation = operation,
            .id = id,
            .operationId = id,
            .commandLabel = commandLabel,
            .liveLabel = liveLabel,
            .availability = availability,
            .runtimeOperation = runtimeOperation,
            .publicCommandId = publicCommandId,
            .registersPublicCommand = registersPublicCommand,
            .publicCompatibilityAlias = publicCompatibilityAlias,
    };
}

[[nodiscard]] ModelingInstantOperationCatalogEntry commandOnlyEntry(
        ModelingInstantOperationId operation,
        std::string_view id,
        std::string_view commandLabel,
        std::string_view liveLabel,
        ModelingActionAvailabilityRule availability,
        std::string_view publicCommandId = {},
        bool registersPublicCommand = true,
        std::string_view publicCompatibilityAlias = {}) {
    return {
            .operation = operation,
            .id = id,
            .operationId = id,
            .commandLabel = commandLabel,
            .liveLabel = liveLabel,
            .availability = availability,
            .runtimeOperation = std::nullopt,
            .publicCommandId = publicCommandId,
            .registersPublicCommand = registersPublicCommand,
            .publicCompatibilityAlias = publicCompatibilityAlias,
    };
}

[[nodiscard]] ModelingActionAvailabilityRule anySelection() {
    return {.requirement = ModelingActionSelectionRequirement::AnySelection};
}

[[nodiscard]] ModelingActionAvailabilityRule modeSelection(
        ModelingSelectionMode mode,
        std::uint32_t minimumObjects = 0,
        std::uint32_t minimumVertices = 0,
        std::uint32_t minimumEdges = 0,
        std::uint32_t minimumFaces = 0,
        ModelingActiveSelectionKind requiredActive = ModelingActiveSelectionKind::None) {
    return {
            .requirement = ModelingActionSelectionRequirement::SelectionMode,
            .mode = mode,
            .minimumSelectedVertices = minimumVertices,
            .minimumSelectedEdges = minimumEdges,
            .minimumSelectedFaces = minimumFaces,
            .minimumSelectedObjects = minimumObjects,
            .requiredActiveKind = requiredActive,
    };
}

[[nodiscard]] ModelingActionAvailabilityRule objectSelection(std::uint32_t minimumObjects = 1) {
    return modeSelection(ModelingSelectionMode::Object, minimumObjects);
}

[[nodiscard]] ModelingActionAvailabilityRule vertexSelection(
        std::uint32_t minimumVertices = 1,
        ModelingActiveSelectionKind requiredActive = ModelingActiveSelectionKind::None) {
    return modeSelection(ModelingSelectionMode::Vertex, 0, minimumVertices, 0, 0, requiredActive);
}

[[nodiscard]] ModelingActionAvailabilityRule edgeSelection(
        std::uint32_t minimumEdges = 1,
        ModelingActiveSelectionKind requiredActive = ModelingActiveSelectionKind::None) {
    return modeSelection(ModelingSelectionMode::Edge, 0, 0, minimumEdges, 0, requiredActive);
}

[[nodiscard]] ModelingActionAvailabilityRule faceSelection(std::uint32_t minimumFaces = 1) {
    return modeSelection(ModelingSelectionMode::Face, 0, 0, 0, minimumFaces);
}

[[nodiscard]] ModelingActionAvailabilityRule edgeOrFaceSelection() {
    return {.requirement = ModelingActionSelectionRequirement::EdgeOrFaceSelection};
}

[[nodiscard]] ModelingActionAvailabilityRule nonObjectSelection() {
    return {.requirement = ModelingActionSelectionRequirement::NonObjectSelection};
}

[[nodiscard]] ModelingActionAvailabilityRule objectOrFaceSelection() {
    return {.requirement = ModelingActionSelectionRequirement::ObjectOrFaceSelection};
}

} // namespace

std::span<const ModelingInstantOperationCatalogEntry> modelingInstantOperationCatalog() {
    using RuntimeId = quader_editor_core::EditorOperationId;

    static const std::array entries{
            commandOnlyEntry(
                    ModelingInstantOperationId::DeleteSelection,
                    "delete_selection",
                    "Delete",
                    "Delete",
                    anySelection(),
                    "edit.delete",
                    false,
                    "modeling.delete_selection"),
            runtimeEntry(
                    ModelingInstantOperationId::DuplicateMeshes,
                    "duplicate_meshes",
                    "Duplicate Meshes",
                    "Duplicate",
                    objectSelection(),
                    RuntimeId::duplicate_meshes,
                    "edit.duplicate",
                    false,
                    "modeling.duplicate_meshes"),
            runtimeEntry(
                    ModelingInstantOperationId::CombineMeshes,
                    "combine_meshes",
                    "Combine Meshes",
                    "Combine Meshes",
                    objectSelection(2),
                    RuntimeId::combine_meshes),
            runtimeEntry(
                    ModelingInstantOperationId::ExtractFaces,
                    "extract_faces",
                    "Extract Faces",
                    "Extract Faces",
                    faceSelection(),
                    RuntimeId::extract_faces),
            runtimeEntry(
                    ModelingInstantOperationId::DetachFaces,
                    "detach_faces",
                    "Detach Faces",
                    "Detach Faces",
                    faceSelection(),
                    RuntimeId::detach_faces),
            runtimeEntry(
                    ModelingInstantOperationId::Bridge,
                    "bridge",
                    "Bridge",
                    "Bridge",
                    edgeOrFaceSelection(),
                    RuntimeId::bridge),
            runtimeEntry(
                    ModelingInstantOperationId::SnapVerticesToActive,
                    "snap_vertices_to_active",
                    "Snap Vertices to Active",
                    "Snap to Vertex",
                    vertexSelection(2, ModelingActiveSelectionKind::Vertex),
                    RuntimeId::snap_vertices_to_active),
            runtimeEntry(
                    ModelingInstantOperationId::ConnectVertices,
                    "connect_vertices",
                    "Connect Vertices",
                    "Connect Vertices",
                    vertexSelection(),
                    RuntimeId::connect_vertices),
            runtimeEntry(
                    ModelingInstantOperationId::DissolveVertices,
                    "dissolve_vertices",
                    "Dissolve Vertices",
                    "Dissolve Vertices",
                    vertexSelection(),
                    RuntimeId::dissolve_vertices),
            runtimeEntry(
                    ModelingInstantOperationId::BevelVertices,
                    "bevel_vertices",
                    "Bevel Vertex",
                    "Bevel Vertex",
                    vertexSelection(),
                    RuntimeId::bevel_vertices),
            runtimeEntry(
                    ModelingInstantOperationId::MergeVerticesToActive,
                    "merge_vertices_to_active",
                    "Merge Vertices to Active",
                    "Merge Vertices To Active",
                    vertexSelection(2),
                    RuntimeId::merge_vertices_to_active),
            runtimeEntry(
                    ModelingInstantOperationId::MergeVerticesToCenter,
                    "merge_vertices_to_center",
                    "Merge Vertices to Center",
                    "Merge Vertices To Center",
                    vertexSelection(2),
                    RuntimeId::merge_vertices_to_center),
            runtimeEntry(
                    ModelingInstantOperationId::SnapEdgesToActive,
                    "snap_edges_to_active",
                    "Snap Edges to Active",
                    "Snap to Edge",
                    edgeSelection(2, ModelingActiveSelectionKind::Edge),
                    RuntimeId::snap_edges_to_active),
            runtimeEntry(
                    ModelingInstantOperationId::ConnectEdges,
                    "connect_edges",
                    "Connect Edges",
                    "Connect Edges",
                    edgeSelection(),
                    RuntimeId::connect_edges),
            runtimeEntry(
                    ModelingInstantOperationId::SplitEdges,
                    "split_edges",
                    "Split Edges",
                    "Split Edges",
                    edgeSelection(),
                    RuntimeId::split_edges),
            runtimeEntry(
                    ModelingInstantOperationId::DissolveEdges,
                    "dissolve_edges",
                    "Dissolve Edges",
                    "Dissolve Edges",
                    edgeSelection(),
                    RuntimeId::dissolve_edges),
            runtimeEntry(
                    ModelingInstantOperationId::MergeEdges,
                    "merge_edges",
                    "Merge Edges",
                    "Merge Edges",
                    edgeSelection(),
                    RuntimeId::merge_edges),
            runtimeEntry(
                    ModelingInstantOperationId::CollapseEdges,
                    "collapse_edges",
                    "Collapse Edges",
                    "Collapse Edges",
                    edgeSelection(),
                    RuntimeId::collapse_edges),
            runtimeEntry(
                    ModelingInstantOperationId::FillHole,
                    "fill_hole",
                    "Fill Hole",
                    "Fill Hole",
                    edgeSelection(),
                    RuntimeId::fill_hole),
            runtimeEntry(
                    ModelingInstantOperationId::CombineFaces,
                    "combine_faces",
                    "Combine Faces",
                    "Combine Faces",
                    faceSelection(),
                    RuntimeId::combine_faces),
            runtimeEntry(
                    ModelingInstantOperationId::CollapseFaces,
                    "collapse_faces",
                    "Collapse Faces",
                    "Collapse Faces",
                    faceSelection(),
                    RuntimeId::collapse_faces),
            runtimeEntry(
                    ModelingInstantOperationId::RadialAlign,
                    "radial_align",
                    "Radial Align",
                    "Radial Align",
                    nonObjectSelection(),
                    RuntimeId::radial_align),
            runtimeEntry(
                    ModelingInstantOperationId::FlipFaceNormals,
                    "flip_face_normals",
                    "Flip Face Normals",
                    "Flip Face Normals",
                    objectOrFaceSelection(),
                    RuntimeId::flip_face_normals),
            runtimeEntry(
                    ModelingInstantOperationId::RecalculateFaceNormals,
                    "recalculate_face_normals",
                    "Recalculate Face Normals",
                    "Recalculate Face Normals",
                    objectOrFaceSelection(),
                    RuntimeId::recalculate_face_normals),
            runtimeEntry(
                    ModelingInstantOperationId::ShadeFacesSmooth,
                    "shade_faces_smooth",
                    "Shade Smooth",
                    "Shade Smooth",
                    objectOrFaceSelection(),
                    RuntimeId::shade_faces_smooth),
            runtimeEntry(
                    ModelingInstantOperationId::ShadeFacesFlat,
                    "shade_faces_flat",
                    "Shade Flat",
                    "Shade Flat",
                    objectOrFaceSelection(),
                    RuntimeId::shade_faces_flat),
            runtimeEntry(
                    ModelingInstantOperationId::HardenEdgeNormals,
                    "harden_edge_normals",
                    "Harden Edge Normals",
                    "Harden Edge Normals",
                    edgeSelection(),
                    RuntimeId::harden_edge_normals),
            runtimeEntry(
                    ModelingInstantOperationId::SoftenEdgeNormals,
                    "soften_edge_normals",
                    "Soften Edge Normals",
                    "Soften Edge Normals",
                    edgeSelection(),
                    RuntimeId::soften_edge_normals),
    };
    return {entries.data(), entries.size()};
}

const ModelingInstantOperationCatalogEntry* findModelingInstantOperationCatalogEntry(
        ModelingInstantOperationId operation) {
    const std::span<const ModelingInstantOperationCatalogEntry> entries = modelingInstantOperationCatalog();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const ModelingInstantOperationCatalogEntry& entry) {
        return entry.operation == operation;
    });
    return found == entries.end() ? nullptr : &*found;
}

bool modelingInstantOperationEnabled(
        const ModelingInstantOperationCatalogEntry& entry,
        const ModelingSelectionSummary& summary) {
    return ModelingActionCatalog::evaluateOperationAvailability(
            summary,
            entry.operationId,
            entry.availability).visibility == ModelingActionVisibility::Enabled;
}

} // namespace quader
