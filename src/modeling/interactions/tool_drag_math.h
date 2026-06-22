////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <modeling/interactions/viewport_projection.h>
#include <modeling/polygon/document.h>

namespace quader_editor_core {

/**
 * Stores the Camera Drag State data contract used by the modal modeling interaction layer.
 */
struct CameraDragState {
	bool valid = false;
	CameraProjection projection = CameraProjection::perspective;
	quader_poly::Vec3 origin;
	quader_poly::Vec3 right { 1.0F, 0.0F, 0.0F };
	quader_poly::Vec3 up { 0.0F, 1.0F, 0.0F };
	quader_poly::Vec3 forward { 0.0F, 0.0F, -1.0F };
	quader_poly::Vec2 viewport_size { 1.0F, 1.0F };
	double orthographic_size = 1.0;
	double fov_degrees = 60.0;
};

/**
 * Stores the Drag Distance Result data contract used by the modal modeling interaction layer.
 */
struct DragDistanceResult {
	bool ok = false;
	double distance = 0.0;
};

/**
 * Stores the Drag Point Result data contract used by the modal modeling interaction layer.
 */
struct DragPointResult {
	bool ok = false;
	quader_poly::Vec3 point;
};

/**
 * Represents a Projected Axis Drag Input value used by the modal modeling interaction layer.
 */
struct ProjectedAxisDragInput {
	quader_poly::Vec2 screen_position;
	quader_poly::Vec2 press_position;
	quader_poly::Vec2 screen_axis { 1.0F, 0.0F };
	bool uses_screen_handle = false;
	bool has_axis_origin = false;
	double axis_origin = 0.0;
	bool has_current_axis_distance = false;
	double current_axis_distance = 0.0;
	bool anchor_behind = false;
	bool tip_behind = false;
	quader_poly::Vec2 screen_anchor;
	quader_poly::Vec2 screen_tip;
	double reference_distance = 1.0;
};

double drag_world_units_per_pixel(const CameraDragState& camera, quader_poly::Vec3 anchor);
DragPointResult horizontal_drag_point(const quader_poly::Ray& ray, quader_poly::Vec3 anchor);
DragPointResult drag_point_on_plane(const quader_poly::Ray& ray, quader_poly::Vec3 plane_point, quader_poly::Vec3 plane_normal);
DragDistanceResult vertical_drag_value(const quader_poly::Ray& ray, quader_poly::Vec3 anchor);
quader_poly::Vec3 screen_fallback_move_delta(
	const CameraDragState& camera,
	quader_poly::Vec2 screen_position,
	quader_poly::Vec2 press_position,
	quader_poly::Vec3 anchor,
	bool vertical);
quader_poly::Vec3 snapped_move_delta(quader_poly::Vec3 delta, bool vertical, double grid_world_size);
DragDistanceResult axis_distance_from_ray(
	quader_poly::Vec3 world_axis,
	quader_poly::Vec3 anchor,
	const quader_poly::Ray& ray,
	double parallel_epsilon = tool_epsilon);
DragDistanceResult projected_axis_drag_distance(const ProjectedAxisDragInput& input);

} // namespace quader_editor_core
