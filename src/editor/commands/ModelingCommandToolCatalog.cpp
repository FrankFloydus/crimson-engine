////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/ModelingCommandToolCatalog.h"

#include "editor/commands/modeling/AssignMaterialTool.h"
#include "editor/commands/modeling/ClearSelectionTool.h"
#include "editor/commands/modeling/CopySelectionTool.h"
#include "editor/commands/modeling/FlipHorizontalTool.h"
#include "editor/commands/modeling/FlipVerticalTool.h"
#include "editor/commands/modeling/InvertSelectionTool.h"
#include "editor/commands/modeling/MirrorSelectionTool.h"
#include "editor/commands/modeling/PasteSelectionTool.h"
#include "editor/commands/modeling/RepeatLastActionTool.h"
#include "editor/commands/modeling/SelectAllTool.h"
#include "editor/commands/ContextOperationCommandTool.h"
#include "editor/commands/InstantOperationCommandTool.h"
#include "editor/commands/PreviewOperationCommandTool.h"
#include "editor/modeling/services/ModelingInstantOperationCatalog.h"

#include <algorithm>
#include <array>
#include <memory>
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

[[nodiscard]] CommandToolDescriptor instantDescriptor(const ModelingInstantOperationCatalogEntry& entry) {
    return {
            .id = entry.id,
            .operationId = entry.operationId,
            .label = entry.commandLabel,
            .kind = CommandToolKind::Instant,
            .previewKind = ModelingActionPreviewKind::None,
            .rebuildsMeshOnSuccess = entry.rebuildsMeshOnSuccess,
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
    static const std::vector<std::unique_ptr<CatalogInstantOperationTool>> ownedTools = [] {
        std::vector<std::unique_ptr<CatalogInstantOperationTool>> result;
        for (const ModelingInstantOperationCatalogEntry& entry : modelingInstantOperationCatalog()) {
            result.push_back(std::make_unique<CatalogInstantOperationTool>(
                    instantDescriptor(entry),
                    entry.operation));
        }
        return result;
    }();

    static const std::vector<const InstantCommandTool*> tools = [] {
        std::vector<const InstantCommandTool*> result;
        result.reserve(ownedTools.size());
        for (const std::unique_ptr<CatalogInstantOperationTool>& tool : ownedTools) {
            result.push_back(tool.get());
        }
        return result;
    }();
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
