#pragma once

#include <quader_editor_core/tool/transform_drag.h>
#include <quader_editor_core/tool/tool_identity.h>
#include <quader_editor_core/tool/viewport_projection.h>
#include <quader_editor_core/tool_constants.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quader_editor_core {

enum class ViewportNavigationMode {
	none,
	orbit,
	pan,
	fly,
};

enum class ViewportMouseButton {
	none,
	left,
	middle,
	right,
	wheel_up,
	wheel_down,
	other,
};

enum class ViewportInteractionAction {
	none,
	consume,
	begin_fly_and_look,
	update_fly_look,
	end_fly,
	begin_middle_navigation,
	end_navigation,
	update_pan,
	update_orbit,
	update_hover,
	update_cut_hover,
	update_poly_hover,
	update_box_hover,
	update_move_drag,
	commit_move_drag,
	update_transform_drag,
	commit_transform_drag,
	clear_move_candidate,
	begin_move_candidate_drag,
	wait_move_candidate_drag,
	clear_selection_box,
	update_selection_box,
	zoom,
	adjust_extrude_cuts,
	route_select_button,
	route_cut_button,
	route_poly_button,
	route_transform_button,
	route_box_button,
	route_move_button,
	polygon_update_box_base,
	polygon_cancel_box_base,
	polygon_update_box_height,
	polygon_begin_box_base,
	polygon_commit_box_height,
	polygon_begin_box_height,
	polygon_begin_move_drag,
	polygon_begin_transform_drag,
	polygon_pick_select,
};

enum class ViewAxisStepDirection {
	left,
	right,
	up,
	down,
};

enum class ViewCubeHitKind {
	none,
	body,
	face,
};

struct ViewportNavigationState {
	ViewportNavigationMode mode = ViewportNavigationMode::none;
	quader_poly::Vec3 pivot;
	bool has_pivot = false;
	quader_poly::Transform3 orbit_snap_source_transform;
	bool has_orbit_snap_source = false;
	std::uint64_t fly_camera_id = 0;
	std::string fly_pane_id;
};

struct ViewportNavigationResult {
	bool ok = true;
	std::string message;
	quader_poly::Transform3 camera_transform;
	quader_poly::Vec3 pivot;
	bool has_camera_size = false;
	double camera_size = 0.0;
	bool orthogonal_snap = false;
	quader_poly::Transform3 unsnapped_camera_transform;
};

struct ViewportInteractionState {
	bool polygon_session = false;
	bool fly_active = false;
	bool can_begin_fly = false;
	bool move_drag_active = false;
	bool transform_drag_active = false;
	bool move_candidate_active = false;
	bool move_candidate_wait_for_drag_threshold = false;
	bool selection_box_active = false;
	bool cut_active = false;
	bool box_active = false;
	bool box_dragging_base = false;
	bool box_height_preview = false;
	bool navigating = false;
	ViewportNavigationMode navigation_mode = ViewportNavigationMode::none;
	ViewportInteractionTool active_tool = ViewportInteractionTool::select;
	bool has_selection = false;
	bool extrude_tool_active = false;
	ModalMoveDragTool move_drag_tool = ModalMoveDragTool::move;
};

struct ViewportMotionInput {
	bool left_down = false;
	bool middle_down = false;
	bool right_down = false;
	bool relative_is_zero = false;
	double move_candidate_drag_distance = 0.0;
	double move_candidate_drag_threshold = selection_box_default_drag_threshold;
};

struct ViewportButtonInput {
	ViewportMouseButton button = ViewportMouseButton::none;
	bool pressed = false;
};

struct ViewportOrbitInput {
	quader_poly::Transform3 camera_transform;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_delta;
	double sensitivity = navigation_orbit_radians_per_pixel;
	bool snap_to_orthogonal = false;
};

struct ViewportPanInput {
	ViewportCameraState camera;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_delta;
};

struct ViewportZoomInput {
	ViewportCameraState camera;
	quader_poly::Vec3 pivot;
	double steps = 0.0;
};

struct ViewportZoomToCursorInput {
	ViewportCameraState camera;
	quader_poly::Vec3 pivot;
	quader_poly::Vec3 ray_origin;
	quader_poly::Vec3 ray_direction;
	quader_poly::Vec3 cursor_target;
	bool has_cursor_target = false;
	double steps = 0.0;
};

struct ViewportFlyLookInput {
	quader_poly::Transform3 camera_transform;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_delta;
	double sensitivity = navigation_fly_look_radians_per_pixel;
};

struct ViewportFlyMoveInput {
	quader_poly::Transform3 camera_transform;
	quader_poly::Vec3 pivot;
	quader_poly::Vec3 local_direction;
	double distance = 0.0;
};

struct ViewportFrameBoundsInput {
	ViewportCameraState camera;
	quader_poly::Vec3 min_bounds;
	quader_poly::Vec3 max_bounds;
	double margin = 1.0;
};

struct ViewCubeFacePrimitive {
	ViewAxis axis = ViewAxis::x;
	int sign = 1;
	double depth = 0.0;
	quader_poly::Vec2 screen_position;
	std::vector<quader_poly::Vec2> polygon;
};

struct ViewCubeAxisMarkerPrimitive {
	ViewAxis axis = ViewAxis::x;
	int sign = 1;
	double depth = 0.0;
	quader_poly::Vec2 screen_position;
};

struct ViewCubePrimitiveInput {
	quader_poly::Transform3 camera_transform;
	quader_poly::Vec2 viewport_size;
	double widget_size = 76.0;
	quader_poly::Vec2 margin { 46.0F, 82.0F };
};

struct ViewCubePrimitives {
	bool ok = true;
	quader_poly::Vec2 screen_position;
	quader_poly::Vec2 rect_position;
	quader_poly::Vec2 rect_size;
	std::vector<ViewCubeFacePrimitive> faces;
	std::vector<ViewCubeAxisMarkerPrimitive> axis_markers;
};

struct ViewCubePickInput {
	ViewCubePrimitiveInput primitives;
	quader_poly::Vec2 screen_position;
};

struct ViewCubeHit {
	bool ok = true;
	bool hit = false;
	ViewCubeHitKind kind = ViewCubeHitKind::none;
	ViewAxis axis = ViewAxis::x;
	int sign = 1;
	double depth = 0.0;
	quader_poly::Vec2 screen_position;
};

quader_poly::Vec3 viewport_camera_forward(const quader_poly::Transform3& camera_transform);
quader_poly::Vec3 viewport_camera_right(const quader_poly::Transform3& camera_transform);
quader_poly::Vec3 viewport_camera_up(const quader_poly::Transform3& camera_transform);
double viewport_distance_to_pivot(const quader_poly::Transform3& camera_transform, quader_poly::Vec3 pivot);
double viewport_aspect_for_size(quader_poly::Vec2 viewport_size);
double viewport_vertical_fov_radians(double fov_degrees);
double viewport_orthographic_size_for_perspective(const quader_poly::Transform3& camera_transform, quader_poly::Vec3 pivot, double fov_degrees);
double viewport_orbit_sensitivity(double mouse_sensitivity);
double viewport_fly_look_sensitivity(double mouse_sensitivity);
double viewport_fly_move_distance(double delta_seconds, bool fast, bool slow);
void reset_viewport_navigation(ViewportNavigationState& state);
void begin_viewport_navigation(ViewportNavigationState& state, bool pan);
void begin_viewport_fly_navigation(ViewportNavigationState& state, std::uint64_t camera_id, std::string_view pane_id);
void end_viewport_navigation(ViewportNavigationState& state);
void ensure_viewport_navigation_pivot(ViewportNavigationState& state);
void clear_viewport_orbit_snap_source(ViewportNavigationState& state);
bool viewport_navigation_is_pan(const ViewportNavigationState& state);
bool viewport_navigation_is_orbit(const ViewportNavigationState& state);
bool viewport_navigation_is_fly(const ViewportNavigationState& state);
bool viewport_navigation_is_fly_camera(const ViewportNavigationState& state, std::uint64_t camera_id);
bool viewport_navigation_is_active(const ViewportNavigationState& state, bool view_cube_dragging = false);
ViewportNavigationResult viewport_orbit(const ViewportOrbitInput& input);
ViewportNavigationResult viewport_pan(const ViewportPanInput& input);
ViewportNavigationResult viewport_zoom(const ViewportZoomInput& input);
ViewportNavigationResult viewport_zoom_to_cursor(const ViewportZoomToCursorInput& input);
ViewportNavigationResult viewport_fly_look(const ViewportFlyLookInput& input);
ViewportNavigationResult viewport_fly_move(const ViewportFlyMoveInput& input);
ViewportNavigationResult viewport_frame_bounds(const ViewportFrameBoundsInput& input);
ViewportNavigationResult viewport_view_axis(const ViewportCameraState& camera, quader_poly::Vec3 pivot, ViewAxis axis, int sign);
ViewportNavigationResult viewport_step_view_axis(const ViewportCameraState& camera, quader_poly::Vec3 pivot, ViewAxisStepDirection direction);
ViewportInteractionAction plan_viewport_motion(const ViewportInteractionState& state, const ViewportMotionInput& input);
ViewportInteractionAction plan_viewport_button(const ViewportInteractionState& state, const ViewportButtonInput& input);
ViewCubePrimitives build_view_cube_primitives(const ViewCubePrimitiveInput& input);
ViewCubeHit pick_view_cube(const ViewCubePickInput& input);
bool view_cube_hits_match(const ViewCubeHit& left, const ViewCubeHit& right);

} // namespace quader_editor_core
