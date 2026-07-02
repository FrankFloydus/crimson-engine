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
#include <mesh/polygon/document.hpp>

#include "tool_math_internal.hpp"

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
		return object_operation_object_ids_for_selection(selection);
	}

	[[nodiscard]] const PolygonObject* object_by_id(std::span<const PolygonObject> objects, int object_id) const
	{
		for (const PolygonObject& object : objects) {
			if (object.id == object_id) {
				return &object;
			}
		}
		return nullptr;
	}

	[[nodiscard]] quader_poly::PlaneCutMode plane_cut_mode(CutMode mode) const
	{
		switch (mode) {
                case CutMode::KeepBack:
                  return quader_poly::PlaneCutMode::KeepBack;
                case CutMode::KeepFront:
                  return quader_poly::PlaneCutMode::KeepFront;
                case CutMode::KeepBoth:
                default:
                  return quader_poly::PlaneCutMode::KeepBoth;
                }
	}

	[[nodiscard]] quader_poly::PlaneCutRequest plane_cut_request(const CutToolState& state) const
	{
		const std::vector<quader::QVec3> points = plane_points(state);
		quader_poly::PlaneCutRequest request;
		request.mode = plane_cut_mode(state.mode);
		if (points.size() >= 3U) {
			request.first_point = points[0];
			request.second_point = points[1];
			request.third_point = points[2];
		}
		return request;
	}

	[[nodiscard]] CutToolResult set_targets_from_selection(CutToolState& state, const RectangleSelectionState& selection) const
	{
		const std::vector<int> targets = target_object_ids_for_selection(selection);
		if (targets == state.target_object_ids) {
			return { true, false, {}, static_cast<int>(state.target_object_ids.size()) };
		}
		state.target_object_ids = targets;
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
		(void)set_targets_from_selection(state, selection);
		state.source_object_id = -1;
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().capture_first(cut_drawn_plane_ref(state), position, plane_normal, label());
		if (result.ok && result.changed) {
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result, static_cast<int>(state.target_object_ids.size()));
	}

	[[nodiscard]] CutToolResult capture_second_point(CutToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().capture_second(cut_drawn_plane_ref(state), position, label());
		if (result.ok && result.changed) {
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult update_dragged_new_point(CutToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_new_point(cut_drawn_plane_ref(state), position, label());
		if (result.ok && result.changed) {
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult update_dragged_point(CutToolState& state, quader::QVec3 position, quader::QVec3 plane_normal) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_point(cut_drawn_plane_ref(state), position, plane_normal, label());
		if (result.ok && result.changed) {
			invalidate_cut_plane_preview(state);
		}
		return cut_result_from_drawn_plane(result);
	}

	[[nodiscard]] CutToolResult begin_point_drag(CutToolState& state, int point_index) const
	{
		return cut_result_from_drawn_plane(
			drawn_plane_tool_logic().begin_point_drag(cut_drawn_plane_ref(state), point_index, label()));
	}

	[[nodiscard]] CutToolResult begin_line_drag(CutToolState& state, quader::QVec3 anchor) const
	{
		return cut_result_from_drawn_plane(
			drawn_plane_tool_logic().begin_line_drag(cut_drawn_plane_ref(state), anchor, label()));
	}

	[[nodiscard]] CutToolResult update_dragged_line(CutToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_line(cut_drawn_plane_ref(state), position, label());
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
		state.mode = mode;
		CutPreviewRequest request;
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
		state.mode = mode;
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
		if (!can_preview(state)) {
			return invalidate_preview(state);
		}

		const quader_poly::PlaneCutRequest request = plane_cut_request(state);
		CutPreviewCounts counts;
		counts.target_count = static_cast<int>(state.target_object_ids.size());
		std::string message;
		for (const int object_id : state.target_object_ids) {
			const PolygonObject* object = object_by_id(objects, object_id);
			if (object == nullptr) {
				message = "Cut target mesh was not found.";
				break;
			}
			const quader_poly::PlaneCutResult result = quader_poly::plane_cut(object->document, request);
			if (!result.changed) {
				message = result.message.empty() ? "Cut preview failed." : result.message;
				break;
			}
			counts.split_count += result.split_count;
			counts.removed_count += result.removed_count;
		}
		if (!message.empty()) {
			return set_preview_result(state, state.mode, false, {}, std::move(message));
		}
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

	[[nodiscard]] std::vector<quader_poly::Document> preview_documents(
			const CutToolState& state,
			std::span<const PolygonObject> objects,
			CutPreviewMeshSide side) const
	{
		QDR_PROFILE_SCOPE("qdr_editor_core.CutTool.preview_documents");
		std::vector<quader_poly::Document> documents;
		if (!can_preview(state)) {
			return documents;
		}

		const quader_poly::PlaneCutRequest request = plane_cut_request(state);
		for (const int object_id : state.target_object_ids) {
			const PolygonObject* object = object_by_id(objects, object_id);
			if (object == nullptr) {
				continue;
			}
			quader_poly::PlaneCutResult result = quader_poly::plane_cut(object->document, request);
			if (!result.changed) {
				continue;
			}

                        const bool keep_front = state.mode != CutMode::KeepBack;
                        if (side == CutPreviewMeshSide::Keep) {
                          documents.push_back(
                              keep_front ? std::move(result.front_document)
                                         : std::move(result.back_document));
                        } else {
                          documents.push_back(
                              keep_front ? std::move(result.back_document)
                                         : std::move(result.front_document));
                        }
                }
		return documents;
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

		const CutGridPointResult point =
			cut_grid_point_from_ray(input.camera, input.ray, input.grid_size);
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Cut needs a valid grid point." : point.message };
		}
		const bool hover_changed = update_hover(runtime.cut_hover, true, -1, point.position, 0.000001);
		if (!input.dragging) {
			(void)refresh_preview(runtime.cut, objects);
			return { true, hover_changed };
		}

		CutToolResult result;
		if (runtime.cut.dragging_new_point) {
			result = update_dragged_new_point(runtime.cut, point.position);
		} else if (runtime.cut.dragging_point_index >= 0) {
			result = update_dragged_point(runtime.cut, point.position, point.plane_normal);
		} else if (runtime.cut.dragging_line) {
			result = update_dragged_line(runtime.cut, point.position);
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

		const CutGridPointResult point =
			cut_grid_point_from_ray(input.camera, input.ray, input.grid_size);
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Cut needs a valid grid point." : point.message };
		}
		(void)update_hover(runtime.cut_hover, true, -1, point.position, 0.000001);

		CutToolResult result;
		if (runtime.cut.points.empty()) {
			result = capture_first_point(runtime.cut, selection, point.position, point.plane_normal);
		} else if (runtime.cut.points.size() == 1U) {
			result = capture_second_point(runtime.cut, point.position);
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
		const CutGridPointResult point =
			cut_grid_point_from_ray(input.camera, input.ray, input.grid_size);
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

quader_poly::PlaneCutMode plane_cut_mode_from_cut_mode(CutMode mode)
{
	return cut_tool_instance().plane_cut_mode(mode);
}

quader_poly::PlaneCutRequest plane_cut_request_from_cut_state(const CutToolState& state)
{
	return cut_tool_instance().plane_cut_request(state);
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

std::vector<quader_poly::Document> cut_preview_documents(
		const CutToolState& state,
		std::span<const PolygonObject> objects,
		CutPreviewMeshSide side)
{
	return cut_tool_instance().preview_documents(state, objects, side);
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
