////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "tool_math_internal.h"

#include <modeling/runtime/editor_tool.h>
#include <modeling/interactions/poly_tool.h>
#include <modeling/interactions/tool_snapping.h>

#include <geometry/geometry.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace quader_editor_core {
namespace {

inline constexpr double poly_point_epsilon_squared = 0.0001;
inline constexpr double poly_plane_epsilon_squared = 0.000001;

quader_geometry::Vec3d to_geometry_vec3(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

quader_geometry::Triangle3<double> to_geometry_triangle(quader_poly::Vec3 a, quader_poly::Vec3 b, quader_poly::Vec3 c)
{
	return { to_geometry_vec3(a), to_geometry_vec3(b), to_geometry_vec3(c) };
}

bool poly_points_are_collinear(quader_poly::Vec3 a, quader_poly::Vec3 b, quader_poly::Vec3 c)
{
	const quader_geometry::Vec3d area_vector =
		quader_geometry::triangle_area_vector(to_geometry_triangle(a, b, c));
	return quader_geometry::length_squared(area_vector) <= poly_plane_epsilon_squared * 0.25;
}

bool poly_points_have_area(const std::vector<quader_poly::Vec3>& points)
{
	if (points.size() < 3U) {
		return false;
	}
	for (std::size_t i = 0; i + 2U < points.size(); ++i) {
		for (std::size_t j = i + 1U; j + 1U < points.size(); ++j) {
			for (std::size_t k = j + 1U; k < points.size(); ++k) {
				if (!poly_points_are_collinear(points[i], points[j], points[k])) {
					return true;
				}
			}
		}
	}
	return false;
}

/**
 * Implements Poly Tool behavior for the modal modeling interaction layer.
 */
class PolyTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::poly;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Poly";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool keep_active_when_current_tool) const override
	{
		reset_editor_runtime_poly_state(runtime, keep_active_when_current_tool);
	}

	void reset(PolyToolState& state) const
	{
		state = PolyToolState();
	}

	void rebuild_overlay(PolyToolState& state) const
	{
		state.preview_lines.clear();
		state.preview_points.clear();
		if (!state.active) {
			return;
		}

		for (const quader_poly::Vec3& point : state.points) {
			state.preview_points.push_back({
				SelectionOverlayLayer::selected_vertex,
				point,
				0xffffbd45U,
				8.0F,
			});
		}

		for (std::size_t index = 1; index < state.points.size(); ++index) {
			state.preview_lines.push_back({
				SelectionOverlayLayer::hover_edge,
				state.points[index - 1U],
				state.points[index],
				0xffffbd45U,
				3.0F,
			});
		}

		if (state.closed && state.points.size() >= 3U) {
			state.preview_lines.push_back({
				SelectionOverlayLayer::hover_edge,
				state.points.back(),
				state.points.front(),
				0xffffbd45U,
				3.0F,
			});
			return;
		}

		if (state.has_current) {
			state.preview_points.push_back({
				SelectionOverlayLayer::hover_vertex,
				state.current_point,
				current_closes_stroke(state) ? 0xff56ff8fU : 0xffffd77aU,
				8.0F,
			});
			if (!state.points.empty()) {
				state.preview_lines.push_back({
					SelectionOverlayLayer::hover_edge,
					state.points.back(),
					state.current_point,
					current_closes_stroke(state) ? 0xff56ff8fU : 0xffffbd45U,
					3.0F,
				});
			}
		}
	}

	[[nodiscard]] bool can_commit(const PolyToolState& state) const
	{
		return state.points.size() >= 3U && poly_points_have_area(state.points);
	}

	[[nodiscard]] PolyToolPreview preview(const PolyToolState& state) const
	{
		const auto saturated_size = [](std::size_t size) {
			return size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ?
				std::numeric_limits<std::uint32_t>::max() :
				static_cast<std::uint32_t>(size);
		};
		PolyToolPreview result;
		result.active = state.active;
		result.closed = state.closed;
		result.has_current = state.has_current;
		result.finalizable = can_commit(state);
		result.point_count = saturated_size(state.points.size());
		result.line_count = saturated_size(state.preview_lines.size());
		if (!state.points.empty()) {
			result.first_point = state.points.front();
			result.last_point = state.points.back();
		}
		result.current_point = state.has_current ? state.current_point : quader_poly::Vec3 {};
		return result;
	}

	[[nodiscard]] PolyToolResult update_hover(
			PolyToolState& state,
			quader_poly::Vec3 position,
			quader_poly::Vec3 plane_normal,
			bool constrain_axis,
			double grid_size,
			bool snap_enabled) const
	{
		if (state.closed) {
			state.active = true;
			rebuild_overlay(state);
			return { true, false };
		}
		const quader_poly::Vec3 safe_normal = vec3_is_tiny(plane_normal) ? quader_poly::Vec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(plane_normal);
		quader_poly::Vec3 next_position = snap_knife_point_to_grid(position, grid_size, snap_enabled);
		if (constrain_axis && !state.points.empty()) {
			next_position = constrain_knife_point_to_axis(state.points.back(), next_position, safe_normal, grid_size, snap_enabled);
		}

		const bool changed =
			!state.active ||
			!state.has_current ||
			state.closed ||
			vec3_distance_squared(state.current_point, next_position) > poly_point_epsilon_squared ||
			vec3_distance_squared(state.face_normal, safe_normal) > poly_plane_epsilon_squared;
		state.active = true;
		state.closed = false;
		state.has_current = true;
		state.current_point = next_position;
		state.face_normal = safe_normal;
		rebuild_overlay(state);
		return { true, changed };
	}

	[[nodiscard]] PolyToolResult accept_current_point(PolyToolState& state) const
	{
		if (!state.active || !state.has_current) {
			return { false, false, "Poly needs a point under the cursor." };
		}
		if (state.closed) {
			return { true, false };
		}
		if (current_closes_stroke(state)) {
			state.closed = true;
			state.has_current = false;
			rebuild_overlay(state);
			return { true, true };
		}
		if (point_repeats_existing(state, state.current_point)) {
			return { false, false, "Poly vertices must stay distinct." };
		}
		state.points.push_back(state.current_point);
		state.closed = false;
		state.has_current = false;
		rebuild_overlay(state);
		return { true, true };
	}

private:
	[[nodiscard]] bool point_repeats_existing(const PolyToolState& state, quader_poly::Vec3 point) const
	{
		for (const quader_poly::Vec3 existing : state.points) {
			if (vec3_distance_squared(existing, point) <= poly_point_epsilon_squared) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool current_closes_stroke(const PolyToolState& state) const
	{
		return state.points.size() >= 3U &&
			state.has_current &&
			vec3_distance_squared(state.points.front(), state.current_point) <= poly_point_epsilon_squared;
	}
};

const PolyTool& poly_tool_instance()
{
	static const PolyTool tool;
	return tool;
}

} // namespace

const EditorTool& poly_tool()
{
	return poly_tool_instance();
}

void reset_poly(PolyToolState& state)
{
	poly_tool_instance().reset(state);
}

void rebuild_poly_overlay(PolyToolState& state)
{
	poly_tool_instance().rebuild_overlay(state);
}

bool poly_can_commit(const PolyToolState& state)
{
	return poly_tool_instance().can_commit(state);
}

PolyToolPreview poly_tool_preview(const PolyToolState& state)
{
	return poly_tool_instance().preview(state);
}

PolyToolResult update_poly_hover(
		PolyToolState& state,
		quader_poly::Vec3 position,
		quader_poly::Vec3 plane_normal,
		bool constrain_axis,
		double grid_size,
		bool snap_enabled)
{
	return poly_tool_instance().update_hover(state, position, plane_normal, constrain_axis, grid_size, snap_enabled);
}

PolyToolResult accept_poly_current_point(PolyToolState& state)
{
	return poly_tool_instance().accept_current_point(state);
}

} // namespace quader_editor_core
