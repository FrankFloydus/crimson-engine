////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/poly_tool.hpp>
#include <editor/interactions/tool_snapping.hpp>
#include <editor/modeling/runtime/editor_tool.hpp>

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include "tool_math_internal.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace quader_editor_core {
namespace {

inline constexpr double kPolyPointEpsilonSquared = 0.0001;
inline constexpr double kPolyPlaneEpsilonSquared = 0.000001;

quader_geometry::QVec3d to_geometry_vec3(quader::QVec3 value)
{
	return { value.x, value.y, value.z };
}

quader_geometry::QTriangle3<double> to_geometry_triangle(quader::QVec3 a, quader::QVec3 b, quader::QVec3 c)
{
	return { to_geometry_vec3(a), to_geometry_vec3(b), to_geometry_vec3(c) };
}

bool poly_points_are_collinear(quader::QVec3 a, quader::QVec3 b, quader::QVec3 c)
{
	const quader_geometry::QVec3d area_vector =
		quader_geometry::triangle_area_vector(to_geometry_triangle(a, b, c));
        return quader_geometry::length_squared(area_vector) <=
               kPolyPlaneEpsilonSquared * 0.25;
}

bool poly_points_have_area(const std::vector<quader::QVec3>& points)
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
          return ViewportInteractionTool::Poly;
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

		for (const quader::QVec3& point : state.points) {
                  state.preview_points.push_back({
                      SelectionOverlayLayer::SelectedVertex,
                      point,
                      0xffffbd45U,
                      8.0F,
                  });
                }

		for (std::size_t index = 1; index < state.points.size(); ++index) {
                  state.preview_lines.push_back({
                      SelectionOverlayLayer::HoverEdge,
                      state.points[index - 1U],
                      state.points[index],
                      0xffffbd45U,
                      3.0F,
                  });
                }

		if (state.closed && state.points.size() >= 3U) {
                  state.preview_lines.push_back({
                      SelectionOverlayLayer::HoverEdge,
                      state.points.back(),
                      state.points.front(),
                      0xffffbd45U,
                      3.0F,
                  });
                  return;
		}

		if (state.has_current) {
                  state.preview_points.push_back({
                      SelectionOverlayLayer::HoverVertex,
                      state.current_point,
                      current_closes_stroke(state) ? 0xff56ff8fU : 0xffffd77aU,
                      8.0F,
                  });
                  if (!state.points.empty()) {
                    state.preview_lines.push_back({
                        SelectionOverlayLayer::HoverEdge,
                        state.points.back(),
                        state.current_point,
                        current_closes_stroke(state) ? 0xff56ff8fU
                                                     : 0xffffbd45U,
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
		result.current_closes_stroke = current_closes_stroke(state);
		result.finalizable = can_commit(state);
		result.point_count = saturated_size(state.points.size());
		result.line_count = saturated_size(state.preview_lines.size());
		if (!state.points.empty()) {
			result.first_point = state.points.front();
			result.last_point = state.points.back();
		}
		result.current_point = state.has_current ? state.current_point : quader::QVec3 {};
		return result;
	}

	[[nodiscard]] PolyToolResult update_hover(
			PolyToolState& state,
			quader::QVec3 position,
			quader::QVec3 plane_normal,
			bool constrain_axis,
			double grid_size,
			bool snap_enabled) const
	{
		if (state.closed) {
			state.active = true;
			rebuild_overlay(state);
			return { true, false };
		}
		const quader::QVec3 requested_normal =
			state.points.empty() ? plane_normal : state.face_normal;
		const quader::QVec3 safe_normal = vec3_is_tiny(requested_normal) ?
			quader::QVec3 { 0.0F, 1.0F, 0.0F } :
			normalized_axis(requested_normal);
		const quader::QVec3 plane_point =
			state.points.empty() ? position : state.points.front();
		const std::optional<quader::QVec3> snapped_position =
			snap_point_to_plane_grid(
				position, plane_point, safe_normal, grid_size, snap_enabled);
		if (!snapped_position.has_value()) {
			return { false, false, "Poly needs a valid construction plane." };
		}
		quader::QVec3 next_position = *snapped_position;
		if (constrain_axis && !state.points.empty()) {
			next_position = constrain_knife_point_to_axis(state.points.back(), next_position, safe_normal, grid_size, snap_enabled);
		}

                const bool changed =
                    !state.active || !state.has_current || state.closed ||
                    vec3_distance_squared(state.current_point, next_position) >
                        kPolyPointEpsilonSquared ||
                    vec3_distance_squared(state.face_normal, safe_normal) >
                        kPolyPlaneEpsilonSquared;
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
	[[nodiscard]] bool point_repeats_existing(const PolyToolState& state, quader::QVec3 point) const
	{
		for (const quader::QVec3 existing : state.points) {
                  if (vec3_distance_squared(existing, point) <=
                      kPolyPointEpsilonSquared) {
                    return true;
                  }
                }
		return false;
	}

	[[nodiscard]] bool current_closes_stroke(const PolyToolState& state) const
	{
          return state.points.size() >= 3U && state.has_current &&
                 vec3_distance_squared(state.points.front(),
                                       state.current_point) <=
                     kPolyPointEpsilonSquared;
        }
};

const PolyTool& poly_tool_instance()
{
  static const PolyTool kTool;
  return kTool;
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
		quader::QVec3 position,
		quader::QVec3 plane_normal,
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
