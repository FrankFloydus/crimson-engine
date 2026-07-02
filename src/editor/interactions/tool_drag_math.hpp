////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/interactions/viewport_projection.hpp>
#include <mesh/polygon/document.hpp>

namespace quader_editor_core {

/**
 * Stores the Camera Drag State data contract used by the modal modeling interaction layer.
 */
struct CameraDragState {
	bool valid = false;
        CameraProjection projection = CameraProjection::Perspective;
        quader::QVec3 origin;
	quader::QVec3 right { 1.0F, 0.0F, 0.0F };
	quader::QVec3 up { 0.0F, 1.0F, 0.0F };
	quader::QVec3 forward { 0.0F, 0.0F, -1.0F };
	quader::QVec2 viewport_size { 1.0F, 1.0F };
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
	quader::QVec3 point;
};

/**
 * Represents a Projected QAxis Drag Input value used by the modal modeling interaction layer.
 */
struct ProjectedAxisDragInput {
	quader::QVec2 screen_position;
	quader::QVec2 press_position;
	quader::QVec2 screen_axis { 1.0F, 0.0F };
	bool uses_screen_handle = false;
	bool has_axis_origin = false;
	double axis_origin = 0.0;
	bool has_current_axis_distance = false;
	double current_axis_distance = 0.0;
	bool anchor_behind = false;
	bool tip_behind = false;
	quader::QVec2 screen_anchor;
	quader::QVec2 screen_tip;
	double reference_distance = 1.0;
};

double drag_world_units_per_pixel(const CameraDragState& camera, quader::QVec3 anchor);
DragPointResult horizontal_drag_point(const quader_poly::Ray& ray, quader::QVec3 anchor);
DragPointResult drag_point_on_plane(const quader_poly::Ray& ray, quader::QVec3 plane_point, quader::QVec3 plane_normal);
DragDistanceResult vertical_drag_value(const quader_poly::Ray& ray, quader::QVec3 anchor);
quader::QVec3 screen_fallback_move_delta(
	const CameraDragState& camera,
	quader::QVec2 screen_position,
	quader::QVec2 press_position,
	quader::QVec3 anchor,
	bool vertical);
quader::QVec3 snapped_move_delta(quader::QVec3 delta, bool vertical, double grid_world_size);
DragDistanceResult
axis_distance_from_ray(quader::QVec3 world_axis, quader::QVec3 anchor,
                       const quader_poly::Ray &ray,
                       double parallel_epsilon = kToolEpsilon,
                       double vector_epsilon = kToolEpsilon);
DragDistanceResult projected_axis_drag_distance(const ProjectedAxisDragInput& input);

} // namespace quader_editor_core
