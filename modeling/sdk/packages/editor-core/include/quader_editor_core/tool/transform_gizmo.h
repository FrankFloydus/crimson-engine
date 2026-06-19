#pragma once

#include <quader_editor_core/tool/tool_identity.h>
#include <quader_editor_core/tool/viewport_projection.h>
#include <quader_poly/document.h>

#include <vector>

namespace quader_editor_core {

struct TransformGizmoAxisBasis {
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 local_direction;
	quader_poly::Vec3 world_direction;
	quader_poly::Vec3 ring_world_a;
	quader_poly::Vec3 ring_world_b;
};

struct TransformGizmoPickCandidate {
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 local_direction;
	quader_poly::Vec3 world_direction;
	quader_poly::Vec2 screen_tip;
	bool visible = false;
};

struct TransformGizmoRingPickCandidate {
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 local_direction;
	quader_poly::Vec3 world_direction;
	quader_poly::Vec2 a;
	quader_poly::Vec2 b;
};

struct TransformGizmoPickHit {
	bool hit = false;
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 local_direction;
	quader_poly::Vec3 world_direction;
	quader_poly::Vec2 screen_tip;
	double distance = 0.0;
};

struct TransformGizmoAxisPrimitive {
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 world_start;
	quader_poly::Vec3 world_tip;
	quader_poly::Vec2 screen_start;
	quader_poly::Vec2 screen_tip;
	float alpha = 1.0F;
};

struct TransformGizmoPlanePrimitive {
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec3 world0;
	quader_poly::Vec3 world1;
	quader_poly::Vec3 world2;
	quader_poly::Vec3 world3;
	quader_poly::Vec2 screen0;
	quader_poly::Vec2 screen1;
	quader_poly::Vec2 screen2;
	quader_poly::Vec2 screen3;
};

struct TransformGizmoRingSegmentPrimitive {
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec2 screen_start;
	quader_poly::Vec2 screen_end;
};

struct TransformGizmoFrameInfo {
	bool ok = false;
	ViewportInteractionTool tool = ViewportInteractionTool::select;
	TransformAxis hover_axis = TransformAxis::none;
	TransformAxis active_axis = TransformAxis::none;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_pivot;
	double gizmo_scale = 1.0;
};

struct TransformGizmoFrame {
	TransformGizmoFrameInfo info;
	std::vector<TransformGizmoAxisPrimitive> axes;
	std::vector<TransformGizmoPlanePrimitive> planes;
	std::vector<TransformGizmoRingSegmentPrimitive> ring_segments;
};

struct TransformGizmoFrameInput {
	ViewportInteractionTool tool = ViewportInteractionTool::select;
	TransformAxis hover_axis = TransformAxis::none;
	TransformAxis active_axis = TransformAxis::none;
	bool has_selection = false;
	bool has_pivot = false;
	quader_poly::Vec3 pivot;
	ViewportCameraState camera;
	double grid_size = 1.0;
	double gizmo_size_pixels = 68.0;
	double pick_radius_pixels = 12.0;
	double gizmo_scale = 1.0;
	int ring_segments = 64;
};

TransformGizmoAxisBasis transform_gizmo_axis_basis(TransformAxis axis, const quader_poly::Transform3& global_transform = {});
double transform_gizmo_axis_visibility_score(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis);
double transform_gizmo_axis_projection_scale(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis);
bool transform_gizmo_axis_is_visible(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis);
double transform_gizmo_axis_alpha(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis);
quader_poly::Vec3 transform_gizmo_ring_point(quader_poly::Vec3 world_center, const TransformGizmoAxisBasis& axis, double radius, double angle);
TransformGizmoFrame build_transform_gizmo_frame(const TransformGizmoFrameInput& input);
TransformGizmoPickHit pick_transform_gizmo(const TransformGizmoFrameInput& input, quader_poly::Vec2 screen_position);
double distance_to_screen_segment_squared(quader_poly::Vec2 point, quader_poly::Vec2 a, quader_poly::Vec2 b);
TransformGizmoPickHit pick_move_gizmo_axis(quader_poly::Vec2 screen_position, quader_poly::Vec2 screen_center, const std::vector<TransformGizmoPickCandidate>& candidates, double pick_radius);
TransformGizmoPickHit pick_scale_gizmo_axis(quader_poly::Vec2 screen_position, quader_poly::Vec2 screen_pivot, const std::vector<TransformGizmoPickCandidate>& candidates, double pick_radius, double screen_size);
TransformGizmoPickHit pick_rotate_gizmo_axis(quader_poly::Vec2 screen_position, const std::vector<TransformGizmoRingPickCandidate>& candidates, double pick_radius);

} // namespace quader_editor_core
