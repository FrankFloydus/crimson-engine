////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/interactions/viewport_projection.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader_editor_core {

/**
 * Stores the Drawn Plane Tool Result data contract used by the modal modeling interaction layer.
 */
struct DrawnPlaneToolResult {
	bool ok = false;
	bool changed = false;
	std::string message;
};

/**
 * Stores the Drawn Plane Tool State data contract used by the modal modeling interaction layer.
 */
struct DrawnPlaneToolState {
	std::vector<quader::QVec3> points;
	quader::QVec3 face_normal;
	int dragging_point_index = -1;
	bool dragging_new_point = false;
	bool dragging_line = false;
	quader::QVec3 drag_line_anchor;
};

/**
 * Stores the Drawn Plane Hover State data contract used by the modal modeling interaction layer.
 */
struct DrawnPlaneHoverState {
	bool valid = false;
	int object_id = -1;
	quader::QVec3 position;
};

/**
 * Represents a Drawn Plane Surface value used by the modal modeling interaction layer.
 */
struct DrawnPlaneSurface {
	bool valid = false;
	quader::QVec3 origin;
	quader::QVec3 normal { 0.0F, 1.0F, 0.0F };
	std::string message;
};

/**
 * Represents a Drawn Plane Ray Update Input value used by the modal modeling interaction layer.
 */
struct DrawnPlaneRayUpdateInput {
	ViewportCameraState camera;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool dragging = false;
};

/**
 * Represents a Drawn Plane Tool State Ref value used by the modal modeling interaction layer.
 */
struct DrawnPlaneToolStateRef {
	std::vector<quader::QVec3>& points;
	quader::QVec3& face_normal;
	int& dragging_point_index;
	bool& dragging_new_point;
	bool& dragging_line;
	quader::QVec3& drag_line_anchor;
};

/**
 * Represents a Drawn Plane Tool State Const Ref value used by the modal modeling interaction layer.
 */
struct DrawnPlaneToolStateConstRef {
	const std::vector<quader::QVec3>& points;
	quader::QVec3 face_normal;
	int dragging_point_index = -1;
	bool dragging_new_point = false;
	bool dragging_line = false;
	quader::QVec3 drag_line_anchor;
};

DrawnPlaneToolStateRef drawn_plane_state_ref(DrawnPlaneToolState& state);
DrawnPlaneToolStateConstRef drawn_plane_state_ref(const DrawnPlaneToolState& state);

/**
 * Owns Drawn Plane Tool Logic behavior within the modal modeling interaction layer.
 */
class DrawnPlaneToolLogic {
public:
	[[nodiscard]] DrawnPlaneToolResult capture_first(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		quader::QVec3 plane_normal,
		std::string_view label) const;
	[[nodiscard]] DrawnPlaneToolResult capture_second(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		std::string_view label) const;
	[[nodiscard]] DrawnPlaneToolResult update_dragged_new_point(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		std::string_view label) const;
	[[nodiscard]] DrawnPlaneToolResult update_dragged_point(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		quader::QVec3 plane_normal,
		std::string_view label) const;
	[[nodiscard]] DrawnPlaneToolResult begin_point_drag(
		DrawnPlaneToolStateRef state,
		int point_index,
		std::string_view label) const;
	[[nodiscard]] DrawnPlaneToolResult begin_line_drag(
		DrawnPlaneToolStateRef state,
		quader::QVec3 anchor,
		std::string_view label) const;
	[[nodiscard]] DrawnPlaneToolResult update_dragged_line(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		std::string_view label) const;
	[[nodiscard]] DrawnPlaneToolResult end_point_drag(DrawnPlaneToolStateRef state) const;
	[[nodiscard]] DrawnPlaneToolResult end_new_point_drag(DrawnPlaneToolStateRef state) const;
	[[nodiscard]] DrawnPlaneToolResult end_line_drag(DrawnPlaneToolStateRef state) const;

	[[nodiscard]] std::vector<quader::QVec3> overlay_points(
		DrawnPlaneToolStateConstRef state,
		quader::QVec3 hover_position,
		bool has_hover) const;
	[[nodiscard]] std::vector<quader::QVec3> plane_points(DrawnPlaneToolStateConstRef state) const;
	[[nodiscard]] bool point_repeats_existing(DrawnPlaneToolStateConstRef state, quader::QVec3 point) const;
	[[nodiscard]] bool point_repeats_other(DrawnPlaneToolStateConstRef state, quader::QVec3 point, int point_index) const;
	[[nodiscard]] bool is_dragging(DrawnPlaneToolStateConstRef state) const;
	[[nodiscard]] bool has_points(DrawnPlaneToolStateConstRef state) const;
	[[nodiscard]] bool has_line(DrawnPlaneToolStateConstRef state) const;
	[[nodiscard]] bool update_hover(
		DrawnPlaneHoverState& state,
		bool valid,
		int object_id,
		quader::QVec3 position,
		double epsilon) const;
};

[[nodiscard]] const DrawnPlaneToolLogic& drawn_plane_tool_logic();
[[nodiscard]] DrawnPlaneSurface drawn_plane_surface_from_points(std::span<const quader::QVec3> points);
[[nodiscard]] quader::QVec3 reflect_point_across_plane(
	quader::QVec3 point,
	quader::QVec3 plane_origin,
	quader::QVec3 plane_normal);

} // namespace quader_editor_core
