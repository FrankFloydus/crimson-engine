////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_operation_settings.hpp"

#include <initializer_list>

namespace quader {
namespace {

bool sameOperation(std::string_view requestedToolId, std::string_view operationId) {
    return requestedToolId.empty() || requestedToolId == operationId;
}

bool matchesAny(std::string_view name, std::initializer_list<std::string_view> acceptedNames) {
    for (const std::string_view accepted : acceptedNames) {
        if (name == accepted) {
            return true;
        }
    }
    return false;
}

} // namespace

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
    const std::optional<quader_editor_core::EditorOperationCommandSchema> schema =
            quader_editor_core::command_schema_for_operation(operation);
    if (!schema.has_value()) {
        return quader_editor_core::SelectionOperationSettings{};
    }

    switch (schema->settings_kind) {
    case quader_editor_core::EditorOperationSettingsKind::bridge:
        return quader_editor_core::BridgeOperationSettings{.steps = interactionSettings.bridgeSteps};
    case quader_editor_core::EditorOperationSettingsKind::edge_bevel:
        return quader_editor_core::EdgeBevelOperationSettings{
                .width = interactionSettings.edgeBevelWidth,
                .profile = interactionSettings.edgeBevelProfile,
                .segments = interactionSettings.edgeBevelSegments,
        };
    case quader_editor_core::EditorOperationSettingsKind::thicken:
        return quader_editor_core::ThickenOperationSettings{
                .thickness = interactionSettings.thickenThickness,
                .from_center = interactionSettings.thickenFromCenter,
        };
    case quader_editor_core::EditorOperationSettingsKind::slice_quad:
        return quader_editor_core::SliceQuadOperationSettings{
                .x_slices = interactionSettings.sliceQuadXSlices,
                .y_slices = interactionSettings.sliceQuadYSlices,
        };
    case quader_editor_core::EditorOperationSettingsKind::merge_by_distance:
        return quader_editor_core::MergeByDistanceOperationSettings{
                .tolerance = interactionSettings.mergeByDistanceTolerance,
        };
    case quader_editor_core::EditorOperationSettingsKind::bevel_vertices:
        return quader_editor_core::BevelVerticesOperationSettings{};
    case quader_editor_core::EditorOperationSettingsKind::duplicate_meshes:
        return quader_editor_core::DuplicateMeshesOperationSettings{};
    case quader_editor_core::EditorOperationSettingsKind::recalculate_face_normals: {
        quader_editor_core::RecalculateFaceNormalsOperationSettings settings;
        settings.outside = true;
        return settings;
    }
    case quader_editor_core::EditorOperationSettingsKind::assign_material:
        return quader_editor_core::AssignMaterialOperationSettings{};
    case quader_editor_core::EditorOperationSettingsKind::selection:
        return quader_editor_core::SelectionOperationSettings{};
    }
    return quader_editor_core::SelectionOperationSettings{};
}

bool applyModelingInteractionSetting(
        ModelingInteractionSettings& interactionSettings,
        std::string_view toolId,
        std::string_view name,
        double value) {
    const quader_editor_core::BridgeOperationSchema bridge = quader_editor_core::bridge_operation_schema();
    if (sameOperation(toolId, bridge.operation_id) &&
            matchesAny(name, {bridge.steps.id, "bridge_steps"})) {
        interactionSettings.bridgeSteps = static_cast<int>(value);
        return true;
    }

    const quader_editor_core::EdgeBevelOperationSchema edgeBevel = quader_editor_core::edge_bevel_operation_schema();
    if (sameOperation(toolId, edgeBevel.operation_id) &&
            matchesAny(name, {edgeBevel.width.id, "amount", "edge_bevel_width"})) {
        interactionSettings.edgeBevelWidth = value;
        return true;
    }
    if (sameOperation(toolId, edgeBevel.operation_id) &&
            matchesAny(name, {edgeBevel.profile.id, "edge_bevel_profile"})) {
        interactionSettings.edgeBevelProfile = value;
        return true;
    }
    if (sameOperation(toolId, edgeBevel.operation_id) &&
            matchesAny(name, {edgeBevel.segments.id, "edge_bevel_segments"})) {
        interactionSettings.edgeBevelSegments = static_cast<int>(value);
        return true;
    }

    const quader_editor_core::ThickenOperationSchema thicken = quader_editor_core::thicken_operation_schema();
    if (sameOperation(toolId, thicken.operation_id) &&
            matchesAny(name, {thicken.thickness.id, "amount"})) {
        interactionSettings.thickenThickness = value;
        return true;
    }
    if (sameOperation(toolId, thicken.operation_id) &&
            matchesAny(name, {thicken.from_center.id})) {
        interactionSettings.thickenFromCenter = value != 0.0;
        return true;
    }

    const quader_editor_core::SliceQuadOperationSchema sliceQuad = quader_editor_core::slice_quad_operation_schema();
    if (sameOperation(toolId, sliceQuad.operation_id) &&
            matchesAny(name, {sliceQuad.x_slices.id})) {
        interactionSettings.sliceQuadXSlices = static_cast<int>(value);
        return true;
    }
    if (sameOperation(toolId, sliceQuad.operation_id) &&
            matchesAny(name, {sliceQuad.y_slices.id})) {
        interactionSettings.sliceQuadYSlices = static_cast<int>(value);
        return true;
    }

    const quader_editor_core::MergeByDistanceOperationSchema mergeByDistance =
            quader_editor_core::merge_by_distance_operation_schema();
    if (sameOperation(toolId, mergeByDistance.operation_id) &&
            matchesAny(name, {mergeByDistance.tolerance.id, "distance"})) {
        interactionSettings.mergeByDistanceTolerance = value;
        return true;
    }

    return false;
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
    return quader_editor_core::operation_for_preview_transaction_kind(kind);
}

} // namespace quader
