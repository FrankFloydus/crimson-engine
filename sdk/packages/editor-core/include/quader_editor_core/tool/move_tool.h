#ifndef QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H
#define QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H

#include <quader_editor_core/tool/tool_identity.h>
#include <quader_editor_core/tool/viewport_projection.h>
#include <quader_poly/document.h>

#include <string>

namespace quader_editor_core {

struct MoveToolState {
	bool active = false;
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 world_axis;
	quader_poly::Vec3 local_axis;
	quader_poly::Vec3 center_local;
	quader_poly::Vec3 center_world;
	quader_poly::Vec2 screen_axis { 1.0F, 0.0F };
	quader_poly::Vec2 start_screen;
	quader_poly::Vec3 plane_start_point;
	double preview_distance = 0.0;
	quader_poly::Vec3 preview_delta;
};

struct MoveBeginInput {
	TransformAxis axis = TransformAxis::none;
	bool has_world_direction = false;
	quader_poly::Vec3 world_direction;
	quader_poly::Transform3 model_transform;
	quader_poly::Vec3 center_local;
	bool has_world_center = false;
	quader_poly::Vec3 world_center;
	bool has_screen_axis = false;
	quader_poly::Vec2 screen_pivot;
	quader_poly::Vec2 screen_tip;
	quader_poly::Vec2 screen_position;
	bool has_start_ray = false;
	quader_poly::Ray start_ray;
	bool has_camera = false;
	quader_poly::Transform3 camera_transform;
};

struct MoveBeginResult {
	bool ok = false;
	std::string message;
	MoveToolState state;
};

struct MovePreviewResult {
	bool ok = false;
	std::string message;
	double distance = 0.0;
	double preview_distance = 0.0;
	quader_poly::Vec3 preview_delta;
};

MoveBeginResult begin_move_drag(const MoveBeginInput& input);
double move_axis_distance(const MoveToolState& state, const quader_poly::Ray& ray);
MovePreviewResult preview_move_from_distance(MoveToolState& state, double distance, bool precision, bool grid_enabled, double grid_size);
MovePreviewResult preview_move_from_drag_pixels(MoveToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size);
MovePreviewResult preview_move_to_axis_snap_target(MoveToolState& state, quader_poly::Vec3 target_position);
MovePreviewResult preview_move_from_viewport(
	MoveToolState& state,
	quader_poly::Vec2 screen_position,
	quader_poly::Ray ray,
	const ViewportCameraState& camera,
	bool precision,
	bool grid_enabled,
	double grid_size,
	bool snap_center_to_grid = false);
void reset_move(MoveToolState& state);

} // namespace quader_editor_core

#endif // QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H
