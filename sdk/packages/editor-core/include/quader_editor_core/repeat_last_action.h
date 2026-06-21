#pragma once

#include <quader_editor_core/operation_command.h>
#include <quader_editor_core/tool/transform_drag.h>

#include <optional>

namespace quader_editor_core {

enum class RepeatableActionKind {
	none,
	operation,
	transform,
};

enum class RepeatTransformDirectionMode {
	world_delta,
	selection_forward,
};

struct RepeatTransformAction {
	ViewportInteractionTool tool = ViewportInteractionTool::unknown;
	TransformDragOperation operation = TransformDragOperation::transform;
	TransformAxis axis = TransformAxis::none;
	bool duplicate_selected = false;
	quader_poly::Vec3 move_delta;
	double rotate_angle_degrees = 0.0;
	quader_poly::Vec3 scale { 1.0F, 1.0F, 1.0F };
	RepeatTransformDirectionMode direction_mode = RepeatTransformDirectionMode::world_delta;
	float signed_forward_distance = 0.0F;
};

struct RepeatLastActionState {
	RepeatableActionKind last_action_kind = RepeatableActionKind::none;
	std::optional<EditorOperationCommand> last_operation;
	std::optional<EditorOperationCommand> pending_preview_operation;
	std::optional<RepeatTransformAction> last_transform;
};

[[nodiscard]] bool operation_is_repeatable(EditorOperationId operation_id);
void record_repeatable_operation(
		RepeatLastActionState& state,
		const EditorOperationCommand& command,
		const CommandResult& result);
void record_repeatable_transform(
		RepeatLastActionState& state,
		const EditorSession& session,
		const TransformDragState& drag,
		bool duplicate_selected);
[[nodiscard]] CommandResult execute_repeatable_transform(
		EditorSession& session,
		const RepeatTransformAction& action);
[[nodiscard]] CommandResult execute_repeat_last_action(
		EditorSession& session,
		RepeatLastActionState& state);

} // namespace quader_editor_core
