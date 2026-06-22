////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/ModelingOperationSettings.h"

namespace quader {

const char* fallbackToolSettingsLabel(ModelingToolSettingsTargetKind kind) {
    switch (kind) {
    case ModelingToolSettingsTargetKind::Cut:
        return "Cut";
    case ModelingToolSettingsTargetKind::Mirror:
        return "Mirror";
    case ModelingToolSettingsTargetKind::Bridge:
        return "Interpolated Bridge";
    case ModelingToolSettingsTargetKind::EdgeBevel:
        return "Bevel Edges";
    case ModelingToolSettingsTargetKind::Thicken:
        return "Distance";
    case ModelingToolSettingsTargetKind::SliceQuad:
        return "Slice Quad";
    case ModelingToolSettingsTargetKind::MergeByDistance:
        return "Merge by Distance";
    case ModelingToolSettingsTargetKind::None:
        break;
    }
    return "Tool Settings";
}

quader_editor_core::EditorOperationSettings settingsForOperation(
        quader_editor_core::EditorOperationId operation,
        const ModelingInteractionSettings& interactionSettings) {
    switch (operation) {
    case quader_editor_core::EditorOperationId::bridge:
        return quader_editor_core::BridgeOperationSettings{.steps = interactionSettings.bridgeSteps};
    case quader_editor_core::EditorOperationId::edge_bevel:
        return quader_editor_core::EdgeBevelOperationSettings{
                .width = interactionSettings.edgeBevelWidth,
                .profile = interactionSettings.edgeBevelProfile,
                .segments = interactionSettings.edgeBevelSegments,
        };
    case quader_editor_core::EditorOperationId::thicken:
        return quader_editor_core::ThickenOperationSettings{
                .thickness = interactionSettings.thickenThickness,
                .from_center = interactionSettings.thickenFromCenter,
        };
    case quader_editor_core::EditorOperationId::slice_quad:
        return quader_editor_core::SliceQuadOperationSettings{
                .x_slices = interactionSettings.sliceQuadXSlices,
                .y_slices = interactionSettings.sliceQuadYSlices,
        };
    case quader_editor_core::EditorOperationId::merge_by_distance:
        return quader_editor_core::MergeByDistanceOperationSettings{
                .tolerance = interactionSettings.mergeByDistanceTolerance,
        };
    case quader_editor_core::EditorOperationId::bevel_vertices:
        return quader_editor_core::BevelVerticesOperationSettings{};
    case quader_editor_core::EditorOperationId::duplicate_meshes:
        return quader_editor_core::DuplicateMeshesOperationSettings{};
    case quader_editor_core::EditorOperationId::recalculate_face_normals: {
        quader_editor_core::RecalculateFaceNormalsOperationSettings settings;
        settings.outside = true;
        return settings;
    }
    case quader_editor_core::EditorOperationId::assign_material:
        return quader_editor_core::AssignMaterialOperationSettings{};
    case quader_editor_core::EditorOperationId::connect_vertices:
    case quader_editor_core::EditorOperationId::dissolve_vertices:
    case quader_editor_core::EditorOperationId::connect_edges:
    case quader_editor_core::EditorOperationId::split_edges:
    case quader_editor_core::EditorOperationId::dissolve_edges:
    case quader_editor_core::EditorOperationId::merge_edges:
    case quader_editor_core::EditorOperationId::collapse_edges:
    case quader_editor_core::EditorOperationId::fill_hole:
    case quader_editor_core::EditorOperationId::combine_faces:
    case quader_editor_core::EditorOperationId::collapse_faces:
    case quader_editor_core::EditorOperationId::radial_align:
    case quader_editor_core::EditorOperationId::flip_face_normals:
    case quader_editor_core::EditorOperationId::shade_faces_smooth:
    case quader_editor_core::EditorOperationId::shade_faces_flat:
    case quader_editor_core::EditorOperationId::harden_edge_normals:
    case quader_editor_core::EditorOperationId::soften_edge_normals:
    case quader_editor_core::EditorOperationId::combine_meshes:
    case quader_editor_core::EditorOperationId::extract_faces:
    case quader_editor_core::EditorOperationId::detach_faces:
    case quader_editor_core::EditorOperationId::snap_vertices_to_active:
    case quader_editor_core::EditorOperationId::snap_edges_to_active:
    case quader_editor_core::EditorOperationId::merge_vertices_to_active:
    case quader_editor_core::EditorOperationId::merge_vertices_to_center:
    case quader_editor_core::EditorOperationId::delete_selection:
        return quader_editor_core::SelectionOperationSettings{};
    }
    return quader_editor_core::SelectionOperationSettings{};
}

ModelingToolSettingsTargetKind toolSettingsTargetKindForPreview(
        quader_editor_core::PreviewTransactionKind kind) {
    switch (kind) {
    case quader_editor_core::PreviewTransactionKind::bridge:
        return ModelingToolSettingsTargetKind::Bridge;
    case quader_editor_core::PreviewTransactionKind::edge_bevel:
        return ModelingToolSettingsTargetKind::EdgeBevel;
    case quader_editor_core::PreviewTransactionKind::thicken:
        return ModelingToolSettingsTargetKind::Thicken;
    case quader_editor_core::PreviewTransactionKind::slice_quad:
        return ModelingToolSettingsTargetKind::SliceQuad;
    case quader_editor_core::PreviewTransactionKind::merge_by_distance:
        return ModelingToolSettingsTargetKind::MergeByDistance;
    case quader_editor_core::PreviewTransactionKind::none:
    case quader_editor_core::PreviewTransactionKind::transform:
        break;
    }
    return ModelingToolSettingsTargetKind::None;
}

std::optional<quader_editor_core::EditorOperationId> operationForPreviewKind(
        quader_editor_core::PreviewTransactionKind kind) {
    switch (kind) {
    case quader_editor_core::PreviewTransactionKind::bridge:
        return quader_editor_core::EditorOperationId::bridge;
    case quader_editor_core::PreviewTransactionKind::edge_bevel:
        return quader_editor_core::EditorOperationId::edge_bevel;
    case quader_editor_core::PreviewTransactionKind::thicken:
        return quader_editor_core::EditorOperationId::thicken;
    case quader_editor_core::PreviewTransactionKind::slice_quad:
        return quader_editor_core::EditorOperationId::slice_quad;
    case quader_editor_core::PreviewTransactionKind::merge_by_distance:
        return quader_editor_core::EditorOperationId::merge_by_distance;
    case quader_editor_core::PreviewTransactionKind::none:
    case quader_editor_core::PreviewTransactionKind::transform:
        break;
    }
    return std::nullopt;
}

} // namespace quader
