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
  Move,
  Extend,
  Extrude,
};

/**
 * Enumerates ModalMovePreviewRoute values used by the modeling layer.
 */
enum class ModalMovePreviewRoute {
  None,
  ClearPreview,
  MoveTransformPreview,
  MovePatchPreview,
  ExtendPatchPreview,
  ExtrudePatchPreview,
};

/**
 * Enumerates ModalMoveCommitRoute values used by the modeling layer.
 */
enum class ModalMoveCommitRoute {
  None,
  MovePatch,
  ExtendPatch,
  ExtrudePatch,
};

/**
 * Enumerates ModalTransformDragTool values used by the modeling layer.
 */
enum class ModalTransformDragTool {
  Rotate,
  Scale,
};

/**
 * Enumerates ModalTransformPreviewRoute values used by the modeling layer.
 */
enum class ModalTransformPreviewRoute {
  None,
  ClearPreview,
  TransformPreview,
};

/**
 * Enumerates ModalTransformCommitRoute values used by the modeling layer.
 */
enum class ModalTransformCommitRoute {
  None,
  TransformPatch,
};

/**
 * Enumerates TransformDragOperation values used by the modeling layer.
 */
enum class TransformDragOperation {
  Transform,
  Extrude,
  Inset,
};

/**
 * Enumerates TransformSnapLandmarkKind values used by the modeling layer.
 */
enum class TransformSnapLandmarkKind {
  None,
  Vertex,
};

/**
 * Represents a Transform Snap Target value used by the modal modeling interaction layer.
 */
struct TransformSnapTarget {
  TransformSnapLandmarkKind kind = TransformSnapLandmarkKind::None;
  int object_id = -1;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Stores the Transform Drag State data contract used by the modal modeling interaction layer.
 */
struct TransformDragState {
	bool active = false;
        ViewportInteractionTool tool = ViewportInteractionTool::Select;
        TransformDragOperation operation = TransformDragOperation::Transform;
        TransformAxis axis = TransformAxis::None;
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
        ModalMoveDragTool tool = ModalMoveDragTool::Move;
        bool preview_uses_transform = false;
	quader::QVec3 previous_delta;
	quader::QVec3 delta;
        double epsilon = kToolEpsilon;
};

/**
 * Represents a Modal Move Update Plan value used by the modal modeling interaction layer.
 */
struct ModalMoveUpdatePlan {
	bool handled = false;
	bool changed = false;
	bool preview_active = false;
        ModalMovePreviewRoute route = ModalMovePreviewRoute::None;
};

/**
 * Represents a Modal Move Commit Input value used by the modal modeling interaction layer.
 */
struct ModalMoveCommitInput {
	bool active = false;
        ModalMoveDragTool tool = ModalMoveDragTool::Move;
        bool duplicate_active = false;
	quader::QVec3 delta;
        double epsilon = kToolEpsilon;
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
        ModalMoveCommitRoute route = ModalMoveCommitRoute::None;
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
        ModalTransformDragTool tool = ModalTransformDragTool::Rotate;
        bool preview_ok = false;
	double preview_angle_degrees = 0.0;
	quader::QVec3 preview_scale { 1.0F, 1.0F, 1.0F };
        double epsilon = kToolEpsilon;
};

/**
 * Represents a Modal Transform Update Plan value used by the modal modeling interaction layer.
 */
struct ModalTransformUpdatePlan {
	bool handled = false;
	bool preview_ok = false;
	bool preview_valid = false;
        ModalTransformPreviewRoute route = ModalTransformPreviewRoute::None;
};

/**
 * Represents a Modal Transform Commit Input value used by the modal modeling interaction layer.
 */
struct ModalTransformCommitInput {
	bool active = false;
        ModalTransformDragTool tool = ModalTransformDragTool::Rotate;
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
        ModalTransformCommitRoute route = ModalTransformCommitRoute::None;
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
        double epsilon = kToolEpsilon;
        // Host adapters that must convert the world-space drag into an
        // object-local component transform can ask the runtime to update only
        // the drag state. The host then applies the preview exactly once in
        // the correct local space.
        bool defer_session_update = false;
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
