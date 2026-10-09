////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/cut_tool.hpp>
#include <editor/interactions/drawn_plane_tool.hpp>
#include <diagnostics/profile.hpp>
#include <editor/modeling/runtime/editor_tool.hpp>

#include "tool_math_internal.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader_editor_core {
namespace {

DrawnPlaneToolStateRef cut_drawn_plane_ref(CutToolState& state)
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

DrawnPlaneToolStateConstRef cut_drawn_plane_ref(const CutToolState& state)
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

CutToolResult cut_result_from_drawn_plane(DrawnPlaneToolResult result, int target_count = 0)
{
	return {
		result.ok,
		result.changed,
		std::move(result.message),
		target_count,
	};
}

void invalidate_cut_plane_preview(CutToolState& state)
{
	state.preview_valid = false;
	state.preview_counts = {};
}

quader::QVec3 cut_point_on_support_plane(const CutToolState& state,
                                       quader::QVec3 point)
{
  if (!state.support_plane_valid) {
    return point;
  }
  return point - vec3_scale(state.face_normal,
      vec3_dot(point - state.support_plane_origin, state.face_normal));
}

struct CutPlacementPoint {
  CutGridPointResult point;
  int object_id = -1;
};

CutPlacementPoint cut_placement_point_from_ray(
    const CutToolState& state, std::span<const PolygonObject> objects,
    const CutRayUpdateInput& input)
{
  if (!state.points.empty()) {
    return {cut_support_plane_point_from_ray(
                input.ray,
                state.support_plane_valid ? state.support_plane_origin
                                          : state.points.front(),
                state.face_normal, input.grid_size, false),
            state.source_object_id};
  }
  std::optional<PickHitRecord> hit = input.resolved_surface_hit;
  if (!input.surface_hit_resolved) {
    quader_poly::PickOptions options;
    options.mode = quader_poly::SelectionMode::Face;
    hit = pick_nearest(std::vector<PolygonObject>(objects.begin(), objects.end()),
                       {input.ray, options});
  }
  if (hit.has_value() && !vec3_is_tiny(hit->local_normal)) {
    return {cut_support_plane_point_from_ray(
                input.ray, hit->local_position, hit->local_normal,
                input.grid_size, false),
            hit->object_id};
  }
  return {viewport_grid_point_from_ray(input.camera, input.ray, input.grid_size, false), -1};
}

quader::QVec3 cut_position_with_modifiers(
    const CutToolState& state, const CutGridPointResult& placement,
    const CutRayUpdateInput& input, quader::QVec3 position,
    std::optional<quader::QVec3> constraint_origin)
{
  if (input.constrain_axis && constraint_origin.has_value()) {
    const auto constrained = constrain_knife_point_to_axis(*constraint_origin, position,
        placement.plane_normal, input.grid_size, false);
    const auto snapped = snap_point_to_plane_grid(constrained,
        state.support_plane_valid ? state.support_plane_origin : placement.position,
        placement.plane_normal, input.grid_size, input.snap_enabled);
    const auto delta = constrained - *constraint_origin;
    if (!input.snap_enabled || !snapped.has_value() || vec3_is_tiny(delta)) {
      return constrained;
    }
    const auto axis = normalized_axis(delta);
    return *constraint_origin + vec3_scale(axis, vec3_dot(*snapped - *constraint_origin, axis));
  }
  const auto snapped = snap_point_to_plane_grid(position,
      state.support_plane_valid ? state.support_plane_origin : placement.position,
      placement.plane_normal, input.grid_size, input.snap_enabled);
  return snapped.value_or(position);
}

/**
 * Implements Cut Tool behavior for the modal modeling interaction layer.
 */
class CutTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
          return ViewportInteractionTool::Cut;
        }

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Cut";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool keep_active_when_current_tool) const override
	{
		reset_editor_runtime_cut_state(runtime, keep_active_when_current_tool);
	}

	[[nodiscard]] std::vector<int> target_object_ids_for_selection(const RectangleSelectionState& selection) const
	{
		std::vector<int> targets;
		const auto append = [&targets](int object_id) {
			if (object_id >= 0 && std::find(targets.begin(), targets.end(), object_id) == targets.end()) {
				targets.push_back(object_id);
			}
		};
		if (selection.mode == SelectionMode::Object) {
			for (const int object_id : selection.selected_object_ids) {
				append(object_id);
			}
		} else if (selection.mode == SelectionMode::Face) {
			for (const auto& face : selection.selected_faces) {
				append(face.object_id);
			}
		} else if (selection.mode == SelectionMode::Edge) {
			for (const auto& edge : selection.selected_edges) {
				append(edge.object_id);
			}
			for (const int object_id : selection.selected_edge_full_object_ids) {
				append(object_id);
			}
			if (!selection.active_edge_vertex_indices.empty()) {
				append(selection.active_object_id);
			}
		}
		return targets;
	}

	[[nodiscard]] CutToolResult set_targets_from_selection(CutToolState& state, const RectangleSelectionState& selection) const
	{
		if (state.scope_captured) {
			return { true, false, {}, static_cast<int>(state.target_object_ids.size()) };
		}
		if (selection.mode == SelectionMode::Vertex) {
			return {false, false, "Cut requires Object, Face, or Edge selection."};
		}
		const std::vector<int> targets = target_object_ids_for_selection(selection);
		if (targets.empty()) {
			return {false, false, "Select meshes, faces, or edges to cut."};
		}
		state.target_object_ids = targets;
		state.scope = selection.mode == SelectionMode::Face ? CutScope::Face :
			selection.mode == SelectionMode::Edge ? CutScope::Edge : CutScope::Object;
		state.target_faces = state.scope == CutScope::Face ? selection.selected_faces : std::vector<RectangleSelectionFace>{};
		state.target_edges = state.scope == CutScope::Edge ? selection.selected_edges : std::vector<RectangleSelectionEdge>{};
		if (state.scope == CutScope::Edge) {
			state.target_edge_full_object_ids = selection.selected_edge_full_object_ids;
			if (state.target_edges.empty() && selection.active_edge_vertex_indices.size() == 2U) {
				state.target_edges.push_back({selection.active_object_id,
					selection.active_edge_vertex_indices[0], selection.active_edge_vertex_indices[1]});
			}
			state.mode = CutMode::KeepBoth;
			state.cap = false;
		}
		state.scope_captured = true;
		state.source_object_id = -1;
		state.preview_valid = false;
		state.preview_counts = {};
		return { true, true, {}, static_cast<int>(state.target_object_ids.size()) };
	}

	[[nodiscard]] CutToolResult capture_first_point(
			CutToolState& state,
			const RectangleSelectionState& selection,
			quader::QVec3 position,
			quader::QVec3 plane_normal) const
	{
		const CutToolResult targets = set_targets_from_selection(state, selection);
		if (!targets.ok) {
			return targets;
		}
		state.source_object_id = -1;
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().capture_first(cut_drawn_plane_ref(state), position, plane_normal, label());
		if (result.ok && result.changed) {
			state.support_plane_origin = position;
			state.support_plane_valid = true;
			state.drag_point_offset = {};
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result, static_cast<int>(state.target_object_ids.size()));
	}

	[[nodiscard]] CutToolResult capture_second_point(CutToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().capture_second(cut_drawn_plane_ref(state), cut_point_on_support_plane(state, position), label());
		if (result.ok && result.changed) {
			state.drag_point_offset = {};
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult update_dragged_new_point(CutToolState& state, quader::QVec3 position) const
	{
		position = cut_point_on_support_plane(state, position);
		if (state.points.size() == 2U && vec3_distance_squared(state.points[1], position) <= 1.0e-12) {
			return {true, false};
		}
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_new_point(cut_drawn_plane_ref(state), position, label());
		if (result.ok && result.changed) {
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult update_dragged_point(CutToolState& state, quader::QVec3 position, quader::QVec3 plane_normal) const
	{
		position = cut_point_on_support_plane(state, position);
		if (state.support_plane_valid && state.dragging_point_index >= 0 &&
			state.dragging_point_index < static_cast<int>(state.points.size()) &&
			vec3_distance_squared(state.points[static_cast<std::size_t>(state.dragging_point_index)], position) <= 1.0e-12) {
			return {true, false};
		}
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_point(cut_drawn_plane_ref(state), position,
				state.support_plane_valid ? state.face_normal : plane_normal, label());
		if (result.ok && result.changed) {
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult begin_point_drag(CutToolState& state, int point_index) const
	{
		const auto result = drawn_plane_tool_logic().begin_point_drag(cut_drawn_plane_ref(state), point_index, label());
		if (result.ok) {
			state.drag_point_offset = {};
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult begin_line_drag(CutToolState& state, quader::QVec3 anchor) const
	{
		const auto result = drawn_plane_tool_logic().begin_line_drag(
			cut_drawn_plane_ref(state), cut_point_on_support_plane(state, anchor), label());
		if (result.ok) {
			// Retain the cursor itself so the line does not jump inside its hit band.
			state.drag_line_anchor = cut_point_on_support_plane(state, anchor);
			state.drag_line_cursor_origin = state.drag_line_anchor;
			state.drag_line_point_origin = state.points.front();
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult update_dragged_line(CutToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_line(cut_drawn_plane_ref(state), cut_point_on_support_plane(state, position), label());
		if (result.ok && result.changed) {
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult end_point_drag(CutToolState& state) const
	{
		return cut_result_from_drawn_plane(drawn_plane_tool_logic().end_point_drag(cut_drawn_plane_ref(state)));
	}

	[[nodiscard]] CutToolResult end_new_point_drag(CutToolState& state) const
	{
		return cut_result_from_drawn_plane(drawn_plane_tool_logic().end_new_point_drag(cut_drawn_plane_ref(state)));
	}

	[[nodiscard]] CutToolResult end_line_drag(CutToolState& state) const
	{
		return cut_result_from_drawn_plane(drawn_plane_tool_logic().end_line_drag(cut_drawn_plane_ref(state)));
	}

	[[nodiscard]] CutToolResult set_mode(CutToolState& state, CutMode mode) const
	{
		if (state.scope == CutScope::Edge && mode != CutMode::KeepBoth) {
			return {false, false, "Edge Cut only inserts crossing vertices."};
		}
		const CutMode previous_mode = state.mode;
		state.mode = mode;
		if (previous_mode != state.mode) {
			state.preview_valid = false;
			state.preview_counts = {};
		}
		return { true, previous_mode != state.mode };
	}

	[[nodiscard]] std::vector<quader::QVec3> overlay_points(
			const CutToolState& state,
			quader::QVec3 hover_position,
			bool has_hover) const
	{
		return drawn_plane_tool_logic().overlay_points(cut_drawn_plane_ref(state), hover_position, has_hover);
	}

	[[nodiscard]] std::vector<quader::QVec3> plane_points(const CutToolState& state) const
	{
		return drawn_plane_tool_logic().plane_points(cut_drawn_plane_ref(state));
	}

	[[nodiscard]] std::vector<int> dirty_object_ids(const std::vector<int>& source_ids, const std::vector<int>& result_ids) const
	{
		return merge_unique_ids(source_ids, result_ids);
	}

	[[nodiscard]] bool point_repeats_existing(const CutToolState& state, quader::QVec3 point) const
	{
		return drawn_plane_tool_logic().point_repeats_existing(cut_drawn_plane_ref(state), point);
	}

	[[nodiscard]] bool point_repeats_other(const CutToolState& state, quader::QVec3 point, int point_index) const
	{
		return drawn_plane_tool_logic().point_repeats_other(cut_drawn_plane_ref(state), point, point_index);
	}

	[[nodiscard]] CutPreviewRequest preview_request(CutToolState& state, CutMode mode) const
	{
		CutPreviewRequest request;
		const CutToolResult mode_result = set_mode(state, mode);
		if (!mode_result.ok) {
			request.message = mode_result.message;
			return request;
		}
		request.mode = state.mode;
		request.point_count = static_cast<int>(state.points.size());
		request.target_count = static_cast<int>(state.target_object_ids.size());
		request.preview_valid = state.preview_valid;
		if (!can_preview(state)) {
			request.ok = false;
			request.message = state.points.size() < 2 ? "Cut needs at least two points." : "Select meshes or elements to cut.";
			request.can_preview = false;
			return request;
		}
		request.ok = true;
		request.can_preview = true;
		request.object_ids = state.target_object_ids;
		request.points = plane_points(state);
		return request;
	}

	[[nodiscard]] CutToolResult set_preview_result(
			CutToolState& state,
			CutMode mode,
			bool ok,
			const CutPreviewCounts& counts,
			std::string message) const
	{
		const CutToolResult mode_result = set_mode(state, mode);
		if (!mode_result.ok) {
			return mode_result;
		}
		if (!ok) {
			state.preview_valid = false;
			state.preview_counts = {};
			return { false, false, message.empty() ? "invalid cut plane" : std::move(message) };
		}
		state.preview_valid = true;
		state.preview_counts = counts;
		return { true, false };
	}

	[[nodiscard]] CutToolResult invalidate_preview(CutToolState& state) const
	{
		invalidate_cut_plane_preview(state);
		return { true, false };
	}

	[[nodiscard]] CutToolResult commit_reset(CutToolState& state, const std::vector<int>& result_object_ids) const
	{
		const int target_count = static_cast<int>(state.target_object_ids.size());
		std::vector<int> dirty_ids = merge_unique_ids(state.target_object_ids, result_object_ids);
		reset(state);
		CutToolResult result;
		result.ok = true;
		result.target_count = target_count;
		result.dirty_object_ids = std::move(dirty_ids);
		return result;
	}

	void reset(CutToolState& state) const
	{
		state = CutToolState();
	}

	[[nodiscard]] bool is_dragging(const CutToolState& state) const
	{
		return drawn_plane_tool_logic().is_dragging(cut_drawn_plane_ref(state));
	}

	[[nodiscard]] bool is_active(const CutToolState& state) const
	{
		return !state.points.empty() || !state.target_object_ids.empty() || state.source_object_id >= 0 || is_dragging(state) || state.preview_valid;
	}

	[[nodiscard]] bool can_preview(const CutToolState& state) const
	{
		return state.points.size() >= 2 && !state.target_object_ids.empty();
	}

	[[nodiscard]] CutPreviewMetadata preview_metadata(const CutToolState& state) const
	{
		CutPreviewMetadata metadata;
		metadata.preview_valid = state.preview_valid;
		metadata.can_preview = can_preview(state);
		metadata.mode = state.mode;
		metadata.point_count = static_cast<int>(state.points.size());
		metadata.target_count = state.preview_counts.target_count > 0 ? state.preview_counts.target_count : static_cast<int>(state.target_object_ids.size());
		metadata.split_count = state.preview_counts.split_count;
		metadata.removed_count = state.preview_counts.removed_count;
		return metadata;
	}

	[[nodiscard]] std::string preview_status(const CutToolState& state, CutMode mode, const CutPreviewCounts& fallback_counts) const
	{
		const CutPreviewMetadata metadata = preview_metadata(state);
		const int split_count = metadata.split_count > 0 ? metadata.split_count : fallback_counts.split_count;
		const int target_count = metadata.target_count > 0 ? metadata.target_count : fallback_counts.target_count;
		const int removed_count = metadata.removed_count > 0 ? metadata.removed_count : fallback_counts.removed_count;
		if (state.scope == CutScope::Edge) {
			return "Cut preview: " + std::to_string(split_count) + "/" +
				std::to_string(target_count) + " affected meshes. Enter/Space confirms.";
		}
		return "Cut preview: " +
			std::string(cut_mode_label(mode)) +
			", " +
			std::to_string(split_count) +
			"/" +
			std::to_string(target_count) +
			" split, " +
			std::to_string(removed_count) +
			" removed preview. Enter/Space confirms; F flips.";
	}

	[[nodiscard]] bool update_hover(CutHoverState& state, bool valid, int object_id, quader::QVec3 position, double epsilon) const
	{
		DrawnPlaneHoverState hover { state.valid, state.object_id, state.position };
		const bool changed = drawn_plane_tool_logic().update_hover(hover, valid, object_id, position, epsilon);
		state.valid = hover.valid;
		state.object_id = hover.object_id;
		state.position = hover.position;
		return changed;
	}

	[[nodiscard]] CutToolResult refresh_preview(CutToolState& state, std::span<const PolygonObject> objects) const
	{
		QDR_PROFILE_SCOPE("qdr_editor_core.CutTool.refresh_preview");
		(void)objects;
		if (!can_preview(state)) {
			return invalidate_preview(state);
		}
		const DrawnPlaneSurface surface =
			drawn_plane_surface_from_points(plane_points(state));
		if (!surface.valid) {
			return set_preview_result(state, state.mode, false, {}, surface.message);
		}
		CutPreviewCounts counts = state.preview_valid ? state.preview_counts : CutPreviewCounts{};
		counts.target_count = static_cast<int>(state.target_object_ids.size());
		return set_preview_result(state, state.mode, true, counts, {});
	}

	[[nodiscard]] CutToolResult refresh_preview_from_selection(
			CutToolState& state,
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects) const
	{
		CutToolResult result = set_targets_from_selection(state, selection);
		if (!result.ok) {
			return result;
		}
		return refresh_preview(state, objects);
	}

	[[nodiscard]] CutToolResult update_hover_from_ray(
			EditorRuntimeState& runtime,
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const CutRayUpdateInput& input) const
	{
		const CutToolResult target_result = set_targets_from_selection(runtime.cut, selection);
		if (!target_result.ok) {
			return target_result;
		}

		const CutPlacementPoint placement = cut_placement_point_from_ray(runtime.cut, objects, input);
		const CutGridPointResult& point = placement.point;
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Cut needs a valid grid point." : point.message };
		}
		runtime.cut.pointer_position = point.position;
		runtime.cut.pointer_position_valid = true;
		quader::QVec3 position = point.position;
		std::optional<quader::QVec3> constraint_origin;
		if (!runtime.cut.points.empty()) {
			constraint_origin = runtime.cut.points.front();
		}
		if (input.dragging && runtime.cut.dragging_point_index >= 0) {
			position += runtime.cut.drag_point_offset;
			if (runtime.cut.points.size() >= 2U && runtime.cut.dragging_point_index == 0) {
				constraint_origin = runtime.cut.points[1];
			}
		} else if (input.dragging && runtime.cut.dragging_line) {
			position = runtime.cut.drag_line_point_origin + point.position - runtime.cut.drag_line_cursor_origin;
			constraint_origin = runtime.cut.drag_line_point_origin;
		}
		position = cut_position_with_modifiers(runtime.cut, point, input, position, constraint_origin);
		const bool hover_changed = update_hover(runtime.cut_hover, true, placement.object_id, position, 0.000001);
		if (!input.dragging) {
			(void)refresh_preview(runtime.cut, objects);
			return { true, hover_changed };
		}

		CutToolResult result;
		if (runtime.cut.dragging_new_point) {
			result = update_dragged_new_point(runtime.cut, position);
		} else if (runtime.cut.dragging_point_index >= 0) {
			result = update_dragged_point(runtime.cut, position, point.plane_normal);
		} else if (runtime.cut.dragging_line) {
			result = update_dragged_line(runtime.cut,
				runtime.cut.drag_line_anchor + position - runtime.cut.points.front());
		} else {
			result = { true, hover_changed };
		}

		if (result.ok) {
			(void)refresh_preview(runtime.cut, objects);
		}
		return result;
	}

	[[nodiscard]] CutToolResult click_from_ray(
			EditorRuntimeState& runtime,
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const CutRayUpdateInput& input) const
	{
		const CutToolResult target_result = set_targets_from_selection(runtime.cut, selection);
		if (!target_result.ok) {
			return target_result;
		}

		const CutPlacementPoint placement = cut_placement_point_from_ray(runtime.cut, objects, input);
		const CutGridPointResult& point = placement.point;
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Cut needs a valid grid point." : point.message };
		}
		runtime.cut.pointer_position = point.position;
		runtime.cut.pointer_position_valid = true;
		const quader::QVec3 position = cut_position_with_modifiers(runtime.cut, point, input, point.position,
			runtime.cut.points.empty() ? std::optional<quader::QVec3>{} : runtime.cut.points.front());
		(void)update_hover(runtime.cut_hover, true, placement.object_id, position, 0.000001);

		CutToolResult result;
		if (runtime.cut.points.empty()) {
			result = capture_first_point(runtime.cut, selection, position, point.plane_normal);
			if (result.ok) {
				runtime.cut.source_object_id = placement.object_id;
			}
		} else if (runtime.cut.points.size() == 1U) {
			result = capture_second_point(runtime.cut, position);
		} else {
			result = { true, false };
		}

		if (result.ok) {
			(void)refresh_preview(runtime.cut, objects);
		}
		return result;
	}

	[[nodiscard]] CutToolResult begin_line_drag_from_ray(EditorRuntimeState& runtime, const CutRayUpdateInput& input) const
	{
		const CutGridPointResult point = cut_placement_point_from_ray(runtime.cut, {}, input).point;
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Cut needs a valid grid point." : point.message };
		}
		return begin_line_drag(runtime.cut, point.position);
	}
};

const CutTool& cut_tool_instance()
{
  static const CutTool kTool;
  return kTool;
}

} // namespace

const EditorTool& cut_tool()
{
	return cut_tool_instance();
}

std::vector<int> cut_target_object_ids_for_selection(const RectangleSelectionState& selection)
{
	return cut_tool_instance().target_object_ids_for_selection(selection);
}

CutToolResult set_cut_targets_from_selection(CutToolState& state, const RectangleSelectionState& selection)
{
	return cut_tool_instance().set_targets_from_selection(state, selection);
}

CutToolResult capture_first_cut_point(CutToolState& state, const RectangleSelectionState& selection, quader::QVec3 position, quader::QVec3 plane_normal)
{
	return cut_tool_instance().capture_first_point(state, selection, position, plane_normal);
}

CutToolResult capture_second_cut_point(CutToolState& state, quader::QVec3 position)
{
	return cut_tool_instance().capture_second_point(state, position);
}

CutToolResult update_dragged_new_cut_point(CutToolState& state, quader::QVec3 position)
{
	return cut_tool_instance().update_dragged_new_point(state, position);
}

CutToolResult update_dragged_cut_point(CutToolState& state, quader::QVec3 position, quader::QVec3 plane_normal)
{
	return cut_tool_instance().update_dragged_point(state, position, plane_normal);
}

CutToolResult begin_cut_point_drag(CutToolState& state, int point_index)
{
	return cut_tool_instance().begin_point_drag(state, point_index);
}

CutToolResult begin_cut_line_drag(CutToolState& state, quader::QVec3 anchor)
{
	return cut_tool_instance().begin_line_drag(state, anchor);
}

CutToolResult update_dragged_cut_line(CutToolState& state, quader::QVec3 position)
{
	return cut_tool_instance().update_dragged_line(state, position);
}

CutToolResult end_cut_point_drag(CutToolState& state)
{
	return cut_tool_instance().end_point_drag(state);
}

CutToolResult end_new_cut_point_drag(CutToolState& state)
{
	return cut_tool_instance().end_new_point_drag(state);
}

CutToolResult end_cut_line_drag(CutToolState& state)
{
	return cut_tool_instance().end_line_drag(state);
}

CutToolResult set_cut_mode(CutToolState& state, CutMode mode)
{
	return cut_tool_instance().set_mode(state, mode);
}

std::vector<quader::QVec3> cut_overlay_points(const CutToolState& state, quader::QVec3 hover_position, bool has_hover)
{
	return cut_tool_instance().overlay_points(state, hover_position, has_hover);
}

std::vector<quader::QVec3> cut_plane_points(const CutToolState& state)
{
	return cut_tool_instance().plane_points(state);
}

std::vector<int> dirty_object_ids_for_cut(const std::vector<int>& source_ids, const std::vector<int>& result_ids)
{
	return cut_tool_instance().dirty_object_ids(source_ids, result_ids);
}

bool cut_point_repeats_existing(const CutToolState& state, quader::QVec3 point)
{
	return cut_tool_instance().point_repeats_existing(state, point);
}

bool cut_point_repeats_other(const CutToolState& state, quader::QVec3 point, int point_index)
{
	return cut_tool_instance().point_repeats_other(state, point, point_index);
}

CutPreviewRequest cut_preview_request(CutToolState& state, CutMode mode)
{
	return cut_tool_instance().preview_request(state, mode);
}

CutToolResult set_cut_preview_result(CutToolState& state, CutMode mode, bool ok, const CutPreviewCounts& counts, std::string message)
{
	return cut_tool_instance().set_preview_result(state, mode, ok, counts, std::move(message));
}

CutToolResult invalidate_cut_preview(CutToolState& state)
{
	return cut_tool_instance().invalidate_preview(state);
}

CutToolResult commit_reset_cut(CutToolState& state, const std::vector<int>& result_object_ids)
{
	return cut_tool_instance().commit_reset(state, result_object_ids);
}

void reset_cut(CutToolState& state)
{
	cut_tool_instance().reset(state);
}

bool cut_is_dragging(const CutToolState& state)
{
	return cut_tool_instance().is_dragging(state);
}

bool cut_is_active(const CutToolState& state)
{
	return cut_tool_instance().is_active(state);
}

bool cut_can_preview(const CutToolState& state)
{
	return cut_tool_instance().can_preview(state);
}

CutPreviewMetadata cut_preview_metadata(const CutToolState& state)
{
	return cut_tool_instance().preview_metadata(state);
}

std::string cut_preview_status(const CutToolState& state, CutMode mode, const CutPreviewCounts& fallback_counts)
{
	return cut_tool_instance().preview_status(state, mode, fallback_counts);
}

bool update_cut_hover(CutHoverState& state, bool valid, int object_id, quader::QVec3 position, double epsilon)
{
	return cut_tool_instance().update_hover(state, valid, object_id, position, epsilon);
}

CutToolResult refresh_cut_preview(CutToolState& state, std::span<const PolygonObject> objects)
{
	return cut_tool_instance().refresh_preview(state, objects);
}

CutToolResult refresh_cut_preview_from_selection(
		CutToolState& state,
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects)
{
	return cut_tool_instance().refresh_preview_from_selection(state, selection, objects);
}

CutToolResult update_cut_hover_from_ray(
		EditorRuntimeState& runtime,
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const CutRayUpdateInput& input)
{
	return cut_tool_instance().update_hover_from_ray(runtime, selection, objects, input);
}

CutToolResult click_cut_from_ray(
		EditorRuntimeState& runtime,
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const CutRayUpdateInput& input)
{
	return cut_tool_instance().click_from_ray(runtime, selection, objects, input);
}

CutToolResult begin_cut_line_drag_from_ray(
		EditorRuntimeState& runtime,
		const CutRayUpdateInput& input)
{
	return cut_tool_instance().begin_line_drag_from_ray(runtime, input);
}

} // namespace quader_editor_core
