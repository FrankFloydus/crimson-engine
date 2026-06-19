#pragma once

#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/move_tool.h>
#include <quader_editor_core/tool/rotate_tool.h>
#include <quader_editor_core/tool/scale_tool.h>
#include <quader_editor_core/tool/viewport_projection.h>
#include <quader_editor_core/tool_constants.h>

#include <string>
#include <string_view>

namespace quader_editor_core {

enum class ModalMoveDragTool {
	move,
	extend,
	extrude,
};

enum class ModalMovePreviewRoute {
	none,
	clear_preview,
	move_transform_preview,
	move_patch_preview,
	extend_patch_preview,
	extrude_patch_preview,
};

enum class ModalMoveCommitRoute {
	none,
	move_patch,
	extend_patch,
	extrude_patch,
};

enum class ModalTransformDragTool {
	rotate,
	scale,
};

enum class ModalTransformPreviewRoute {
	none,
	clear_preview,
	transform_preview,
};

enum class ModalTransformCommitRoute {
	none,
	transform_patch,
};

enum class TransformDragOperation {
	transform,
	extrude,
	inset,
};

enum class TransformSnapLandmarkKind {
	none,
	vertex,
};

struct TransformSnapTarget {
	TransformSnapLandmarkKind kind = TransformSnapLandmarkKind::none;
	int object_id = -1;
	quader_poly::ElementId vertex_id = quader_poly::invalid_element_id;
	quader_poly::Vec3 position;
};

struct TransformDragState {
	bool active = false;
	ViewportInteractionTool tool = ViewportInteractionTool::select;
	TransformDragOperation operation = TransformDragOperation::transform;
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 pivot;
	bool scene_transform_preview_eligible = false;
	TransformSnapTarget snap_target;
	MoveToolState move;
	RotateToolState rotate;
	ScaleToolState scale;
};

struct ModalMoveBeginInput {
	bool duplicate_requested = false;
	bool can_duplicate_selection = false;
	bool selection_uses_transform_preview = false;
};

struct ModalMoveBeginPlan {
	bool request_duplicate = false;
	bool preview_uses_transform = false;
	bool mark_selection_dirty = true;
};

struct ModalMoveUpdateInput {
	bool active = false;
	ModalMoveDragTool tool = ModalMoveDragTool::move;
	bool preview_uses_transform = false;
	quader_poly::Vec3 previous_delta;
	quader_poly::Vec3 delta;
	double epsilon = tool_epsilon;
};

struct ModalMoveUpdatePlan {
	bool handled = false;
	bool changed = false;
	bool preview_active = false;
	ModalMovePreviewRoute route = ModalMovePreviewRoute::none;
};

struct ModalMoveCommitInput {
	bool active = false;
	ModalMoveDragTool tool = ModalMoveDragTool::move;
	bool duplicate_active = false;
	quader_poly::Vec3 delta;
	double epsilon = tool_epsilon;
};

struct ModalMoveCommitPlan {
	bool handled = false;
	bool model_changed = false;
	bool restore_base_snapshot = false;
	bool restore_selection = false;
	bool clear_preview = true;
	ModalMoveCommitRoute route = ModalMoveCommitRoute::none;
	std::string undo_action;
};

struct ModalTransformBeginInput {
	bool duplicate_requested = false;
	bool can_duplicate_selection = false;
};

struct ModalTransformBeginPlan {
	bool ok = true;
	bool request_duplicate = false;
	bool mark_selection_dirty = true;
	std::string message;
};

struct ModalTransformUpdateInput {
	bool active = false;
	ModalTransformDragTool tool = ModalTransformDragTool::rotate;
	bool preview_ok = false;
	double preview_angle_degrees = 0.0;
	quader_poly::Vec3 preview_scale { 1.0F, 1.0F, 1.0F };
	double epsilon = tool_epsilon;
};

struct ModalTransformUpdatePlan {
	bool handled = false;
	bool preview_ok = false;
	bool preview_valid = false;
	ModalTransformPreviewRoute route = ModalTransformPreviewRoute::none;
};

struct ModalTransformCommitInput {
	bool active = false;
	ModalTransformDragTool tool = ModalTransformDragTool::rotate;
	bool preview_valid = false;
	bool duplicate_active = false;
};

struct ModalTransformCommitPlan {
	bool handled = false;
	bool model_changed = false;
	bool restore_base_snapshot = false;
	bool restore_selection = false;
	bool clear_preview = true;
	ModalTransformCommitRoute route = ModalTransformCommitRoute::none;
	std::string undo_action;
};

struct TransformDragPreviewInput {
	ViewportCameraState camera;
	quader_poly::Vec2 screen_position;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool precision = false;
	double rotate_snap_degrees = 15.0;
	double epsilon = tool_epsilon;
	bool vertex_snap_enabled = false;
	double vertex_snap_radius = 0.0;
	std::span<const PickObjectBounds> snap_object_bounds;
	std::span<const PickObjectFaceGeometry> snap_face_geometry;
};

ModalMoveDragTool modal_move_drag_tool_from_id(std::string_view tool_id);
std::string modal_move_drag_tool_id(ModalMoveDragTool tool);
ModalMoveBeginPlan plan_modal_move_drag_begin(const ModalMoveBeginInput& input);
ModalMoveUpdatePlan plan_modal_move_drag_update(const ModalMoveUpdateInput& input);
ModalMoveCommitPlan plan_modal_move_drag_commit(const ModalMoveCommitInput& input);

ModalTransformDragTool modal_transform_drag_tool_from_id(std::string_view tool_id);
std::string modal_transform_drag_tool_id(ModalTransformDragTool tool);
ModalTransformBeginPlan plan_modal_transform_drag_begin(const ModalTransformBeginInput& input);
ModalTransformUpdatePlan plan_modal_transform_drag_update(const ModalTransformUpdateInput& input);
ModalTransformCommitPlan plan_modal_transform_drag_commit(const ModalTransformCommitInput& input);

TransformDragOperation sanitize_transform_drag_operation(
		TransformDragOperation operation,
		ViewportInteractionTool tool,
		SelectionMode selection_mode);
void reset_transform_drag(TransformDragState& state);
CommandResult apply_transform_drag_preview(
		EditorSession& session,
		TransformDragState& state,
		const TransformDragPreviewInput& input);
std::vector<SelectionOverlayPoint> transform_drag_snap_overlay_points(
		const TransformDragState& state,
		const SelectionOverlayStyle& style = {});

} // namespace quader_editor_core
