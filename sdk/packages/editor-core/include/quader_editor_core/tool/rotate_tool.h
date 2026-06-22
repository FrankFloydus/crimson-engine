#pragma once

#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/tool_identity.h>

#include <string>
#include <vector>

namespace quader_editor_core {

struct RotateToolState {
	bool active = false;
	TransformAxis axis = TransformAxis::none;
	std::vector<int> object_ids;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_pivot;
	double start_angle = 0.0;
	double last_angle = 0.0;
	double drag_view_sign = 1.0;
	double accumulated_angle_degrees = 0.0;
	double preview_angle_degrees = 0.0;
};

struct RotateBeginInput {
	TransformAxis axis = TransformAxis::none;
	std::vector<int> object_ids;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_position;
	bool has_screen_pivot = false;
	quader_poly::Vec2 screen_pivot;
	bool has_world_direction = false;
	quader_poly::Vec3 world_direction;
	bool has_world_pivot = false;
	quader_poly::Vec3 world_pivot;
	bool has_camera = false;
	quader_poly::Transform3 camera_transform;
};

struct RotateBeginResult {
	bool ok = false;
	std::string message;
	RotateToolState state;
};

struct RotatePreviewResult {
	bool ok = false;
	std::string message;
	double angle = 0.0;
	double preview_angle_degrees = 0.0;
	quader_poly::Transform3 transform;
	quader_poly::Transform3 pivoted_transform;
};

RotateBeginResult begin_rotate_drag(const RotateBeginInput& input);
RotatePreviewResult preview_rotate_from_screen_position(RotateToolState& state, quader_poly::Vec2 screen_position, double snap_degrees);
RotatePreviewResult preview_rotate_from_angle_degrees(RotateToolState& state, double angle_degrees, double snap_degrees);
void reset_rotate(RotateToolState& state);

} // namespace quader_editor_core
