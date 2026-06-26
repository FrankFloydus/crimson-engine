////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/operations/operation_registry.h>

#include <optional>

namespace quader_editor_core {

/**
 * Enumerates EditorOperationCommandPhase values used by the modeling layer.
 */
enum class EditorOperationCommandPhase {
	execute,
	begin_preview,
	update_preview,
	commit_preview,
	cancel_preview,
};

/**
 * Enumerates EditorOperationSettingsKind values used by operation command validation.
 */
enum class EditorOperationSettingsKind {
	selection,
	bridge,
	edge_bevel,
	thicken,
	slice_quad,
	merge_by_distance,
	bevel_vertices,
	duplicate_meshes,
	recalculate_face_normals,
	assign_material,
};

/**
 * Stores the command-schema metadata for an editor operation.
 */
struct EditorOperationCommandSchema {
	EditorOperationId operation_id = EditorOperationId::bridge;
	EditorOperationSettingsKind settings_kind = EditorOperationSettingsKind::selection;
	std::optional<PreviewTransactionKind> preview_transaction_kind;
};

/**
 * Represents an Editor Operation Command value used by the modeling runtime domain.
 */
struct EditorOperationCommand {
	EditorOperationId operation_id = EditorOperationId::bridge;
	EditorOperationCommandPhase phase = EditorOperationCommandPhase::execute;
	EditorOperationSettings settings = BridgeOperationSettings {};
};

/**
 * Stores the Editor Operation Command Result data contract used by the modeling runtime domain.
 */
struct EditorOperationCommandResult {
	CommandResult command;
	std::optional<PreviewTransactionFinishResult> preview_finish;
};

[[nodiscard]] std::optional<EditorOperationCommandSchema> command_schema_for_operation(EditorOperationId operation_id);
[[nodiscard]] bool command_settings_match_operation(EditorOperationId operation_id, const EditorOperationSettings& settings);
[[nodiscard]] std::optional<PreviewTransactionKind> preview_transaction_kind_for_operation(EditorOperationId operation_id);
[[nodiscard]] std::optional<EditorOperationId> operation_for_preview_transaction_kind(PreviewTransactionKind kind);
[[nodiscard]] EditorOperationCommandResult execute_command(
		EditorSession& session,
		const EditorOperationCommand& command);
[[nodiscard]] EditorOperationCommandResult dispatch_editor_operation_command(
		EditorSession& session,
		const EditorOperationCommand& command);

} // namespace quader_editor_core
