////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/move_tool.hpp>
#include <editor/interactions/rotate_tool.hpp>
#include <editor/interactions/scale_tool.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>

#include <string>
#include <string_view>

namespace quader_editor_core {

/**
 * Enumerates ModalMoveDragTool values used by the modeling layer.
 */
enum class ModalMoveDragTool {
	move,
	extend,
	extrude,
};

/**
 * Enumerates ModalMovePreviewRoute values used by the modeling layer.
 */
enum class ModalMovePreviewRoute {
	none,
	clear_preview,
	move_transform_preview,
	move_patch_preview,
	extend_patch_preview,
	extrude_patch_preview,
};

/**
 * Enumerates ModalMoveCommitRoute values used by the modeling layer.
 */
enum class ModalMoveCommitRoute {
	none,
	move_patch,
	extend_patch,
	extrude_patch,
};

/**
 * Enumerates ModalTransformDragTool values used by the modeling layer.
 */
enum class ModalTransformDragTool {
	rotate,
	scale,
};

/**
 * Enumerates ModalTransformPreviewRoute values used by the modeling layer.
 */
enum class ModalTransformPreviewRoute {
	none,
	clear_preview,
	transform_preview,
};

/**
 * Enumerates ModalTransformCommitRoute values used by the modeling layer.
 */
enum class ModalTransformCommitRoute {
	none,
	transform_patch,
};

/**
 * Enumerates TransformDragOperation values used by the modeling layer.
 */
enum class TransformDragOperation {
	transform,
	extrude,
	inset,
};

/**
 * Enumerates TransformSnapLandmarkKind values used by the modeling layer.
 */
enum class TransformSnapLandmarkKind {
	none,
	vertex,
};

/**
 * Represents a Transform Snap Target value used by the modal modeling interaction layer.
 */
struct TransformSnapTarget {
	TransformSnapLandmarkKind kind = TransformSnapLandmarkKind::none;
	int object_id = -1;
	quader_poly::ElementId vertex_id = quader_poly::invalid_element_id;
	quader::QVec3 position;
};

/**
 * Stores the Transform Drag State data contract used by the modal modeling interaction layer.
 */
struct TransformDragState {
	bool active = false;
	ViewportInteractionTool tool = ViewportInteractionTool::select;
	TransformDragOperation operation = TransformDragOperation::transform;
	TransformAxis axis = TransformAxis::none;
	quader::QVec3 pivot;
	bool scene_transform_preview_eligible = false;
	TransformSnapTarget snap_target;
	MoveToolState move;
	RotateToolState rotate;
	ScaleToolState scale;
};

/**
 * Represents a Modal Move Begin Input value used by the modal modeling interaction layer.
 */
struct ModalMoveBeginInput {
	bool duplicate_requested = false;
	bool can_duplicate_selection = false;
	bool selection_uses_transform_preview = false;
};

/**
 * Represents a Modal Move Begin Plan value used by the modal modeling interaction layer.
 */
struct ModalMoveBeginPlan {
	bool request_duplicate = false;
	bool preview_uses_transform = false;
	bool mark_selection_dirty = true;
};

/**
 * Represents a Modal Move Update Input value used by the modal modeling interaction layer.
 */
struct ModalMoveUpdateInput {
	bool active = false;
	ModalMoveDragTool tool = ModalMoveDragTool::move;
	bool preview_uses_transform = false;
	quader::QVec3 previous_delta;
	quader::QVec3 delta;
	double epsilon = tool_epsilon;
};

/**
 * Represents a Modal Move Update Plan value used by the modal modeling interaction layer.
 */
struct ModalMoveUpdatePlan {
	bool handled = false;
	bool changed = false;
	bool preview_active = false;
	ModalMovePreviewRoute route = ModalMovePreviewRoute::none;
};

/**
 * Represents a Modal Move Commit Input value used by the modal modeling interaction layer.
 */
struct ModalMoveCommitInput {
	bool active = false;
	ModalMoveDragTool tool = ModalMoveDragTool::move;
	bool duplicate_active = false;
	quader::QVec3 delta;
	double epsilon = tool_epsilon;
};

/**
 * Represents a Modal Move Commit Plan value used by the modal modeling interaction layer.
 */
struct ModalMoveCommitPlan {
	bool handled = false;
	bool model_changed = false;
	bool restore_base_snapshot = false;
	bool restore_selection = false;
	bool clear_preview = true;
	ModalMoveCommitRoute route = ModalMoveCommitRoute::none;
	std::string undo_action;
};

/**
 * Represents a Modal Transform Begin Input value used by the modal modeling interaction layer.
 */
struct ModalTransformBeginInput {
	bool duplicate_requested = false;
	bool can_duplicate_selection = false;
};

/**
 * Represents a Modal Transform Begin Plan value used by the modal modeling interaction layer.
 */
struct ModalTransformBeginPlan {
	bool ok = true;
	bool request_duplicate = false;
	bool mark_selection_dirty = true;
	std::string message;
};

/**
 * Represents a Modal Transform Update Input value used by the modal modeling interaction layer.
 */
struct ModalTransformUpdateInput {
	bool active = false;
	ModalTransformDragTool tool = ModalTransformDragTool::rotate;
	bool preview_ok = false;
	double preview_angle_degrees = 0.0;
	quader::QVec3 preview_scale { 1.0F, 1.0F, 1.0F };
	double epsilon = tool_epsilon;
};

/**
 * Represents a Modal Transform Update Plan value used by the modal modeling interaction layer.
 */
struct ModalTransformUpdatePlan {
	bool handled = false;
	bool preview_ok = false;
	bool preview_valid = false;
	ModalTransformPreviewRoute route = ModalTransformPreviewRoute::none;
};

/**
 * Represents a Modal Transform Commit Input value used by the modal modeling interaction layer.
 */
struct ModalTransformCommitInput {
	bool active = false;
	ModalTransformDragTool tool = ModalTransformDragTool::rotate;
	bool preview_valid = false;
	bool duplicate_active = false;
};

/**
 * Represents a Modal Transform Commit Plan value used by the modal modeling interaction layer.
 */
struct ModalTransformCommitPlan {
	bool handled = false;
	bool model_changed = false;
	bool restore_base_snapshot = false;
	bool restore_selection = false;
	bool clear_preview = true;
	ModalTransformCommitRoute route = ModalTransformCommitRoute::none;
	std::string undo_action;
};

/**
 * Represents a Transform Drag Preview Input value used by the modal modeling interaction layer.
 */
struct TransformDragPreviewInput {
	ViewportCameraState camera;
	quader::QVec2 screen_position;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool grid_enabled = true;
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
