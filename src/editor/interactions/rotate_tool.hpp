////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/tool_identity.hpp>

#include <string>
#include <vector>

namespace quader_editor_core {

/**
 * Stores the Rotate Tool State data contract used by the modal modeling interaction layer.
 */
struct RotateToolState {
	bool active = false;
        TransformAxis axis = TransformAxis::None;
        quader_poly::Transform3 orientation;
        std::vector<int> object_ids;
	quader::QVec3 pivot;
	quader::QVec2 screen_pivot;
	quader::QVec2 start_screen;
	double start_angle = 0.0;
	double last_angle = 0.0;
	double drag_view_sign = 1.0;
	bool edge_on_linear_drag = false;
	quader::QVec2 edge_on_screen_axis { 1.0F, 0.0F };
	double edge_on_radius_pixels = 1.0;
	double accumulated_angle_degrees = 0.0;
	double preview_angle_degrees = 0.0;
	// Trackball uses a frozen world-space camera frame and unsnapped pointer angles.
	quader::QVec3 trackball_camera_right;
	quader::QVec3 trackball_camera_up;
	quader::QVec2 trackball_last_screen;
	double trackball_horizontal_radians = 0.0;
	double trackball_vertical_radians = 0.0;
	quader_poly::Transform3 trackball_rotation;
};

/**
 * Represents a Rotate Begin Input value used by the modal modeling interaction layer.
 */
struct RotateBeginInput {
  TransformAxis axis = TransformAxis::None;
  quader_poly::Transform3 orientation;
  std::vector<int> object_ids;
  quader::QVec3 pivot;
  quader::QVec2 screen_position;
  bool has_screen_pivot = false;
  quader::QVec2 screen_pivot;
  bool has_screen_tip = false;
  quader::QVec2 screen_tip;
  bool has_world_direction = false;
  quader::QVec3 world_direction;
  bool has_world_pivot = false;
  quader::QVec3 world_pivot;
  bool has_camera = false;
  quader_poly::Transform3 camera_transform;
};

/**
 * Stores the Rotate Begin Result data contract used by the modal modeling interaction layer.
 */
struct RotateBeginResult {
	bool ok = false;
	std::string message;
	RotateToolState state;
};

/**
 * Stores the Rotate Preview Result data contract used by the modal modeling interaction layer.
 */
struct RotatePreviewResult {
	bool ok = false;
	std::string message;
	double angle = 0.0;
	double preview_angle_degrees = 0.0;
	quader_poly::Transform3 transform;
	quader_poly::Transform3 pivoted_transform;
};

RotateBeginResult begin_rotate_drag(const RotateBeginInput& input);
RotatePreviewResult preview_rotate_from_screen_position(
	RotateToolState& state,
	quader::QVec2 screen_position,
	double snap_degrees,
	bool precision = false);
RotatePreviewResult preview_rotate_from_angle_degrees(RotateToolState& state, double angle_degrees, double snap_degrees);
/** Returns the linear preview rotation for both scalar-axis and trackball drags. */
quader_poly::Transform3 rotate_preview_transform(const RotateToolState& state);
void reset_rotate(RotateToolState& state);

} // namespace quader_editor_core
