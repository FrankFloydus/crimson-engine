////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/drawn_plane_tool.hpp>

#include "tool_math_internal.hpp"

#include <algorithm>
#include <span>
#include <string>

namespace quader_editor_core {
namespace {

inline constexpr double drawn_plane_point_epsilon_squared = 0.0001;
inline constexpr double drawn_plane_epsilon_squared = 0.000001;

std::string message(std::string_view label, std::string_view suffix)
{
	return std::string(label) + std::string(suffix);
}

quader::QVec3 safe_face_normal(quader::QVec3 normal)
{
	return vec3_is_tiny(normal) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(normal);
}

} // namespace

DrawnPlaneToolStateRef drawn_plane_state_ref(DrawnPlaneToolState& state)
{
	return {
		state.points,
		state.face_normal,
		state.dragging_point_index,
		state.dragging_new_point,
		state.dragging_line,
		state.drag_line_anchor,
	};
}

DrawnPlaneToolStateConstRef drawn_plane_state_ref(const DrawnPlaneToolState& state)
{
	return {
		state.points,
		state.face_normal,
		state.dragging_point_index,
		state.dragging_new_point,
		state.dragging_line,
		state.drag_line_anchor,
	};
}

DrawnPlaneToolResult DrawnPlaneToolLogic::capture_first(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		quader::QVec3 plane_normal,
		std::string_view) const
{
	state.points = { position };
	state.face_normal = safe_face_normal(plane_normal);
	state.dragging_point_index = -1;
	state.dragging_new_point = true;
	state.dragging_line = false;
	return { true, true };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::capture_second(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		std::string_view label) const
{
	if (state.points.size() != 1) {
		return { false, false, message(label, " needs a first point.") };
	}
	if (vec3_distance_squared(state.points[0], position) <= drawn_plane_point_epsilon_squared) {
		return { false, false, message(label, " line is too short.") };
	}
	state.points.push_back(position);
	state.dragging_point_index = 1;
	state.dragging_new_point = false;
	state.dragging_line = false;
	return { true, true };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::update_dragged_new_point(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		std::string_view label) const
{
	if (state.points.size() != 1 && state.points.size() != 2) {
		return { false, false, message(label, " needs a first point.") };
	}
	if (vec3_distance_squared(state.points[0], position) <= drawn_plane_point_epsilon_squared) {
		return { false, false, message(label, " line is too short.") };
	}
	if (state.points.size() == 1) {
		state.points.push_back(position);
	} else {
		state.points[1] = position;
	}
	return { true, true };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::update_dragged_point(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		quader::QVec3 plane_normal,
		std::string_view label) const
{
	if (state.dragging_point_index < 0 || state.dragging_point_index >= static_cast<int>(state.points.size())) {
		return { false, false, message(label, " point is not being dragged.") };
	}
	if (point_repeats_other({ state.points, state.face_normal, state.dragging_point_index, state.dragging_new_point, state.dragging_line, state.drag_line_anchor }, position, state.dragging_point_index)) {
		return { false, false, message(label, " points must stay distinct.") };
	}
	std::vector<quader::QVec3> next_points = state.points;
	next_points[static_cast<std::size_t>(state.dragging_point_index)] = position;
	if (next_points.size() >= 2 && vec3_distance_squared(next_points[0], next_points[1]) <= drawn_plane_point_epsilon_squared) {
		return { false, false, message(label, " line is too short.") };
	}
	state.points = std::move(next_points);
	if (state.dragging_point_index == 0 && quader::length_squared(plane_normal) > drawn_plane_epsilon_squared) {
		state.face_normal = normalized_axis(plane_normal);
	}
	return { true, true };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::begin_point_drag(
		DrawnPlaneToolStateRef state,
		int point_index,
		std::string_view label) const
{
	if (point_index < 0 || point_index >= static_cast<int>(state.points.size())) {
		return { false, false, message(label, " point is invalid.") };
	}
	state.dragging_point_index = point_index;
	state.dragging_new_point = false;
	state.dragging_line = false;
	return { true, false };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::begin_line_drag(
		DrawnPlaneToolStateRef state,
		quader::QVec3 anchor,
		std::string_view label) const
{
	if (state.points.size() < 2U) {
		return { false, false, message(label, " needs a placed line to drag.") };
	}
	state.dragging_point_index = -1;
	state.dragging_new_point = false;
	state.dragging_line = true;
	state.drag_line_anchor = anchor;
	return { true, false };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::update_dragged_line(
		DrawnPlaneToolStateRef state,
		quader::QVec3 position,
		std::string_view label) const
{
	if (!state.dragging_line) {
		return { false, false, message(label, " line is not being dragged.") };
	}
	if (state.points.size() < 2U) {
		return { false, false, message(label, " needs a placed line to drag.") };
	}

	const quader::QVec3 delta = position - state.drag_line_anchor;
	state.drag_line_anchor = position;
	if (vec3_distance_squared(delta, quader::QVec3 {}) <= drawn_plane_epsilon_squared) {
		return { true, false };
	}

	for (quader::QVec3& point : state.points) {
		point += delta;
	}
	return { true, true };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::end_point_drag(DrawnPlaneToolStateRef state) const
{
	state.dragging_point_index = -1;
	return { true, false };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::end_new_point_drag(DrawnPlaneToolStateRef state) const
{
	state.dragging_new_point = false;
	return { true, false };
}

DrawnPlaneToolResult DrawnPlaneToolLogic::end_line_drag(DrawnPlaneToolStateRef state) const
{
	state.dragging_line = false;
	return { true, false };
}

std::vector<quader::QVec3> DrawnPlaneToolLogic::overlay_points(
		DrawnPlaneToolStateConstRef state,
		quader::QVec3 hover_position,
		bool has_hover) const
{
	if (state.points.empty()) {
		return has_hover ? std::vector<quader::QVec3> { hover_position } : state.points;
	}

	std::vector<quader::QVec3> points;
	points.push_back(state.points[0]);
	if (state.points.size() >= 2U) {
		points.push_back(state.points[1]);
		return points;
	}
	if (has_hover) {
		points.push_back(hover_position);
	}
	return points;
}

std::vector<quader::QVec3> DrawnPlaneToolLogic::plane_points(DrawnPlaneToolStateConstRef state) const
{
	if (state.points.size() < 2U) {
		return {};
	}
	return {
		state.points[0],
		state.points[1],
		state.points[0] + safe_face_normal(state.face_normal),
	};
}

bool DrawnPlaneToolLogic::point_repeats_existing(DrawnPlaneToolStateConstRef state, quader::QVec3 point) const
{
	for (const quader::QVec3 existing : state.points) {
		if (vec3_distance_squared(existing, point) <= drawn_plane_point_epsilon_squared) {
			return true;
		}
	}
	return false;
}

bool DrawnPlaneToolLogic::point_repeats_other(
		DrawnPlaneToolStateConstRef state,
		quader::QVec3 point,
		int point_index) const
{
	for (int i = 0; i < static_cast<int>(state.points.size()); ++i) {
		if (i == point_index) {
			continue;
		}
		if (vec3_distance_squared(state.points[static_cast<std::size_t>(i)], point) <= drawn_plane_point_epsilon_squared) {
			return true;
		}
	}
	return false;
}

bool DrawnPlaneToolLogic::is_dragging(DrawnPlaneToolStateConstRef state) const
{
	return state.dragging_new_point || state.dragging_point_index >= 0 || state.dragging_line;
}

bool DrawnPlaneToolLogic::has_points(DrawnPlaneToolStateConstRef state) const
{
	return !state.points.empty();
}

bool DrawnPlaneToolLogic::has_line(DrawnPlaneToolStateConstRef state) const
{
	return state.points.size() >= 2U;
}

bool DrawnPlaneToolLogic::update_hover(
		DrawnPlaneHoverState& state,
		bool valid,
		int object_id,
		quader::QVec3 position,
		double epsilon) const
{
	const bool changed = state.valid != valid ||
		state.object_id != object_id ||
		(valid && vec3_distance_squared(state.position, position) > epsilon);
	state.valid = valid;
	state.object_id = object_id;
	state.position = position;
	return changed;
}

const DrawnPlaneToolLogic& drawn_plane_tool_logic()
{
	static const DrawnPlaneToolLogic logic;
	return logic;
}

DrawnPlaneSurface drawn_plane_surface_from_points(std::span<const quader::QVec3> points)
{
	if (points.size() < 3U) {
		return { false, {}, {}, "Drawn plane needs three plane points." };
	}
	const quader::QVec3 first_edge = points[1] - points[0];
	const quader::QVec3 second_edge = points[2] - points[0];
	const quader::QVec3 normal = vec3_cross(first_edge, second_edge);
	if (vec3_is_tiny(normal)) {
		return { false, {}, {}, "Drawn plane is degenerate." };
	}
	return { true, points[0], normalized_axis(normal), {} };
}

quader::QVec3 reflect_point_across_plane(
		quader::QVec3 point,
		quader::QVec3 plane_origin,
		quader::QVec3 plane_normal)
{
	const quader::QVec3 normal = safe_face_normal(plane_normal);
	const float distance = static_cast<float>(vec3_dot(point - plane_origin, normal));
	return point - normal * (2.0F * distance);
}

} // namespace quader_editor_core
