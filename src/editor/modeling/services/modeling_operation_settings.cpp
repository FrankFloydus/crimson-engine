////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_operation_settings.hpp"

namespace quader {
namespace {

quader_editor_core::HalfCutAxis
to_runtime_half_cut_axis(ModelingHalfCutAxis axis) {
  switch (axis) {
  case ModelingHalfCutAxis::X:
    return quader_editor_core::HalfCutAxis::X;
  case ModelingHalfCutAxis::Y:
    return quader_editor_core::HalfCutAxis::Y;
  case ModelingHalfCutAxis::Z:
    return quader_editor_core::HalfCutAxis::Z;
  }
  return quader_editor_core::HalfCutAxis::X;
}

ModelingHalfCutAxis to_modeling_half_cut_axis(int axis) {
  switch (quader_editor_core::sanitize_half_cut_axis(axis)) {
  case quader_editor_core::HalfCutAxis::X:
    return ModelingHalfCutAxis::X;
  case quader_editor_core::HalfCutAxis::Y:
    return ModelingHalfCutAxis::Y;
  case quader_editor_core::HalfCutAxis::Z:
    return ModelingHalfCutAxis::Z;
  }
  return ModelingHalfCutAxis::X;
}

} // namespace

const char *fallback_tool_settings_label(ModelingToolSettingsTargetKind kind) {
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
  case ModelingToolSettingsTargetKind::HalfCut:
    return "Half-Cut";
  case ModelingToolSettingsTargetKind::None:
    break;
  }
  return "Tool Settings";
}

quader_editor_core::EditorOperationSettings settings_for_operation(
    quader_editor_core::EditorOperationId operation,
    const ModelingInteractionSettings &interaction_settings) {
  const std::optional<quader_editor_core::EditorOperationCommandSchema> schema =
      quader_editor_core::command_schema_for_operation(operation);
  if (!schema.has_value()) {
    return quader_editor_core::SelectionOperationSettings{};
  }

  switch (schema->settings_kind) {
  case quader_editor_core::EditorOperationSettingsKind::Bridge:
    return quader_editor_core::BridgeOperationSettings{
        .steps = interaction_settings.bridge_steps};
  case quader_editor_core::EditorOperationSettingsKind::EdgeBevel:
    return quader_editor_core::EdgeBevelOperationSettings{
        .width = interaction_settings.edge_bevel_width,
        .profile = interaction_settings.edge_bevel_profile,
        .segments = interaction_settings.edge_bevel_segments,
    };
  case quader_editor_core::EditorOperationSettingsKind::Thicken:
    return quader_editor_core::ThickenOperationSettings{
        .thickness = interaction_settings.thicken_thickness,
        .from_center = interaction_settings.thicken_from_center,
    };
  case quader_editor_core::EditorOperationSettingsKind::SliceQuad:
    return quader_editor_core::SliceQuadOperationSettings{
        .x_slices = interaction_settings.slice_quad_x_slices,
        .y_slices = interaction_settings.slice_quad_y_slices,
    };
  case quader_editor_core::EditorOperationSettingsKind::MergeByDistance:
    return quader_editor_core::MergeByDistanceOperationSettings{
        .tolerance = interaction_settings.merge_by_distance_tolerance,
    };
  case quader_editor_core::EditorOperationSettingsKind::HalfCut:
    return quader_editor_core::HalfCutOperationSettings{
        .axis = to_runtime_half_cut_axis(interaction_settings.half_cut_axis),
        .separate_parts = interaction_settings.half_cut_separate_parts,
        .fill_parts = interaction_settings.half_cut_fill_parts,
    };
  case quader_editor_core::EditorOperationSettingsKind::BevelVertices:
    return quader_editor_core::BevelVerticesOperationSettings{};
  case quader_editor_core::EditorOperationSettingsKind::DuplicateMeshes:
    return quader_editor_core::DuplicateMeshesOperationSettings{};
  case quader_editor_core::EditorOperationSettingsKind::
      RecalculateFaceNormals: {
    quader_editor_core::RecalculateFaceNormalsOperationSettings settings;
    settings.outside = true;
    return settings;
  }
  case quader_editor_core::EditorOperationSettingsKind::AssignMaterial:
    return quader_editor_core::AssignMaterialOperationSettings{};
  case quader_editor_core::EditorOperationSettingsKind::Selection:
    return quader_editor_core::SelectionOperationSettings{};
  }
  return quader_editor_core::SelectionOperationSettings{};
}

bool apply_modeling_interaction_setting(
    ModelingInteractionSettings &interaction_settings, std::string_view tool_id,
    std::string_view name, double value) {
  const std::optional<quader_editor_core::EditorOperationOptionDescriptor>
      option = quader_editor_core::operation_option_for_setting(tool_id, name);
  if (!option.has_value()) {
    return false;
  }

  switch (option->operation) {
  case quader_editor_core::EditorOperationId::Bridge:
    interaction_settings.bridge_steps = static_cast<int>(value);
    return true;
  case quader_editor_core::EditorOperationId::EdgeBevel:
    if (option->option_id == std::string_view("width")) {
      interaction_settings.edge_bevel_width = value;
      return true;
    }
    if (option->option_id == std::string_view("profile")) {
      interaction_settings.edge_bevel_profile = value;
      return true;
    }
    if (option->option_id == std::string_view("segments")) {
      interaction_settings.edge_bevel_segments = static_cast<int>(value);
      return true;
    }
    break;
  case quader_editor_core::EditorOperationId::Thicken:
    if (option->option_id == std::string_view("thickness")) {
      interaction_settings.thicken_thickness = value;
      return true;
    }
    if (option->option_id == std::string_view("from_center")) {
      interaction_settings.thicken_from_center = value != 0.0;
      return true;
    }
    break;
  case quader_editor_core::EditorOperationId::SliceQuad:
    if (option->option_id == std::string_view("x_slices")) {
      interaction_settings.slice_quad_x_slices = static_cast<int>(value);
      return true;
    }
    if (option->option_id == std::string_view("y_slices")) {
      interaction_settings.slice_quad_y_slices = static_cast<int>(value);
      return true;
    }
    break;
  case quader_editor_core::EditorOperationId::MergeByDistance:
    interaction_settings.merge_by_distance_tolerance = value;
    return true;
  case quader_editor_core::EditorOperationId::HalfCut:
    if (option->option_id == std::string_view("axis")) {
      interaction_settings.half_cut_axis =
          to_modeling_half_cut_axis(static_cast<int>(value));
      return true;
    }
    if (option->option_id == std::string_view("separate_parts")) {
      interaction_settings.half_cut_separate_parts = value != 0.0;
      return true;
    }
    if (option->option_id == std::string_view("fill_parts")) {
      interaction_settings.half_cut_fill_parts = value != 0.0;
      return true;
    }
    break;
  default:
    break;
  }
  return false;
}

ModelingToolSettingsTargetKind tool_settings_target_kind_for_preview(
    quader_editor_core::PreviewTransactionKind kind) {
  switch (kind) {
  case quader_editor_core::PreviewTransactionKind::Bridge:
    return ModelingToolSettingsTargetKind::Bridge;
  case quader_editor_core::PreviewTransactionKind::EdgeBevel:
    return ModelingToolSettingsTargetKind::EdgeBevel;
  case quader_editor_core::PreviewTransactionKind::Thicken:
    return ModelingToolSettingsTargetKind::Thicken;
  case quader_editor_core::PreviewTransactionKind::SliceQuad:
    return ModelingToolSettingsTargetKind::SliceQuad;
  case quader_editor_core::PreviewTransactionKind::MergeByDistance:
    return ModelingToolSettingsTargetKind::MergeByDistance;
  case quader_editor_core::PreviewTransactionKind::HalfCut:
    return ModelingToolSettingsTargetKind::HalfCut;
  case quader_editor_core::PreviewTransactionKind::None:
  case quader_editor_core::PreviewTransactionKind::Transform:
    break;
  }
  return ModelingToolSettingsTargetKind::None;
}

std::optional<quader_editor_core::EditorOperationId>
operation_for_preview_kind(quader_editor_core::PreviewTransactionKind kind) {
  return quader_editor_core::operation_for_preview_transaction_kind(kind);
}

} // namespace quader
