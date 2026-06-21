#include "modeling/tools/ModelingCommandToolCatalog.h"

#include "modeling/tools/commands/AssignMaterialTool.h"
#include "modeling/tools/commands/BevelVerticesTool.h"
#include "modeling/tools/commands/BridgeOperationTool.h"
#include "modeling/tools/commands/ClearSelectionTool.h"
#include "modeling/tools/commands/CollapseEdgesTool.h"
#include "modeling/tools/commands/CollapseFacesTool.h"
#include "modeling/tools/commands/CombineFacesTool.h"
#include "modeling/tools/commands/CombineMeshesTool.h"
#include "modeling/tools/commands/ConnectEdgesTool.h"
#include "modeling/tools/commands/ConnectVerticesTool.h"
#include "modeling/tools/commands/CopySelectionTool.h"
#include "modeling/tools/commands/DeleteSelectionTool.h"
#include "modeling/tools/commands/DetachFacesTool.h"
#include "modeling/tools/commands/DissolveEdgesTool.h"
#include "modeling/tools/commands/DissolveVerticesTool.h"
#include "modeling/tools/commands/DuplicateMeshesTool.h"
#include "modeling/tools/commands/ExtractFacesTool.h"
#include "modeling/tools/commands/FillHoleTool.h"
#include "modeling/tools/commands/FlipFaceNormalsTool.h"
#include "modeling/tools/commands/FlipHorizontalTool.h"
#include "modeling/tools/commands/FlipVerticalTool.h"
#include "modeling/tools/commands/HardenEdgeNormalsTool.h"
#include "modeling/tools/commands/InvertSelectionTool.h"
#include "modeling/tools/commands/MergeEdgesTool.h"
#include "modeling/tools/commands/MergeVerticesToActiveTool.h"
#include "modeling/tools/commands/MergeVerticesToCenterTool.h"
#include "modeling/tools/commands/MirrorSelectionTool.h"
#include "modeling/tools/commands/PasteSelectionTool.h"
#include "modeling/tools/commands/RadialAlignTool.h"
#include "modeling/tools/commands/RecalculateFaceNormalsTool.h"
#include "modeling/tools/commands/RepeatLastActionTool.h"
#include "modeling/tools/commands/SelectAllTool.h"
#include "modeling/tools/commands/ShadeFacesFlatTool.h"
#include "modeling/tools/commands/ShadeFacesSmoothTool.h"
#include "modeling/tools/commands/SnapEdgesToActiveTool.h"
#include "modeling/tools/commands/SnapVerticesToActiveTool.h"
#include "modeling/tools/commands/SoftenEdgeNormalsTool.h"
#include "modeling/tools/commands/SplitEdgesTool.h"
#include "modeling/tools/context/ApplyActiveMaterialTool.h"
#include "modeling/tools/context/BridgeEdgesTool.h"
#include "modeling/tools/context/BridgeFacesTool.h"
#include "modeling/tools/context/CombineMeshTool.h"
#include "modeling/tools/context/InvertFacesTool.h"
#include "modeling/tools/context/InvertMeshTool.h"
#include "modeling/tools/context/MergeVerticesAtCenterTool.h"
#include "modeling/tools/context/MergeVerticesTool.h"
#include "modeling/tools/context/RadialAlignEdgesTool.h"
#include "modeling/tools/context/RadialAlignFacesTool.h"
#include "modeling/tools/context/RadialAlignVerticesTool.h"
#include "modeling/tools/context/RecalculateInsideFacesTool.h"
#include "modeling/tools/context/RecalculateInsideMeshTool.h"
#include "modeling/tools/context/RecalculateOutsideFacesTool.h"
#include "modeling/tools/context/RecalculateOutsideMeshTool.h"
#include "modeling/tools/context/ShadeFlatMeshTool.h"
#include "modeling/tools/context/ShadeSmoothMeshTool.h"
#include "modeling/tools/preview/CreateOuterHullTool.h"
#include "modeling/tools/preview/EdgeBevelTool.h"
#include "modeling/tools/preview/InterpolatedBridgeEdgesTool.h"
#include "modeling/tools/preview/InterpolatedBridgeFacesTool.h"
#include "modeling/tools/preview/MergeByDistanceTool.h"
#include "modeling/tools/preview/SliceQuadTool.h"
#include "modeling/tools/preview/ThickenTool.h"

#include <algorithm>
#include <array>
#include <vector>

namespace quader {

std::span<const InstantCommandTool* const> modelingSdkInstantOperationTools() {
    static const DeleteSelectionTool deleteSelection;
    static const DuplicateMeshesTool duplicateMeshes;
    static const CombineMeshesTool combineMeshes;
    static const ExtractFacesTool extractFaces;
    static const DetachFacesTool detachFaces;
    static const BridgeOperationTool bridge;
    static const SnapVerticesToActiveTool snapVerticesToActive;
    static const ConnectVerticesTool connectVertices;
    static const DissolveVerticesTool dissolveVertices;
    static const BevelVerticesTool bevelVertices;
    static const MergeVerticesToActiveTool mergeVerticesToActive;
    static const MergeVerticesToCenterTool mergeVerticesToCenter;
    static const SnapEdgesToActiveTool snapEdgesToActive;
    static const ConnectEdgesTool connectEdges;
    static const SplitEdgesTool splitEdges;
    static const DissolveEdgesTool dissolveEdges;
    static const MergeEdgesTool mergeEdges;
    static const CollapseEdgesTool collapseEdges;
    static const FillHoleTool fillHole;
    static const CombineFacesTool combineFaces;
    static const CollapseFacesTool collapseFaces;
    static const RadialAlignTool radialAlign;
    static const FlipFaceNormalsTool flipFaceNormals;
    static const RecalculateFaceNormalsTool recalculateFaceNormals;
    static const ShadeFacesSmoothTool shadeFacesSmooth;
    static const ShadeFacesFlatTool shadeFacesFlat;
    static const HardenEdgeNormalsTool hardenEdgeNormals;
    static const SoftenEdgeNormalsTool softenEdgeNormals;

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
        for (const InstantCommandTool* tool : modelingSdkInstantOperationTools()) {
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
    static const InvertMeshTool invertMesh;
    static const RecalculateOutsideMeshTool recalculateOutsideMesh;
    static const RecalculateInsideMeshTool recalculateInsideMesh;
    static const ShadeSmoothMeshTool shadeSmoothMesh;
    static const ShadeFlatMeshTool shadeFlatMesh;
    static const CombineMeshTool combineMesh;
    static const MergeVerticesTool mergeVertices;
    static const MergeVerticesAtCenterTool mergeVerticesAtCenter;
    static const RadialAlignVerticesTool radialAlignVertices;
    static const BridgeEdgesTool bridgeEdges;
    static const RadialAlignEdgesTool radialAlignEdges;
    static const InvertFacesTool invertFaces;
    static const RecalculateOutsideFacesTool recalculateOutsideFaces;
    static const RecalculateInsideFacesTool recalculateInsideFaces;
    static const BridgeFacesTool bridgeFaces;
    static const RadialAlignFacesTool radialAlignFaces;
    static const ApplyActiveMaterialTool applyActiveMaterial;

    static const std::array<const ContextActionTool*, 17> tools{{
            &invertMesh,
            &recalculateOutsideMesh,
            &recalculateInsideMesh,
            &shadeSmoothMesh,
            &shadeFlatMesh,
            &combineMesh,
            &mergeVertices,
            &mergeVerticesAtCenter,
            &radialAlignVertices,
            &bridgeEdges,
            &radialAlignEdges,
            &invertFaces,
            &recalculateOutsideFaces,
            &recalculateInsideFaces,
            &bridgeFaces,
            &radialAlignFaces,
            &applyActiveMaterial,
    }};
    return {tools.data(), tools.size()};
}

std::span<const PreviewCommandTool* const> modelingPreviewCommandTools() {
    static const EdgeBevelTool edgeBevel;
    static const InterpolatedBridgeEdgesTool interpolatedBridgeEdges;
    static const InterpolatedBridgeFacesTool interpolatedBridgeFaces;
    static const ThickenTool thicken;
    static const CreateOuterHullTool createOuterHull;
    static const SliceQuadTool sliceQuad;
    static const MergeByDistanceTool mergeByDistance;

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
    const std::span<const InstantCommandTool* const> tools = modelingSdkInstantOperationTools();
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
