////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_operation_settings.hpp"

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
    const std::optional<quader_editor_core::EditorOperationOptionDescriptor> option =
            quader_editor_core::operation_option_for_setting(toolId, name);
    if (!option.has_value()) {
        return false;
    }

    switch (option->operation) {
    case quader_editor_core::EditorOperationId::bridge:
        interactionSettings.bridgeSteps = static_cast<int>(value);
        return true;
    case quader_editor_core::EditorOperationId::edge_bevel:
        if (option->option_id == std::string_view("width")) {
            interactionSettings.edgeBevelWidth = value;
            return true;
        }
        if (option->option_id == std::string_view("profile")) {
            interactionSettings.edgeBevelProfile = value;
            return true;
        }
        if (option->option_id == std::string_view("segments")) {
            interactionSettings.edgeBevelSegments = static_cast<int>(value);
            return true;
        }
        break;
    case quader_editor_core::EditorOperationId::thicken:
        if (option->option_id == std::string_view("thickness")) {
            interactionSettings.thickenThickness = value;
            return true;
        }
        if (option->option_id == std::string_view("from_center")) {
            interactionSettings.thickenFromCenter = value != 0.0;
            return true;
        }
        break;
    case quader_editor_core::EditorOperationId::slice_quad:
        if (option->option_id == std::string_view("x_slices")) {
            interactionSettings.sliceQuadXSlices = static_cast<int>(value);
            return true;
        }
        if (option->option_id == std::string_view("y_slices")) {
            interactionSettings.sliceQuadYSlices = static_cast<int>(value);
            return true;
        }
        break;
    case quader_editor_core::EditorOperationId::merge_by_distance:
        interactionSettings.mergeByDistanceTolerance = value;
        return true;
    default:
        break;
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
