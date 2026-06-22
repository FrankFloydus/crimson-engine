////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/actions/ModelingCommandToolCatalog.h"

#include "editor/actions/commands/AssignMaterialTool.h"
#include "editor/actions/commands/ClearSelectionTool.h"
#include "editor/actions/commands/CopySelectionTool.h"
#include "editor/actions/commands/FlipHorizontalTool.h"
#include "editor/actions/commands/FlipVerticalTool.h"
#include "editor/actions/commands/InvertSelectionTool.h"
#include "editor/actions/commands/MirrorSelectionTool.h"
#include "editor/actions/commands/PasteSelectionTool.h"
#include "editor/actions/commands/RepeatLastActionTool.h"
#include "editor/actions/commands/SelectAllTool.h"
#include "editor/actions/ContextOperationCommandTool.h"
#include "editor/actions/InstantOperationCommandTool.h"
#include "editor/actions/PreviewOperationCommandTool.h"

#include <algorithm>
#include <array>
#include <vector>

namespace quader {
namespace {

/**
 * Catalog-owned instant action for modeling operations that only need static
 * descriptor metadata and the standard ModelingSessionService execution path.
 */
class CatalogInstantOperationTool final : public InstantOperationCommandTool {
public:
    CatalogInstantOperationTool(CommandToolDescriptor descriptor, ModelingInstantOperationId operation)
        : descriptor_(descriptor)
        , operation_(operation) {}

    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return descriptor_;
    }

    [[nodiscard]] ModelingInstantOperationId instantOperationId() const override {
        return operation_;
    }

private:
    CommandToolDescriptor descriptor_;
    ModelingInstantOperationId operation_;
};

/**
 * Catalog-owned context action for selection-dependent operation affordances
 * whose behavior is fully handled by the shared context-action executor.
 */
class CatalogContextActionTool final : public ContextOperationCommandTool {
public:
    explicit CatalogContextActionTool(CommandToolDescriptor descriptor)
        : descriptor_(descriptor) {}

    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return descriptor_;
    }

    [[nodiscard]] std::string_view actionId() const override {
        return descriptor_.id;
    }

private:
    CommandToolDescriptor descriptor_;
};

/**
 * Catalog-owned preview action for operation previews that only need descriptor
 * metadata and the shared preview begin/commit/cancel behavior.
 */
class CatalogPreviewActionTool final : public PreviewOperationCommandTool {
public:
    explicit CatalogPreviewActionTool(CommandToolDescriptor descriptor)
        : descriptor_(descriptor) {}

    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return descriptor_;
    }

    [[nodiscard]] std::string_view actionId() const override {
        return descriptor_.id;
    }

private:
    CommandToolDescriptor descriptor_;
};

[[nodiscard]] CommandToolDescriptor instantDescriptor(
        std::string_view id,
        std::string_view operationId,
        std::string_view label) {
    return {
            .id = id,
            .operationId = operationId,
            .label = label,
            .kind = CommandToolKind::Instant,
            .previewKind = ModelingActionPreviewKind::None,
            .rebuildsMeshOnSuccess = true,
    };
}

[[nodiscard]] CommandToolDescriptor contextDescriptor(
        std::string_view id,
        std::string_view operationId,
        std::string_view label,
        bool rebuildsMeshOnSuccess = true) {
    return {
            .id = id,
            .operationId = operationId,
            .label = label,
            .kind = CommandToolKind::ContextAction,
            .previewKind = ModelingActionPreviewKind::None,
            .rebuildsMeshOnSuccess = rebuildsMeshOnSuccess,
    };
}

[[nodiscard]] CommandToolDescriptor previewDescriptor(
        std::string_view id,
        std::string_view operationId,
        std::string_view label,
        ModelingActionPreviewKind previewKind,
        bool rebuildsMeshOnSuccess = true) {
    return {
            .id = id,
            .operationId = operationId,
            .label = label,
            .kind = CommandToolKind::Preview,
            .previewKind = previewKind,
            .rebuildsMeshOnSuccess = rebuildsMeshOnSuccess,
    };
}

} // namespace

std::span<const InstantCommandTool* const> modelingInstantOperationCommandTools() {
    static CatalogInstantOperationTool deleteSelection{
            instantDescriptor("delete_selection", "delete_selection", "Delete"),
            ModelingInstantOperationId::DeleteSelection};
    static CatalogInstantOperationTool duplicateMeshes{
            instantDescriptor("duplicate_meshes", "duplicate_meshes", "Duplicate Meshes"),
            ModelingInstantOperationId::DuplicateMeshes};
    static CatalogInstantOperationTool combineMeshes{
            instantDescriptor("combine_meshes", "combine_meshes", "Combine Meshes"),
            ModelingInstantOperationId::CombineMeshes};
    static CatalogInstantOperationTool extractFaces{
            instantDescriptor("extract_faces", "extract_faces", "Extract Faces"),
            ModelingInstantOperationId::ExtractFaces};
    static CatalogInstantOperationTool detachFaces{
            instantDescriptor("detach_faces", "detach_faces", "Detach Faces"),
            ModelingInstantOperationId::DetachFaces};
    static CatalogInstantOperationTool bridge{
            instantDescriptor("bridge", "bridge", "Bridge"),
            ModelingInstantOperationId::Bridge};
    static CatalogInstantOperationTool snapVerticesToActive{
            instantDescriptor("snap_vertices_to_active", "snap_vertices_to_active", "Snap Vertices to Active"),
            ModelingInstantOperationId::SnapVerticesToActive};
    static CatalogInstantOperationTool connectVertices{
            instantDescriptor("connect_vertices", "connect_vertices", "Connect Vertices"),
            ModelingInstantOperationId::ConnectVertices};
    static CatalogInstantOperationTool dissolveVertices{
            instantDescriptor("dissolve_vertices", "dissolve_vertices", "Dissolve Vertices"),
            ModelingInstantOperationId::DissolveVertices};
    static CatalogInstantOperationTool bevelVertices{
            instantDescriptor("bevel_vertices", "bevel_vertices", "Bevel Vertex"),
            ModelingInstantOperationId::BevelVertices};
    static CatalogInstantOperationTool mergeVerticesToActive{
            instantDescriptor("merge_vertices_to_active", "merge_vertices_to_active", "Merge Vertices to Active"),
            ModelingInstantOperationId::MergeVerticesToActive};
    static CatalogInstantOperationTool mergeVerticesToCenter{
            instantDescriptor("merge_vertices_to_center", "merge_vertices_to_center", "Merge Vertices to Center"),
            ModelingInstantOperationId::MergeVerticesToCenter};
    static CatalogInstantOperationTool snapEdgesToActive{
            instantDescriptor("snap_edges_to_active", "snap_edges_to_active", "Snap Edges to Active"),
            ModelingInstantOperationId::SnapEdgesToActive};
    static CatalogInstantOperationTool connectEdges{
            instantDescriptor("connect_edges", "connect_edges", "Connect Edges"),
            ModelingInstantOperationId::ConnectEdges};
    static CatalogInstantOperationTool splitEdges{
            instantDescriptor("split_edges", "split_edges", "Split Edges"),
            ModelingInstantOperationId::SplitEdges};
    static CatalogInstantOperationTool dissolveEdges{
            instantDescriptor("dissolve_edges", "dissolve_edges", "Dissolve Edges"),
            ModelingInstantOperationId::DissolveEdges};
    static CatalogInstantOperationTool mergeEdges{
            instantDescriptor("merge_edges", "merge_edges", "Merge Edges"),
            ModelingInstantOperationId::MergeEdges};
    static CatalogInstantOperationTool collapseEdges{
            instantDescriptor("collapse_edges", "collapse_edges", "Collapse Edges"),
            ModelingInstantOperationId::CollapseEdges};
    static CatalogInstantOperationTool fillHole{
            instantDescriptor("fill_hole", "fill_hole", "Fill Hole"),
            ModelingInstantOperationId::FillHole};
    static CatalogInstantOperationTool combineFaces{
            instantDescriptor("combine_faces", "combine_faces", "Combine Faces"),
            ModelingInstantOperationId::CombineFaces};
    static CatalogInstantOperationTool collapseFaces{
            instantDescriptor("collapse_faces", "collapse_faces", "Collapse Faces"),
            ModelingInstantOperationId::CollapseFaces};
    static CatalogInstantOperationTool radialAlign{
            instantDescriptor("radial_align", "radial_align", "Radial Align"),
            ModelingInstantOperationId::RadialAlign};
    static CatalogInstantOperationTool flipFaceNormals{
            instantDescriptor("flip_face_normals", "flip_face_normals", "Flip Face Normals"),
            ModelingInstantOperationId::FlipFaceNormals};
    static CatalogInstantOperationTool recalculateFaceNormals{
            instantDescriptor("recalculate_face_normals", "recalculate_face_normals", "Recalculate Face Normals"),
            ModelingInstantOperationId::RecalculateFaceNormals};
    static CatalogInstantOperationTool shadeFacesSmooth{
            instantDescriptor("shade_faces_smooth", "shade_faces_smooth", "Shade Smooth"),
            ModelingInstantOperationId::ShadeFacesSmooth};
    static CatalogInstantOperationTool shadeFacesFlat{
            instantDescriptor("shade_faces_flat", "shade_faces_flat", "Shade Flat"),
            ModelingInstantOperationId::ShadeFacesFlat};
    static CatalogInstantOperationTool hardenEdgeNormals{
            instantDescriptor("harden_edge_normals", "harden_edge_normals", "Harden Edge Normals"),
            ModelingInstantOperationId::HardenEdgeNormals};
    static CatalogInstantOperationTool softenEdgeNormals{
            instantDescriptor("soften_edge_normals", "soften_edge_normals", "Soften Edge Normals"),
            ModelingInstantOperationId::SoftenEdgeNormals};

    static const std::array<const InstantCommandTool*, 28> tools{{
            &deleteSelection,
            &duplicateMeshes,
            &combineMeshes,
            &extractFaces,
            &detachFaces,
            &bridge,
            &snapVerticesToActive,
            &connectVertices,
            &dissolveVertices,
            &bevelVertices,
            &mergeVerticesToActive,
            &mergeVerticesToCenter,
            &snapEdgesToActive,
            &connectEdges,
            &splitEdges,
            &dissolveEdges,
            &mergeEdges,
            &collapseEdges,
            &fillHole,
            &combineFaces,
            &collapseFaces,
            &radialAlign,
            &flipFaceNormals,
            &recalculateFaceNormals,
            &shadeFacesSmooth,
            &shadeFacesFlat,
            &hardenEdgeNormals,
            &softenEdgeNormals,
    }};
    return {tools.data(), tools.size()};
}

std::span<const InstantCommandTool* const> modelingServiceInstantCommandTools() {
    static const SelectAllTool selectAll;
    static const InvertSelectionTool invertSelection;
    static const ClearSelectionTool clearSelection;
    static const CopySelectionTool copySelection;
    static const PasteSelectionTool pasteSelection;
    static const RepeatLastActionTool repeatLastAction;
    static const AssignMaterialTool assignMaterial;
    static const MirrorSelectionTool mirrorX{ModelingMirrorAxis::X, "mirror_selection_x", "Mirror X"};
    static const MirrorSelectionTool mirrorY{ModelingMirrorAxis::Y, "mirror_selection_y", "Mirror Y"};
    static const MirrorSelectionTool mirrorZ{ModelingMirrorAxis::Z, "mirror_selection_z", "Mirror Z"};
    static const FlipHorizontalTool flipHorizontal;
    static const FlipVerticalTool flipVertical;

    static const std::array<const InstantCommandTool*, 12> tools{{
            &selectAll,
            &invertSelection,
            &clearSelection,
            &copySelection,
            &pasteSelection,
            &repeatLastAction,
            &assignMaterial,
            &mirrorX,
            &mirrorY,
            &mirrorZ,
            &flipHorizontal,
            &flipVertical,
    }};
    return {tools.data(), tools.size()};
}

std::span<const InstantCommandTool* const> modelingInstantCommandTools() {
    static const std::vector<const InstantCommandTool*> tools = [] {
        std::vector<const InstantCommandTool*> result;
        for (const InstantCommandTool* tool : modelingInstantOperationCommandTools()) {
            result.push_back(tool);
        }
        for (const InstantCommandTool* tool : modelingServiceInstantCommandTools()) {
            result.push_back(tool);
        }
        return result;
    }();
    return {tools.data(), tools.size()};
}

std::span<const ContextActionTool* const> modelingContextActionTools() {
    static CatalogContextActionTool invertMesh{
            contextDescriptor("invert_mesh", "flip_face_normals", "Invert Mesh")};
    static CatalogContextActionTool shadeSmoothMesh{
            contextDescriptor("shade_smooth_mesh", "shade_faces_smooth", "Shade Smooth Mesh")};
    static CatalogContextActionTool shadeFlatMesh{
            contextDescriptor("shade_flat_mesh", "shade_faces_flat", "Shade Flat Mesh")};
    static CatalogContextActionTool combineMesh{
            contextDescriptor("combine_mesh", "combine_meshes", "Combine Mesh")};
    static CatalogContextActionTool mergeVertices{
            contextDescriptor("merge_vertices", "merge_vertices_to_active", "Merge Vertices")};
    static CatalogContextActionTool mergeVerticesAtCenter{
            contextDescriptor("merge_vertices_at_center", "merge_vertices_to_center", "Merge Vertices at Center")};
    static CatalogContextActionTool radialAlignVertices{
            contextDescriptor("radial_align_vertices", "radial_align", "Radial Align")};
    static CatalogContextActionTool bridgeEdges{
            contextDescriptor("bridge_edges", "bridge", "Bridge")};
    static CatalogContextActionTool radialAlignEdges{
            contextDescriptor("radial_align_edges", "radial_align", "Radial Align")};
    static CatalogContextActionTool invertFaces{
            contextDescriptor("invert_faces", "flip_face_normals", "Invert Faces")};
    static CatalogContextActionTool bridgeFaces{
            contextDescriptor("bridge_faces", "bridge", "Bridge")};
    static CatalogContextActionTool radialAlignFaces{
            contextDescriptor("radial_align_faces", "radial_align", "Radial Align")};
    static CatalogContextActionTool applyActiveMaterial{
            contextDescriptor("apply_active_material", "assign_material", "Apply Active Material")};

    static const std::array<const ContextActionTool*, 13> tools{{
            &invertMesh,
            &shadeSmoothMesh,
            &shadeFlatMesh,
            &combineMesh,
            &mergeVertices,
            &mergeVerticesAtCenter,
            &radialAlignVertices,
            &bridgeEdges,
            &radialAlignEdges,
            &invertFaces,
            &bridgeFaces,
            &radialAlignFaces,
            &applyActiveMaterial,
    }};
    return {tools.data(), tools.size()};
}

std::span<const PreviewCommandTool* const> modelingPreviewCommandTools() {
    static CatalogPreviewActionTool edgeBevel{
            previewDescriptor("bevel_edges", "edge_bevel", "Bevel Edges", ModelingActionPreviewKind::EdgeBevel)};
    static CatalogPreviewActionTool interpolatedBridgeEdges{
            previewDescriptor("interpolated_bridge_edges", "bridge", "Interpolated Bridge Edges", ModelingActionPreviewKind::Bridge)};
    static CatalogPreviewActionTool interpolatedBridgeFaces{
            previewDescriptor("interpolated_bridge_faces", "bridge", "Interpolated Bridge Faces", ModelingActionPreviewKind::Bridge)};
    static CatalogPreviewActionTool thicken{
            previewDescriptor("thicken_faces", "thicken", "Thicken Faces", ModelingActionPreviewKind::Thicken)};
    static CatalogPreviewActionTool createOuterHull{
            previewDescriptor("create_outer_hull", "thicken", "Create Outer Hull", ModelingActionPreviewKind::Thicken)};
    static CatalogPreviewActionTool sliceQuad{
            previewDescriptor("slice_quad", "slice_quad", "Slice Quad", ModelingActionPreviewKind::SliceQuad)};
    static CatalogPreviewActionTool mergeByDistance{
            previewDescriptor("merge_by_distance", "merge_by_distance", "Merge by Distance", ModelingActionPreviewKind::MergeByDistance)};

    static const std::array<const PreviewCommandTool*, 7> tools{{
            &edgeBevel,
            &interpolatedBridgeEdges,
            &interpolatedBridgeFaces,
            &thicken,
            &createOuterHull,
            &sliceQuad,
            &mergeByDistance,
    }};
    return {tools.data(), tools.size()};
}

std::span<const CommandTool* const> modelingCommandTools() {
    static const std::vector<const CommandTool*> tools = [] {
        std::vector<const CommandTool*> result;
        for (const InstantCommandTool* tool : modelingInstantCommandTools()) {
            result.push_back(tool);
        }
        for (const ContextActionTool* tool : modelingContextActionTools()) {
            result.push_back(tool);
        }
        for (const PreviewCommandTool* tool : modelingPreviewCommandTools()) {
            result.push_back(tool);
        }
        return result;
    }();
    return {tools.data(), tools.size()};
}

const CommandTool* findModelingCommandTool(std::string_view id) {
    const std::span<const CommandTool* const> tools = modelingCommandTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const CommandTool* tool) {
        return tool != nullptr && tool->id() == id;
    });
    return found == tools.end() ? nullptr : *found;
}

const InstantCommandTool* findModelingInstantCommandTool(ModelingInstantOperationId operation) {
    const std::span<const InstantCommandTool* const> tools = modelingInstantOperationCommandTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const InstantCommandTool* tool) {
        return tool != nullptr && tool->hasInstantOperation() && tool->operation() == operation;
    });
    return found == tools.end() ? nullptr : *found;
}

const ContextActionTool* findModelingContextActionTool(std::string_view actionId) {
    const std::span<const ContextActionTool* const> tools = modelingContextActionTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const ContextActionTool* tool) {
        return tool != nullptr && tool->actionId() == actionId;
    });
    return found == tools.end() ? nullptr : *found;
}

const PreviewCommandTool* findModelingPreviewCommandTool(std::string_view actionId) {
    const std::span<const PreviewCommandTool* const> tools = modelingPreviewCommandTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const PreviewCommandTool* tool) {
        return tool != nullptr && tool->actionId() == actionId;
    });
    return found == tools.end() ? nullptr : *found;
}

std::string_view modelingMirrorSelectionToolId(ModelingMirrorAxis axis) {
    switch (axis) {
    case ModelingMirrorAxis::X:
        return "mirror_selection_x";
    case ModelingMirrorAxis::Y:
        return "mirror_selection_y";
    case ModelingMirrorAxis::Z:
        return "mirror_selection_z";
    }
    return "mirror_selection_x";
}

} // namespace quader
