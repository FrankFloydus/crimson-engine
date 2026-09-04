////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/interactions/tool_identity.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quader_editor_core {

/**
 * Enumerates ViewportNavigationMode values used by the modeling layer.
 */
enum class ViewportNavigationMode {
  None,
  Orbit,
  Pan,
  Fly,
};

/**
 * Enumerates ViewAxisStepDirection values used by the modeling layer.
 */
enum class ViewAxisStepDirection {
  Left,
  Right,
  Up,
  Down,
};

/**
 * Enumerates ViewCubeHitKind values used by the modeling layer.
 */
enum class ViewCubeHitKind {
  None,
  Body,
  Face,
};

/**
 * Stores the Viewport Navigation State data contract used by the modal modeling interaction layer.
 */
struct ViewportNavigationState {
  ViewportNavigationMode mode = ViewportNavigationMode::None;
  quader::QVec3 pivot;
  bool has_pivot = false;
  quader_poly::Transform3 orbit_snap_source_transform;
  bool has_orbit_snap_source = false;
  std::uint64_t fly_camera_id = 0;
  std::string fly_pane_id;
};

/**
 * Stores the Viewport Navigation Result data contract used by the modal modeling interaction layer.
 */
struct ViewportNavigationResult {
	bool ok = true;
	std::string message;
	quader_poly::Transform3 camera_transform;
	quader::QVec3 pivot;
	bool has_camera_size = false;
	double camera_size = 0.0;
	bool orthogonal_snap = false;
	quader_poly::Transform3 unsnapped_camera_transform;
};

/**
 * Represents a Viewport Orbit Input value used by the modal modeling interaction layer.
 */
struct ViewportOrbitInput {
	quader_poly::Transform3 camera_transform;
	quader::QVec3 pivot;
	quader::QVec2 screen_delta;
        double sensitivity = kNavigationOrbitRadiansPerPixel;
        bool snap_to_orthogonal = false;
	quader::QVec3 orbit_anchor;
	bool has_orbit_anchor = false;
};

/**
 * Represents a Viewport Pan Input value used by the modal modeling interaction layer.
 */
struct ViewportPanInput {
	ViewportCameraState camera;
	quader::QVec3 pivot;
	quader::QVec2 screen_delta;
};

/**
 * Defines the reversible soft envelope applied to viewport wheel zoom.
 *
 * A controller may widen the ordinary limits when framing, restoring, or a
 * projection transition establishes a valid exceptional view. Keeping that
 * envelope across wheel events lets the opposite event return to the exact
 * authored view without permitting zoom farther beyond it.
 */
struct ViewportZoomLimits {
	double minimum_perspective_distance = kNavigationMinCameraDistance;
	double maximum_perspective_distance = kNavigationMaxCameraDistance;
	double minimum_orthographic_size = kNavigationMinOrthographicSize;
	double maximum_orthographic_size =
		kNavigationMaxOrthographicZoomOutSize;
};

/**
 * Represents a Viewport Zoom Input value used by the modal modeling interaction layer.
 */
struct ViewportZoomInput {
	ViewportCameraState camera;
	quader::QVec3 pivot;
	ViewportZoomLimits limits;
	double steps = 0.0;
};

/**
 * Represents a Viewport Zoom To Cursor Input value used by the modal modeling interaction layer.
 */
struct ViewportZoomToCursorInput {
	ViewportCameraState camera;
	quader::QVec3 pivot;
	quader::QVec3 ray_origin;
	quader::QVec3 ray_direction;
	quader::QVec3 cursor_target;
	bool has_cursor_target = false;
	ViewportZoomLimits limits;
	double steps = 0.0;
};

/**
 * Represents a Viewport Fly Look Input value used by the modal modeling interaction layer.
 */
struct ViewportFlyLookInput {
	quader_poly::Transform3 camera_transform;
	quader::QVec3 pivot;
	quader::QVec2 screen_delta;
        double sensitivity = kNavigationFlyLookRadiansPerPixel;
};

/**
 * Represents a Viewport Fly Move Input value used by the modal modeling interaction layer.
 */
struct ViewportFlyMoveInput {
	quader_poly::Transform3 camera_transform;
	quader::QVec3 pivot;
	quader::QVec3 local_direction;
	double distance = 0.0;
};

/**
 * Represents a Viewport Frame Bounds Input value used by the modal modeling interaction layer.
 */
struct ViewportFrameBoundsInput {
	ViewportCameraState camera;
	quader::QVec3 min_bounds;
	quader::QVec3 max_bounds;
	double margin = 1.0;
};

/**
 * Represents a View Cube Face Primitive value used by the modal modeling interaction layer.
 */
struct ViewCubeFacePrimitive {
  ViewAxis axis = ViewAxis::X;
  int sign = 1;
  double depth = 0.0;
  quader::QVec2 screen_position;
  std::vector<quader::QVec2> polygon;
};

/**
 * Represents a View Cube QAxis Marker Primitive value used by the modal modeling interaction layer.
 */
struct ViewCubeAxisMarkerPrimitive {
  ViewAxis axis = ViewAxis::X;
  int sign = 1;
  double depth = 0.0;
  quader::QVec2 screen_position;
};

/**
 * Represents a View Cube Edge Primitive value used by the modal modeling interaction layer.
 */
struct ViewCubeEdgePrimitive {
  ViewAxis axis = ViewAxis::X;
  double depth = 0.0;
  quader::QVec3 local_start;
  quader::QVec3 local_end;
  quader::QVec2 screen_start;
  quader::QVec2 screen_end;
};

/**
 * Represents a View Cube Primitive Input value used by the modal modeling interaction layer.
 */
struct ViewCubePrimitiveInput {
	quader_poly::Transform3 camera_transform;
	quader::QVec2 viewport_size;
	double widget_size = 76.0;
	double axis_marker_radius = 8.0;
	quader::QVec2 margin { 46.0F, 82.0F };
};

/**
 * Represents a View Cube Primitives value used by the modal modeling interaction layer.
 */
struct ViewCubePrimitives {
	bool ok = true;
	quader::QVec2 screen_position;
	quader::QVec2 rect_position;
	quader::QVec2 rect_size;
	double axis_marker_radius = 8.0;
	std::vector<ViewCubeFacePrimitive> faces;
	std::vector<ViewCubeEdgePrimitive> edges;
	std::vector<ViewCubeEdgePrimitive> axis_edges;
	std::vector<ViewCubeAxisMarkerPrimitive> axis_markers;
};

/**
 * Represents a View Cube Pick Input value used by the modal modeling interaction layer.
 */
struct ViewCubePickInput {
	ViewCubePrimitiveInput primitives;
	quader::QVec2 screen_position;
};

/**
 * Represents a View Cube Hit value used by the modal modeling interaction layer.
 */
struct ViewCubeHit {
	bool ok = true;
	bool hit = false;
        ViewCubeHitKind kind = ViewCubeHitKind::None;
        ViewAxis axis = ViewAxis::X;
        int sign = 1;
	double depth = 0.0;
	quader::QVec2 screen_position;
};

quader::QVec3 viewport_camera_forward(const quader_poly::Transform3& camera_transform);
quader::QVec3 viewport_camera_right(const quader_poly::Transform3& camera_transform);
quader::QVec3 viewport_camera_up(const quader_poly::Transform3& camera_transform);
double viewport_distance_to_pivot(const quader_poly::Transform3& camera_transform, quader::QVec3 pivot);
double viewport_aspect_for_size(quader::QVec2 viewport_size);
double viewport_vertical_fov_radians(double fov_degrees);
double viewport_orthographic_size_for_perspective(const quader_poly::Transform3& camera_transform, quader::QVec3 pivot, double fov_degrees);
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
ViewportNavigationResult viewport_view_axis(const ViewportCameraState& camera, quader::QVec3 pivot, ViewAxis axis, int sign);
ViewportNavigationResult viewport_step_view_axis(const ViewportCameraState& camera, quader::QVec3 pivot, ViewAxisStepDirection direction);
ViewCubePrimitives build_view_cube_primitives(const ViewCubePrimitiveInput& input);
ViewCubeHit pick_view_cube(const ViewCubePickInput& input);
bool view_cube_hits_match(const ViewCubeHit& left, const ViewCubeHit& right);

} // namespace quader_editor_core
