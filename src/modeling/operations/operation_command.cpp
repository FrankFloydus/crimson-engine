////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/operations/operation_command.h>

#include <utility>
#include <variant>

namespace quader_editor_core {

namespace {

EditorOperationCommandResult command_result(CommandResult result)
{
	return { std::move(result), std::nullopt };
}

CommandResult preview_not_supported_result()
{
	return { false, false, "Operation does not support preview." };
}

CommandResult settings_mismatch_result()
{
	return { false, false, "Operation command settings do not match the operation id." };
}

bool command_settings_match_operation(EditorOperationId operation_id, const EditorOperationSettings& settings)
{
	switch (operation_id) {
	case EditorOperationId::bridge:
		return std::holds_alternative<BridgeOperationSettings>(settings);
	case EditorOperationId::edge_bevel:
		return std::holds_alternative<EdgeBevelOperationSettings>(settings);
	case EditorOperationId::thicken:
		return std::holds_alternative<ThickenOperationSettings>(settings);
	case EditorOperationId::slice_quad:
		return std::holds_alternative<SliceQuadOperationSettings>(settings);
	case EditorOperationId::merge_by_distance:
		return std::holds_alternative<MergeByDistanceOperationSettings>(settings);
	case EditorOperationId::bevel_vertices:
		return std::holds_alternative<BevelVerticesOperationSettings>(settings);
	case EditorOperationId::duplicate_meshes:
		return std::holds_alternative<DuplicateMeshesOperationSettings>(settings);
	case EditorOperationId::recalculate_face_normals:
		return std::holds_alternative<RecalculateFaceNormalsOperationSettings>(settings);
	case EditorOperationId::assign_material:
		return std::holds_alternative<AssignMaterialOperationSettings>(settings);
	case EditorOperationId::connect_vertices:
	case EditorOperationId::dissolve_vertices:
	case EditorOperationId::connect_edges:
	case EditorOperationId::split_edges:
	case EditorOperationId::dissolve_edges:
	case EditorOperationId::merge_edges:
	case EditorOperationId::collapse_edges:
	case EditorOperationId::fill_hole:
	case EditorOperationId::combine_faces:
	case EditorOperationId::collapse_faces:
	case EditorOperationId::radial_align:
	case EditorOperationId::flip_face_normals:
	case EditorOperationId::shade_faces_smooth:
	case EditorOperationId::shade_faces_flat:
	case EditorOperationId::harden_edge_normals:
	case EditorOperationId::soften_edge_normals:
	case EditorOperationId::combine_meshes:
	case EditorOperationId::extract_faces:
	case EditorOperationId::detach_faces:
	case EditorOperationId::snap_vertices_to_active:
	case EditorOperationId::snap_edges_to_active:
	case EditorOperationId::merge_vertices_to_active:
	case EditorOperationId::merge_vertices_to_center:
	case EditorOperationId::delete_selection:
		return std::holds_alternative<SelectionOperationSettings>(settings);
	}
	return false;
}

EditorOperationCommandResult finish_preview_command(
		EditorSession& session,
		EditorOperationId operation_id,
		bool commit)
{
	const std::optional<PreviewTransactionKind> expected_kind =
		preview_transaction_kind_for_operation(operation_id);
	if (!expected_kind.has_value()) {
		return command_result(preview_not_supported_result());
	}

	const PreviewTransactionStatus before = session.preview_transaction_status();
	const PreviewTransactionFinishResult finish =
		commit ?
			session.commit_preview_transaction(*expected_kind) :
			session.cancel_preview_transaction(*expected_kind);

	CommandResult result;
	result.ok = !before.active || before.kind == *expected_kind;
	result.changed = result.ok && (finish.before.changed || finish.restored);
	if (!result.ok) {
		result.message = "Active preview belongs to another operation.";
	}
	return { std::move(result), finish };
}

} // namespace

std::optional<PreviewTransactionKind> preview_transaction_kind_for_operation(EditorOperationId operation_id)
{
	switch (operation_id) {
	case EditorOperationId::bridge:
		return PreviewTransactionKind::bridge;
	case EditorOperationId::edge_bevel:
		return PreviewTransactionKind::edge_bevel;
	case EditorOperationId::thicken:
		return PreviewTransactionKind::thicken;
	case EditorOperationId::slice_quad:
		return PreviewTransactionKind::slice_quad;
	case EditorOperationId::merge_by_distance:
		return PreviewTransactionKind::merge_by_distance;
	case EditorOperationId::bevel_vertices:
	case EditorOperationId::duplicate_meshes:
	case EditorOperationId::connect_vertices:
	case EditorOperationId::dissolve_vertices:
	case EditorOperationId::connect_edges:
	case EditorOperationId::split_edges:
	case EditorOperationId::dissolve_edges:
	case EditorOperationId::merge_edges:
	case EditorOperationId::collapse_edges:
	case EditorOperationId::fill_hole:
	case EditorOperationId::combine_faces:
	case EditorOperationId::collapse_faces:
	case EditorOperationId::radial_align:
	case EditorOperationId::flip_face_normals:
	case EditorOperationId::shade_faces_smooth:
	case EditorOperationId::shade_faces_flat:
	case EditorOperationId::harden_edge_normals:
	case EditorOperationId::soften_edge_normals:
	case EditorOperationId::combine_meshes:
	case EditorOperationId::extract_faces:
	case EditorOperationId::detach_faces:
	case EditorOperationId::snap_vertices_to_active:
	case EditorOperationId::snap_edges_to_active:
	case EditorOperationId::merge_vertices_to_active:
	case EditorOperationId::merge_vertices_to_center:
	case EditorOperationId::recalculate_face_normals:
	case EditorOperationId::assign_material:
	case EditorOperationId::delete_selection:
		return std::nullopt;
	}
	return std::nullopt;
}

EditorOperationCommandResult execute_command(
		EditorSession& session,
		const EditorOperationCommand& command)
{
	const EditorOperation *operation = editor_operation_for_id(command.operation_id);
	if (operation == nullptr) {
		return command_result({ false, false, "Unknown operation." });
	}

	switch (command.phase) {
	case EditorOperationCommandPhase::execute:
		if (!command_settings_match_operation(command.operation_id, command.settings)) {
			return command_result(settings_mismatch_result());
		}
		return command_result(session.apply_history_command([operation, &command](EditorSession& editor_session) {
			return operation->execute(editor_session, command.settings);
		}));
	case EditorOperationCommandPhase::begin_preview:
		if (!operation->supports_preview()) {
			return command_result(preview_not_supported_result());
		}
		if (!command_settings_match_operation(command.operation_id, command.settings)) {
			return command_result(settings_mismatch_result());
		}
		return command_result(operation->begin_preview(session, command.settings));
	case EditorOperationCommandPhase::update_preview:
		if (!operation->supports_preview()) {
			return command_result(preview_not_supported_result());
		}
		if (!command_settings_match_operation(command.operation_id, command.settings)) {
			return command_result(settings_mismatch_result());
		}
		return command_result(operation->update_preview(session, command.settings));
	case EditorOperationCommandPhase::commit_preview:
		return finish_preview_command(session, command.operation_id, true);
	case EditorOperationCommandPhase::cancel_preview:
		return finish_preview_command(session, command.operation_id, false);
	}

	return command_result({ false, false, "Unknown operation command phase." });
}

EditorOperationCommandResult dispatch_editor_operation_command(
		EditorSession& session,
		const EditorOperationCommand& command)
{
	return execute_command(session, command);
}

} // namespace quader_editor_core
