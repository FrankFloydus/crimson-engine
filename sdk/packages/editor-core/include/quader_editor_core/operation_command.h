#pragma once

#include <quader_editor_core/operation_registry.h>

#include <optional>

namespace quader_editor_core {

enum class EditorOperationCommandPhase {
	execute,
	begin_preview,
	update_preview,
	commit_preview,
	cancel_preview,
};

struct EditorOperationCommand {
	EditorOperationId operation_id = EditorOperationId::bridge;
	EditorOperationCommandPhase phase = EditorOperationCommandPhase::execute;
	EditorOperationSettings settings = BridgeOperationSettings {};
};

struct EditorOperationCommandResult {
	CommandResult command;
	std::optional<PreviewTransactionFinishResult> preview_finish;
};

[[nodiscard]] std::optional<PreviewTransactionKind> preview_transaction_kind_for_operation(EditorOperationId operation_id);
[[nodiscard]] EditorOperationCommandResult execute_command(
		EditorSession& session,
		const EditorOperationCommand& command);
[[nodiscard]] EditorOperationCommandResult dispatch_editor_operation_command(
		EditorSession& session,
		const EditorOperationCommand& command);

} // namespace quader_editor_core
