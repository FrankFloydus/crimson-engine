////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#ifndef QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H
#define QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H

#include <editor/interactions/tool_identity.h>
#include <editor/interactions/viewport_projection.h>
#include <mesh/polygon/document.h>

#include <string>

namespace quader_editor_core {

/**
 * Stores the Move Tool State data contract used by the modal modeling interaction layer.
 */
struct MoveToolState {
	bool active = false;
	TransformAxis axis = TransformAxis::none;
	quader::QVec3 world_axis;
	quader::QVec3 local_axis;
	quader::QVec3 center_local;
	quader::QVec3 center_world;
	quader::QVec2 screen_axis { 1.0F, 0.0F };
	quader::QVec2 start_screen;
	quader::QVec3 plane_start_point;
	double preview_distance = 0.0;
	quader::QVec3 preview_delta;
};

/**
 * Represents a Move Begin Input value used by the modal modeling interaction layer.
 */
struct MoveBeginInput {
	TransformAxis axis = TransformAxis::none;
	bool has_world_direction = false;
	quader::QVec3 world_direction;
	quader_poly::Transform3 model_transform;
	quader::QVec3 center_local;
	bool has_world_center = false;
	quader::QVec3 world_center;
	bool has_screen_axis = false;
	quader::QVec2 screen_pivot;
	quader::QVec2 screen_tip;
	quader::QVec2 screen_position;
	bool has_start_ray = false;
	quader_poly::Ray start_ray;
	bool has_camera = false;
	quader_poly::Transform3 camera_transform;
};

/**
 * Stores the Move Begin Result data contract used by the modal modeling interaction layer.
 */
struct MoveBeginResult {
	bool ok = false;
	std::string message;
	MoveToolState state;
};

/**
 * Stores the Move Preview Result data contract used by the modal modeling interaction layer.
 */
struct MovePreviewResult {
	bool ok = false;
	std::string message;
	double distance = 0.0;
	double preview_distance = 0.0;
	quader::QVec3 preview_delta;
};

MoveBeginResult begin_move_drag(const MoveBeginInput& input);
double move_axis_distance(const MoveToolState& state, const quader_poly::Ray& ray);
MovePreviewResult preview_move_from_distance(MoveToolState& state, double distance, bool precision, bool grid_enabled, double grid_size);
MovePreviewResult preview_move_from_drag_pixels(MoveToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size);
MovePreviewResult preview_move_to_axis_snap_target(MoveToolState& state, quader::QVec3 target_position);
MovePreviewResult preview_move_from_viewport(
	MoveToolState& state,
	quader::QVec2 screen_position,
	quader_poly::Ray ray,
	const ViewportCameraState& camera,
	bool precision,
	bool grid_enabled,
	double grid_size,
	bool snap_center_to_grid = false);
void reset_move(MoveToolState& state);

} // namespace quader_editor_core

#endif // QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H
