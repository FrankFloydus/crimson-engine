////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/operations/operation_command.hpp>

#include <variant>

namespace quader_editor_core {

std::optional<EditorOperationCommandSchema>
command_schema_for_operation(EditorOperationId operation_id) {
  switch (operation_id) {
  case EditorOperationId::Bridge:
    return EditorOperationCommandSchema{
        operation_id,
        EditorOperationSettingsKind::Bridge,
        PreviewTransactionKind::Bridge,
    };
  case EditorOperationId::EdgeBevel:
    return EditorOperationCommandSchema{
        operation_id,
        EditorOperationSettingsKind::EdgeBevel,
        PreviewTransactionKind::EdgeBevel,
    };
  case EditorOperationId::Thicken:
    return EditorOperationCommandSchema{
        operation_id,
        EditorOperationSettingsKind::Thicken,
        PreviewTransactionKind::Thicken,
    };
  case EditorOperationId::SliceQuad:
    return EditorOperationCommandSchema{
        operation_id,
        EditorOperationSettingsKind::SliceQuad,
        PreviewTransactionKind::SliceQuad,
    };
  case EditorOperationId::MergeByDistance:
    return EditorOperationCommandSchema{
        operation_id,
        EditorOperationSettingsKind::MergeByDistance,
        PreviewTransactionKind::MergeByDistance,
    };
  case EditorOperationId::HalfCut:
    return EditorOperationCommandSchema{
        operation_id,
        EditorOperationSettingsKind::HalfCut,
        PreviewTransactionKind::HalfCut,
    };
  case EditorOperationId::BevelVertices:
    return EditorOperationCommandSchema{
        operation_id, EditorOperationSettingsKind::BevelVertices, std::nullopt};
  case EditorOperationId::DuplicateMeshes:
    return EditorOperationCommandSchema{
        operation_id, EditorOperationSettingsKind::DuplicateMeshes,
        std::nullopt};
  case EditorOperationId::RecalculateFaceNormals:
    return EditorOperationCommandSchema{
        operation_id, EditorOperationSettingsKind::RecalculateFaceNormals,
        std::nullopt};
  case EditorOperationId::AssignMaterial:
    return EditorOperationCommandSchema{
        operation_id, EditorOperationSettingsKind::AssignMaterial,
        std::nullopt};
  case EditorOperationId::ConnectVertices:
  case EditorOperationId::DissolveVertices:
  case EditorOperationId::ConnectEdges:
  case EditorOperationId::SplitEdges:
  case EditorOperationId::DissolveEdges:
  case EditorOperationId::MergeEdges:
  case EditorOperationId::CollapseEdges:
  case EditorOperationId::FillHole:
  case EditorOperationId::RemoveDoubles:
  case EditorOperationId::CombineFaces:
  case EditorOperationId::CollapseFaces:
  case EditorOperationId::RadialAlign:
  case EditorOperationId::FlipFaceNormals:
  case EditorOperationId::ShadeFacesSmooth:
  case EditorOperationId::ShadeFacesFlat:
  case EditorOperationId::DefaultEdgeNormals:
  case EditorOperationId::HardenEdgeNormals:
  case EditorOperationId::SoftenEdgeNormals:
  case EditorOperationId::CombineMeshes:
  case EditorOperationId::ExtractFaces:
  case EditorOperationId::DetachFaces:
  case EditorOperationId::SnapVerticesToActive:
  case EditorOperationId::SnapEdgesToActive:
  case EditorOperationId::MergeVerticesToActive:
  case EditorOperationId::MergeVerticesToCenter:
  case EditorOperationId::DeleteSelection:
    return EditorOperationCommandSchema{
        operation_id, EditorOperationSettingsKind::Selection, std::nullopt};
  }
  return std::nullopt;
}

bool command_settings_match_operation(EditorOperationId operation_id,
                                      const EditorOperationSettings &settings) {
  const std::optional<EditorOperationCommandSchema> schema =
      command_schema_for_operation(operation_id);
  if (!schema.has_value()) {
    return false;
  }

  switch (schema->settings_kind) {
  case EditorOperationSettingsKind::Selection:
    return std::holds_alternative<SelectionOperationSettings>(settings);
  case EditorOperationSettingsKind::Bridge:
    return std::holds_alternative<BridgeOperationSettings>(settings);
  case EditorOperationSettingsKind::EdgeBevel:
    return std::holds_alternative<EdgeBevelOperationSettings>(settings);
  case EditorOperationSettingsKind::Thicken:
    return std::holds_alternative<ThickenOperationSettings>(settings);
  case EditorOperationSettingsKind::SliceQuad:
    return std::holds_alternative<SliceQuadOperationSettings>(settings);
  case EditorOperationSettingsKind::MergeByDistance:
    return std::holds_alternative<MergeByDistanceOperationSettings>(settings);
  case EditorOperationSettingsKind::HalfCut:
    return std::holds_alternative<HalfCutOperationSettings>(settings);
  case EditorOperationSettingsKind::BevelVertices:
    return std::holds_alternative<BevelVerticesOperationSettings>(settings);
  case EditorOperationSettingsKind::DuplicateMeshes:
    return std::holds_alternative<DuplicateMeshesOperationSettings>(settings);
  case EditorOperationSettingsKind::RecalculateFaceNormals:
    return std::holds_alternative<RecalculateFaceNormalsOperationSettings>(
        settings);
  case EditorOperationSettingsKind::AssignMaterial:
    return std::holds_alternative<AssignMaterialOperationSettings>(settings);
  }
  return false;
}

std::optional<PreviewTransactionKind>
preview_transaction_kind_for_operation(EditorOperationId operation_id) {
  const std::optional<EditorOperationCommandSchema> schema =
      command_schema_for_operation(operation_id);
  return schema.has_value() ? schema->preview_transaction_kind : std::nullopt;
}

std::optional<EditorOperationId>
operation_for_preview_transaction_kind(PreviewTransactionKind kind) {
  for (const EditorOperationRegistryEntry &entry :
       operation_registry().entries()) {
    const std::optional<EditorOperationCommandSchema> schema =
        command_schema_for_operation(entry.operation_id);
    if (schema.has_value() && schema->preview_transaction_kind == kind) {
      return entry.operation_id;
    }
  }
  return std::nullopt;
}

} // namespace quader_editor_core
