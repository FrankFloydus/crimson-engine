////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/operations/operation_command.hpp>

#include <utility>
#include <variant>

namespace quader_editor_core {

namespace {

EditorOperationCommandResult command_result(CommandResult result) {
  return {std::move(result), std::nullopt};
}

CommandResult preview_not_supported_result() {
  return {false, false, "Operation does not support preview."};
}

CommandResult settings_mismatch_result() {
  return {false, false,
          "Operation command settings do not match the operation id."};
}

EditorOperationCommandResult
finish_preview_command(EditorSession &session, EditorOperationId operation_id,
                       bool commit) {
  const std::optional<PreviewTransactionKind> expected_kind =
      preview_transaction_kind_for_operation(operation_id);
  if (!expected_kind.has_value()) {
    return command_result(preview_not_supported_result());
  }

  const PreviewTransactionStatus before = session.preview_transaction_status();
  const PreviewTransactionFinishResult finish =
      commit ? session.commit_preview_transaction(*expected_kind)
             : session.cancel_preview_transaction(*expected_kind);

  CommandResult result;
  result.ok = !before.active || before.kind == *expected_kind;
  result.changed = result.ok && (finish.before.changed || finish.restored);
  if (!result.ok) {
    result.message = "Active preview belongs to another operation.";
  }
  return {std::move(result), finish};
}

} // namespace

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

EditorOperationCommandResult
execute_command(EditorSession &session, const EditorOperationCommand &command) {
  const EditorOperation *operation =
      editor_operation_for_id(command.operation_id);
  if (operation == nullptr) {
    return command_result({false, false, "Unknown operation."});
  }

  switch (command.phase) {
  case EditorOperationCommandPhase::Execute:
    if (!command_settings_match_operation(command.operation_id,
                                          command.settings)) {
      return command_result(settings_mismatch_result());
    }
    return command_result(session.apply_history_command(
        [operation, &command](EditorSession &editor_session) {
          return operation->execute(editor_session, command.settings);
        }));
  case EditorOperationCommandPhase::BeginPreview:
    if (!operation->supports_preview()) {
      return command_result(preview_not_supported_result());
    }
    if (!command_settings_match_operation(command.operation_id,
                                          command.settings)) {
      return command_result(settings_mismatch_result());
    }
    return command_result(operation->begin_preview(session, command.settings));
  case EditorOperationCommandPhase::UpdatePreview:
    if (!operation->supports_preview()) {
      return command_result(preview_not_supported_result());
    }
    if (!command_settings_match_operation(command.operation_id,
                                          command.settings)) {
      return command_result(settings_mismatch_result());
    }
    return command_result(operation->update_preview(session, command.settings));
  case EditorOperationCommandPhase::CommitPreview:
    return finish_preview_command(session, command.operation_id, true);
  case EditorOperationCommandPhase::CancelPreview:
    return finish_preview_command(session, command.operation_id, false);
  }

  return command_result({false, false, "Unknown operation command phase."});
}

EditorOperationCommandResult execute_command_with_history_state(
    EditorSession &session, const EditorOperationCommand &command,
    SessionState history_state) {
  if (command.phase != EditorOperationCommandPhase::Execute) {
    return execute_command(session, command);
  }

  const EditorOperation *operation =
      editor_operation_for_id(command.operation_id);
  if (operation == nullptr) {
    return command_result({false, false, "Unknown operation."});
  }
  if (!command_settings_match_operation(command.operation_id,
                                        command.settings)) {
    return command_result(settings_mismatch_result());
  }

  return command_result(session.apply_history_command_from(
      std::move(history_state),
      [operation, &command](EditorSession &editor_session) {
        return operation->execute(editor_session, command.settings);
      }));
}

EditorOperationCommandResult
dispatch_editor_operation_command(EditorSession &session,
                                  const EditorOperationCommand &command) {
  return execute_command(session, command);
}

} // namespace quader_editor_core
