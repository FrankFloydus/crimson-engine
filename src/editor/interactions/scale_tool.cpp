////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/scale_tool.hpp>
#include <editor/modeling/runtime/editor_tool.hpp>

#include "tool_math_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace quader_editor_core {
namespace {

double snap_component_scale(const ScaleToolState& state, double factor, double grid_size)
{
	if (!state.bounds.has_bounds || grid_size <= kScreenEpsilon) {
		return factor;
	}
	const auto pivot = inverse_transform_vector(state.orientation, state.pivot);
	const std::array<double, 3> pivot_coordinates{pivot.x, pivot.y, pivot.z};
	const std::array<double, 3> minimum{state.bounds.min.x, state.bounds.min.y, state.bounds.min.z};
	const std::array<double, 3> maximum{state.bounds.max.x, state.bounds.max.y, state.bounds.max.z};
	const auto active_axes = axis_vector(state.axis);
	const std::array<float, 3> active_components{active_axes.x, active_axes.y, active_axes.z};
	double nearest_factor = 1.0;
	double nearest_distance = std::abs(factor - nearest_factor);
	// Identity wins equal-distance ties. Remaining ties follow axis, minimum
	// endpoint, then the grid line nearer zero, keeping reversal deterministic.
	for (std::size_t component = 0; component < active_components.size(); ++component) {
		if (active_components[component] == 0.0F) {
			continue;
		}
		const double coordinate = pivot_coordinates[component];
		for (const double endpoint : {minimum[component], maximum[component]}) {
			const double extent = endpoint - coordinate;
			// A nearly coincident pivot endpoint cannot define a stable factor.
			constexpr double kMinimumSnapExtent = 0.001;
			if (std::abs(extent) < kMinimumSnapExtent) {
				continue;
			}
			const double target = coordinate + extent * factor;
			const double lower_line = std::floor(target / grid_size) * grid_size;
			const double upper_line = lower_line + grid_size;
			const bool lower_nearer_zero = std::abs(lower_line) <= std::abs(upper_line);
			const std::array<double, 2> lines = lower_nearer_zero
				? std::array<double, 2>{lower_line, upper_line}
				: std::array<double, 2>{upper_line, lower_line};
			for (const double line : lines) {
				const double candidate = std::max(0.0, (line - coordinate) / extent);
				const double distance = std::abs(candidate - factor);
				if (distance < nearest_distance) {
					nearest_factor = candidate;
					nearest_distance = distance;
				}
			}
		}
	}
	return nearest_factor;
}

/**
 * Implements Scale Tool behavior for the modal modeling interaction layer.
 */
class ScaleTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
          return ViewportInteractionTool::Scale;
        }

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Scale";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		clear_editor_runtime_transform_drag_state(runtime);
		clear_editor_runtime_gizmo_hover(runtime);
	}

	[[nodiscard]] ScaleBeginResult begin_scale_drag(const ScaleBeginInput& input) const
	{
		if (vec3_is_tiny(axis_vector(input.axis))) {
			return { false, "Invalid scale axis.", {} };
		}
		ScaleToolState state;
		state.active = true;
		state.axis = input.axis;
		state.orientation = input.orientation;
		state.object_ids = input.object_ids;
		state.pivot = input.pivot;
		state.screen_pivot = input.has_screen_pivot ? input.screen_pivot : input.screen_position;
		state.start_mouse = input.screen_position;
		const quader::QVec2 screen_tip = input.has_screen_tip ?
			input.screen_tip :
			quader::QVec2 { input.screen_position.x + 1.0F, input.screen_position.y };
		state.screen_axis = vec2_normalize_or(vec2_sub(screen_tip, state.screen_pivot), { 1.0F, 0.0F });
		state.bounds = input.bounds;
		state.snap_bounds_to_grid = input.snap_bounds_to_grid;
		state.snap_uniform_to_grid = input.snap_uniform_to_grid;
		state.preview_scale = { 1.0F, 1.0F, 1.0F };
		return { true, {}, state };
	}

	[[nodiscard]] ScalePreviewResult preview_scale_from_screen_position(
			ScaleToolState& state,
			quader::QVec2 screen_position,
			bool grid_enabled,
			double grid_size) const
	{
		if (!state.active) {
			return { false, "Scale tool is not active.", {}, {}, {}, {} };
		}
		const double axis_pixels = vec2_dot(vec2_sub(screen_position, state.start_mouse), state.screen_axis);
                double factor = std::max(
                    kMinScaleFactor, 1.0 + axis_pixels / kScalePixelsPerFactor);
		if (grid_enabled && (state.axis != TransformAxis::All || state.snap_uniform_to_grid)) {
			factor = state.snap_bounds_to_grid && state.axis != TransformAxis::All
				? snap_component_scale(state, factor, grid_size)
				: snapped_scale_factor(state.axis, factor, state.bounds, state.pivot, true, grid_size);
		}
		return preview_scale_from_scale(state, axis_scale(state.axis, factor));
	}

	[[nodiscard]] ScalePreviewResult preview_scale_from_scale(ScaleToolState& state, quader::QVec3 scale) const
	{
		if (!state.active) {
			return { false, "Scale tool is not active.", {}, {}, {}, {} };
		}
		state.preview_scale = scale;
		const quader_poly::Transform3 transform = oriented_scale_transform(
			state.preview_scale, state.orientation);
		return { true, {}, state.preview_scale, state.preview_scale, transform, pivoted_transform(transform, state.pivot) };
	}

	void reset_scale(ScaleToolState& state) const
	{
		state = ScaleToolState();
	}
};

const ScaleTool& scale_tool_impl()
{
  static const ScaleTool kTool;
  return kTool;
}

} // namespace

const EditorTool& scale_tool()
{
	return scale_tool_impl();
}

ScaleBeginResult begin_scale_drag(const ScaleBeginInput& input)
{
	return scale_tool_impl().begin_scale_drag(input);
}

ScalePreviewResult preview_scale_from_screen_position(
		ScaleToolState& state,
		quader::QVec2 screen_position,
		bool grid_enabled,
		double grid_size)
{
	return scale_tool_impl().preview_scale_from_screen_position(state, screen_position, grid_enabled, grid_size);
}

ScalePreviewResult preview_scale_from_scale(ScaleToolState& state, quader::QVec3 scale)
{
	return scale_tool_impl().preview_scale_from_scale(state, scale);
}

ScalePreviewResult preview_bounds_resize_from_displacement(
    ScaleToolState &state, double world_displacement, bool grid_enabled,
    double grid_size) {
  if (!state.active || !state.bounds_resize ||
      !std::isfinite(world_displacement) ||
      !std::isfinite(state.bounds_resize_world_extent) ||
      state.bounds_resize_world_extent <= 0.0) {
    return {false, "Bounds resize displacement is invalid."};
  }
  double displacement = world_displacement;
  if (displacement != 0.0 && grid_enabled && std::isfinite(grid_size) && grid_size > 0.0) {
    const double coordinate = state.bounds_resize_coordinate +
        state.bounds_resize_side * displacement / state.bounds_resize_axis_scale;
    const double snapped = std::round(coordinate / grid_size) * grid_size;
    displacement = (snapped - state.bounds_resize_coordinate) *
        state.bounds_resize_side * state.bounds_resize_axis_scale;
  }
  constexpr double kMinimumBoundsScale = 0.001;
  constexpr double kMaximumBoundsScale = 10000.0;
  const double factor = std::clamp(
      1.0 + displacement / state.bounds_resize_world_extent,
      kMinimumBoundsScale, kMaximumBoundsScale);
  if (!std::isfinite(factor)) {
    return {false, "Bounds resize scale is invalid."};
  }
  return preview_scale_from_scale(state, axis_scale(state.axis, factor));
}

void reset_scale(ScaleToolState& state)
{
	scale_tool_impl().reset_scale(state);
}

} // namespace quader_editor_core
