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
        ModelingInstantOperationEnablement enablement,
        quader_editor_core::EditorOperationId runtimeOperation,
        RuntimeOperationSettingsKind settingsKind = RuntimeOperationSettingsKind::Selection) {
    return {
            .operation = operation,
            .id = id,
            .operationId = id,
            .commandLabel = commandLabel,
            .liveLabel = liveLabel,
            .enablement = enablement,
            .runtimeOperation = runtimeOperation,
            .settingsKind = settingsKind,
    };
}

[[nodiscard]] ModelingInstantOperationCatalogEntry commandOnlyEntry(
        ModelingInstantOperationId operation,
        std::string_view id,
        std::string_view commandLabel,
        std::string_view liveLabel,
        ModelingInstantOperationEnablement enablement) {
    return {
            .operation = operation,
            .id = id,
            .operationId = id,
            .commandLabel = commandLabel,
            .liveLabel = liveLabel,
            .enablement = enablement,
            .runtimeOperation = std::nullopt,
    };
}

} // namespace

std::span<const ModelingInstantOperationCatalogEntry> modelingInstantOperationCatalog() {
    using enum ModelingInstantOperationEnablement;
    using RuntimeId = quader_editor_core::EditorOperationId;

    static const std::array entries{
            commandOnlyEntry(
                    ModelingInstantOperationId::DeleteSelection,
                    "delete_selection",
                    "Delete",
                    "Delete",
                    AnySelection),
            runtimeEntry(
                    ModelingInstantOperationId::DuplicateMeshes,
                    "duplicate_meshes",
                    "Duplicate Meshes",
                    "Duplicate",
                    ObjectSelection,
                    RuntimeId::duplicate_meshes,
                    RuntimeOperationSettingsKind::DuplicateMeshes),
            runtimeEntry(
                    ModelingInstantOperationId::CombineMeshes,
                    "combine_meshes",
                    "Combine Meshes",
                    "Combine Meshes",
                    MultipleObjects,
                    RuntimeId::combine_meshes),
            runtimeEntry(
                    ModelingInstantOperationId::ExtractFaces,
                    "extract_faces",
                    "Extract Faces",
                    "Extract Faces",
                    FaceSelection,
                    RuntimeId::extract_faces),
            runtimeEntry(
                    ModelingInstantOperationId::DetachFaces,
                    "detach_faces",
                    "Detach Faces",
                    "Detach Faces",
                    FaceSelection,
                    RuntimeId::detach_faces),
            runtimeEntry(
                    ModelingInstantOperationId::Bridge,
                    "bridge",
                    "Bridge",
                    "Bridge",
                    EdgeOrFaceSelection,
                    RuntimeId::bridge,
                    RuntimeOperationSettingsKind::Bridge),
            runtimeEntry(
                    ModelingInstantOperationId::SnapVerticesToActive,
                    "snap_vertices_to_active",
                    "Snap Vertices to Active",
                    "Snap to Vertex",
                    ActiveVertexSelection,
                    RuntimeId::snap_vertices_to_active),
            runtimeEntry(
                    ModelingInstantOperationId::ConnectVertices,
                    "connect_vertices",
                    "Connect Vertices",
                    "Connect Vertices",
                    VertexSelection,
                    RuntimeId::connect_vertices),
            runtimeEntry(
                    ModelingInstantOperationId::DissolveVertices,
                    "dissolve_vertices",
                    "Dissolve Vertices",
                    "Dissolve Vertices",
                    VertexSelection,
                    RuntimeId::dissolve_vertices),
            runtimeEntry(
                    ModelingInstantOperationId::BevelVertices,
                    "bevel_vertices",
                    "Bevel Vertex",
                    "Bevel Vertex",
                    VertexSelection,
                    RuntimeId::bevel_vertices,
                    RuntimeOperationSettingsKind::BevelVertices),
            runtimeEntry(
                    ModelingInstantOperationId::MergeVerticesToActive,
                    "merge_vertices_to_active",
                    "Merge Vertices to Active",
                    "Merge Vertices To Active",
                    MultipleVertices,
                    RuntimeId::merge_vertices_to_active),
            runtimeEntry(
                    ModelingInstantOperationId::MergeVerticesToCenter,
                    "merge_vertices_to_center",
                    "Merge Vertices to Center",
                    "Merge Vertices To Center",
                    MultipleVertices,
                    RuntimeId::merge_vertices_to_center),
            runtimeEntry(
                    ModelingInstantOperationId::SnapEdgesToActive,
                    "snap_edges_to_active",
                    "Snap Edges to Active",
                    "Snap to Edge",
                    ActiveEdgeSelection,
                    RuntimeId::snap_edges_to_active),
            runtimeEntry(
                    ModelingInstantOperationId::ConnectEdges,
                    "connect_edges",
                    "Connect Edges",
                    "Connect Edges",
                    EdgeSelection,
                    RuntimeId::connect_edges),
            runtimeEntry(
                    ModelingInstantOperationId::SplitEdges,
                    "split_edges",
                    "Split Edges",
                    "Split Edges",
                    EdgeSelection,
                    RuntimeId::split_edges),
            runtimeEntry(
                    ModelingInstantOperationId::DissolveEdges,
                    "dissolve_edges",
                    "Dissolve Edges",
                    "Dissolve Edges",
                    EdgeSelection,
                    RuntimeId::dissolve_edges),
            runtimeEntry(
                    ModelingInstantOperationId::MergeEdges,
                    "merge_edges",
                    "Merge Edges",
                    "Merge Edges",
                    EdgeSelection,
                    RuntimeId::merge_edges),
            runtimeEntry(
                    ModelingInstantOperationId::CollapseEdges,
                    "collapse_edges",
                    "Collapse Edges",
                    "Collapse Edges",
                    EdgeSelection,
                    RuntimeId::collapse_edges),
            runtimeEntry(
                    ModelingInstantOperationId::FillHole,
                    "fill_hole",
                    "Fill Hole",
                    "Fill Hole",
                    EdgeSelection,
                    RuntimeId::fill_hole),
            runtimeEntry(
                    ModelingInstantOperationId::CombineFaces,
                    "combine_faces",
                    "Combine Faces",
                    "Combine Faces",
                    FaceSelection,
                    RuntimeId::combine_faces),
            runtimeEntry(
                    ModelingInstantOperationId::CollapseFaces,
                    "collapse_faces",
                    "Collapse Faces",
                    "Collapse Faces",
                    FaceSelection,
                    RuntimeId::collapse_faces),
            runtimeEntry(
                    ModelingInstantOperationId::RadialAlign,
                    "radial_align",
                    "Radial Align",
                    "Radial Align",
                    NonObjectSelection,
                    RuntimeId::radial_align),
            runtimeEntry(
                    ModelingInstantOperationId::FlipFaceNormals,
                    "flip_face_normals",
                    "Flip Face Normals",
                    "Flip Face Normals",
                    ObjectOrFaceSelection,
                    RuntimeId::flip_face_normals),
            runtimeEntry(
                    ModelingInstantOperationId::RecalculateFaceNormals,
                    "recalculate_face_normals",
                    "Recalculate Face Normals",
                    "Recalculate Face Normals",
                    ObjectOrFaceSelection,
                    RuntimeId::recalculate_face_normals,
                    RuntimeOperationSettingsKind::RecalculateFaceNormals),
            runtimeEntry(
                    ModelingInstantOperationId::ShadeFacesSmooth,
                    "shade_faces_smooth",
                    "Shade Smooth",
                    "Shade Smooth",
                    ObjectOrFaceSelection,
                    RuntimeId::shade_faces_smooth),
            runtimeEntry(
                    ModelingInstantOperationId::ShadeFacesFlat,
                    "shade_faces_flat",
                    "Shade Flat",
                    "Shade Flat",
                    ObjectOrFaceSelection,
                    RuntimeId::shade_faces_flat),
            runtimeEntry(
                    ModelingInstantOperationId::HardenEdgeNormals,
                    "harden_edge_normals",
                    "Harden Edge Normals",
                    "Harden Edge Normals",
                    EdgeSelection,
                    RuntimeId::harden_edge_normals),
            runtimeEntry(
                    ModelingInstantOperationId::SoftenEdgeNormals,
                    "soften_edge_normals",
                    "Soften Edge Normals",
                    "Soften Edge Normals",
                    EdgeSelection,
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
    const bool hasSelection = summary.hasSelection;
    const bool objectMode = summary.mode == ModelingSelectionMode::Object;
    const bool vertexMode = summary.mode == ModelingSelectionMode::Vertex;
    const bool edgeMode = summary.mode == ModelingSelectionMode::Edge;
    const bool faceMode = summary.mode == ModelingSelectionMode::Face;
    const bool hasVertices = summary.selectedVertexCount > 0;
    const bool hasEdges = summary.selectedEdgeCount > 0;
    const bool hasFaces = summary.selectedFaceCount > 0;

    switch (entry.enablement) {
    case ModelingInstantOperationEnablement::AnySelection:
        return hasSelection;
    case ModelingInstantOperationEnablement::ObjectSelection:
        return objectMode && summary.selectedObjectCount > 0;
    case ModelingInstantOperationEnablement::MultipleObjects:
        return objectMode && summary.selectedObjectCount > 1;
    case ModelingInstantOperationEnablement::FaceSelection:
        return faceMode && hasFaces;
    case ModelingInstantOperationEnablement::EdgeOrFaceSelection:
        return (edgeMode && hasEdges) || (faceMode && hasFaces);
    case ModelingInstantOperationEnablement::ActiveVertexSelection:
        return vertexMode && summary.selectedVertexCount > 1 &&
                summary.activeKind == ModelingActiveSelectionKind::Vertex;
    case ModelingInstantOperationEnablement::VertexSelection:
        return vertexMode && hasVertices;
    case ModelingInstantOperationEnablement::MultipleVertices:
        return vertexMode && summary.selectedVertexCount > 1;
    case ModelingInstantOperationEnablement::ActiveEdgeSelection:
        return edgeMode && summary.selectedEdgeCount > 1 &&
                summary.activeKind == ModelingActiveSelectionKind::Edge;
    case ModelingInstantOperationEnablement::EdgeSelection:
        return edgeMode && hasEdges;
    case ModelingInstantOperationEnablement::NonObjectSelection:
        return hasSelection && !objectMode;
    case ModelingInstantOperationEnablement::ObjectOrFaceSelection:
        return (objectMode && hasSelection) || (faceMode && hasFaces);
    }
    return false;
}

} // namespace quader
