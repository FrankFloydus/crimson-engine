////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/mirror_tool.h>
#include <diagnostics/profile.h>
#include <editor/modeling/runtime/editor_tool.h>
#include <mesh/polygon/document.h>

#include "editor/modeling/runtime/session_internal.h"
#include "tool_math_internal.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader_editor_core {
namespace {

MirrorToolResult mirror_result_from_drawn_plane(DrawnPlaneToolResult result, int target_count = 0)
{
	return {
		result.ok,
		result.changed,
		std::move(result.message),
		target_count,
	};
}

void invalidate_mirror_plane_preview(MirrorToolState& state)
{
	state.preview_valid = false;
	state.preview_counts = {};
}

const PolygonObject* object_by_id(std::span<const PolygonObject> objects, int object_id)
{
	for (const PolygonObject& object : objects) {
		if (object.id == object_id) {
			return &object;
		}
	}
	return nullptr;
}

bool id_span_contains(std::span<const int> ids, int value)
{
	return std::find(ids.begin(), ids.end(), value) != ids.end();
}

quader::QVec3 bounds_center_for_flip(const Bounds3& bounds)
{
	if (bounds.empty) {
		return {};
	}
	return {
		(bounds.min.x + bounds.max.x) * 0.5F,
		(bounds.min.y + bounds.max.y) * 0.5F,
		(bounds.min.z + bounds.max.z) * 0.5F,
	};
}

/**
 * Coordinates Mirror Operation Service responsibilities for the modal modeling interaction layer.
 */
class MirrorOperationService final {
public:
	void mirror_document(quader_poly::Document& document, const DrawnPlaneSurface& surface) const
	{
		for (quader_poly::Vertex& vertex : document.vertices) {
			vertex.position = reflect_point_across_plane(vertex.position, surface.origin, surface.normal);
		}
		for (quader_poly::Face& face : document.faces) {
			std::reverse(face.vertices.begin(), face.vertices.end());
			if (face.uvs.size() == face.vertices.size()) {
				std::reverse(face.uvs.begin(), face.uvs.end());
			}
		}
	}

	[[nodiscard]] DrawnPlaneSurface world_axis_surface(TransformAxis axis) const
	{
		DrawnPlaneSurface surface;
		surface.valid = true;
		surface.origin = {};
		switch (axis) {
			case TransformAxis::x:
				surface.normal = { 1.0F, 0.0F, 0.0F };
				break;
			case TransformAxis::y:
				surface.normal = { 0.0F, 1.0F, 0.0F };
				break;
			case TransformAxis::z:
				surface.normal = { 0.0F, 0.0F, 1.0F };
				break;
			case TransformAxis::none:
			case TransformAxis::xy:
			case TransformAxis::xz:
			case TransformAxis::yz:
			case TransformAxis::all:
			default:
				return { false, {}, {}, "Mirror axis must be X, Y, or Z." };
		}
		return surface;
	}

	[[nodiscard]] std::vector<quader::QVec3> world_axis_plane_points(TransformAxis axis) const
	{
		switch (axis) {
			case TransformAxis::x:
				return { {}, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } };
			case TransformAxis::y:
				return { {}, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } };
			case TransformAxis::z:
				return { {}, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } };
			case TransformAxis::none:
			case TransformAxis::xy:
			case TransformAxis::xz:
			case TransformAxis::yz:
			case TransformAxis::all:
			default:
				return {};
		}
	}

	[[nodiscard]] DrawnPlaneSurface mesh_flip_surface(
			ViewportFlipOrientation orientation,
			quader::QVec3 pivot) const
	{
		DrawnPlaneSurface surface;
		surface.valid = true;
		surface.origin = pivot;
		if (orientation == ViewportFlipOrientation::horizontal) {
			surface.normal = { 1.0F, 0.0F, 0.0F };
			return surface;
		}
		if (orientation == ViewportFlipOrientation::vertical) {
			surface.normal = { 0.0F, 1.0F, 0.0F };
			return surface;
		}
		return { false, {}, {}, "Flip orientation must be horizontal or vertical." };
	}

	[[nodiscard]] CommandResult mirror_objects(
			std::vector<PolygonObject>& objects,
			std::span<const int> object_ids,
			const DrawnPlaneSurface& surface,
			int& next_object_id,
			int& active_object_id,
			SelectionMode& mode) const
	{
		if (object_ids.empty()) {
			return error_result("Select one or more mesh objects to mirror.");
		}
		if (!surface.valid) {
			return error_result(surface.message.empty() ? "Mirror needs a valid plane." : surface.message);
		}

		std::vector<PolygonObject> candidate = objects;
		int candidate_next_object_id = next_polygon_object_id(candidate, next_object_id);
		for (PolygonObject& object : candidate) {
			object.selected = false;
			object.selection = quader_poly::Selection();
		}

		std::vector<int> mirrored_ids;
		mirrored_ids.reserve(object_ids.size());
		for (const int object_id : object_ids) {
			const int object_index = object_index_by_id(candidate, object_id);
			if (object_index < 0) {
				return error_result("Mirror target mesh was not found.");
			}
			PolygonObject copy = candidate[static_cast<std::size_t>(object_index)];
			copy.id = candidate_next_object_id++;
			copy.selected = true;
			copy.selection = quader_poly::Selection();
			mirror_document(copy.document, surface);
			if (quader_poly::compile_document(copy.document).indices.empty()) {
				return error_result("Could not mirror the selected mesh.");
			}
			mirrored_ids.push_back(copy.id);
			candidate.push_back(std::move(copy));
		}

		if (mirrored_ids.empty()) {
			return error_result("Select one or more mesh objects to mirror.");
		}

		objects = std::move(candidate);
		next_object_id = candidate_next_object_id;
		mode = SelectionMode::object;
		active_object_id = mirrored_ids.back();
		normalize_active_object_id(objects, active_object_id);
		return ok_result(true);
	}

	[[nodiscard]] CommandResult flip_objects(
			std::vector<PolygonObject>& objects,
			std::span<const int> object_ids,
			ViewportFlipOrientation orientation,
			int& active_object_id,
			SelectionMode& mode) const
	{
		if (mode != SelectionMode::object) {
			return error_result("Flip works only in object/mesh selection mode.");
		}
		if (object_ids.empty()) {
			return error_result("Select one or more mesh objects to flip.");
		}
		const DrawnPlaneSurface orientation_check = mesh_flip_surface(orientation, {});
		if (!orientation_check.valid) {
			return error_result(orientation_check.message.empty() ? "Flip needs a valid orientation." : orientation_check.message);
		}

		std::vector<PolygonObject> candidate = objects;
		for (const int object_id : object_ids) {
			const int object_index = object_index_by_id(candidate, object_id);
			if (object_index < 0) {
				return error_result("Flip target mesh was not found.");
			}
			PolygonObject& object = candidate[static_cast<std::size_t>(object_index)];
			const Bounds3 bounds = document_bounds(object);
			if (bounds.empty) {
				return error_result("Flip needs selected mesh bounds.");
			}
			const DrawnPlaneSurface surface = mesh_flip_surface(orientation, bounds_center_for_flip(bounds));
			mirror_document(object.document, surface);
			object.selection = quader_poly::Selection();
			if (quader_poly::compile_document(object.document).indices.empty()) {
				return error_result("Could not flip the selected mesh.");
			}
		}

		objects = std::move(candidate);
		mode = SelectionMode::object;
		if (!id_span_contains(object_ids, active_object_id)) {
			active_object_id = object_ids.back();
		}
		normalize_active_object_id(objects, active_object_id);
		return ok_result(true);
	}
};

const MirrorOperationService& mirror_operation_service()
{
	static const MirrorOperationService service;
	return service;
}

/**
 * Implements Mirror Tool behavior for the modal modeling interaction layer.
 */
class MirrorTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::mirror;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Mirror";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool keep_active_when_current_tool) const override
	{
		reset_editor_runtime_mirror_state(runtime, keep_active_when_current_tool);
	}

	[[nodiscard]] std::vector<int> target_object_ids_for_selection(const RectangleSelectionState& selection) const
	{
		return selection.mode == SelectionMode::object ? selection.selected_object_ids : std::vector<int> {};
	}

	[[nodiscard]] MirrorToolResult set_targets_from_selection(MirrorToolState& state, const RectangleSelectionState& selection) const
	{
		const std::vector<int> targets = target_object_ids_for_selection(selection);
		if (targets == state.target_object_ids) {
			return { true, false, {}, static_cast<int>(state.target_object_ids.size()) };
		}
		state.target_object_ids = targets;
		invalidate_mirror_plane_preview(state);
		return { true, true, {}, static_cast<int>(state.target_object_ids.size()) };
	}

	[[nodiscard]] MirrorToolResult capture_first_point(
			MirrorToolState& state,
			const RectangleSelectionState& selection,
			quader::QVec3 position,
			quader::QVec3 plane_normal) const
	{
		(void)set_targets_from_selection(state, selection);
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().capture_first(drawn_plane_state_ref(state.plane), position, plane_normal, label());
		if (result.ok && result.changed) {
			invalidate_mirror_plane_preview(state);
		}
		return mirror_result_from_drawn_plane(result, static_cast<int>(state.target_object_ids.size()));
	}

	[[nodiscard]] MirrorToolResult capture_second_point(MirrorToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().capture_second(drawn_plane_state_ref(state.plane), position, label());
		if (result.ok && result.changed) {
			invalidate_mirror_plane_preview(state);
		}
		return mirror_result_from_drawn_plane(result);
	}

	[[nodiscard]] MirrorToolResult update_dragged_new_point(MirrorToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_new_point(drawn_plane_state_ref(state.plane), position, label());
		if (result.ok && result.changed) {
			invalidate_mirror_plane_preview(state);
		}
		return mirror_result_from_drawn_plane(result);
	}

	[[nodiscard]] MirrorToolResult update_dragged_point(MirrorToolState& state, quader::QVec3 position, quader::QVec3 plane_normal) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_point(drawn_plane_state_ref(state.plane), position, plane_normal, label());
		if (result.ok && result.changed) {
			invalidate_mirror_plane_preview(state);
		}
		return mirror_result_from_drawn_plane(result);
	}

	[[nodiscard]] MirrorToolResult begin_point_drag(MirrorToolState& state, int point_index) const
	{
		return mirror_result_from_drawn_plane(
			drawn_plane_tool_logic().begin_point_drag(drawn_plane_state_ref(state.plane), point_index, label()));
	}

	[[nodiscard]] MirrorToolResult begin_line_drag(MirrorToolState& state, quader::QVec3 anchor) const
	{
		return mirror_result_from_drawn_plane(
			drawn_plane_tool_logic().begin_line_drag(drawn_plane_state_ref(state.plane), anchor, label()));
	}

	[[nodiscard]] MirrorToolResult update_dragged_line(MirrorToolState& state, quader::QVec3 position) const
	{
		const DrawnPlaneToolResult result =
			drawn_plane_tool_logic().update_dragged_line(drawn_plane_state_ref(state.plane), position, label());
		if (result.ok && result.changed) {
			invalidate_mirror_plane_preview(state);
		}
		return mirror_result_from_drawn_plane(result);
	}

	[[nodiscard]] MirrorToolResult end_point_drag(MirrorToolState& state) const
	{
		return mirror_result_from_drawn_plane(drawn_plane_tool_logic().end_point_drag(drawn_plane_state_ref(state.plane)));
	}

	[[nodiscard]] MirrorToolResult end_new_point_drag(MirrorToolState& state) const
	{
		return mirror_result_from_drawn_plane(drawn_plane_tool_logic().end_new_point_drag(drawn_plane_state_ref(state.plane)));
	}

	[[nodiscard]] MirrorToolResult end_line_drag(MirrorToolState& state) const
	{
		return mirror_result_from_drawn_plane(drawn_plane_tool_logic().end_line_drag(drawn_plane_state_ref(state.plane)));
	}

	[[nodiscard]] std::vector<quader::QVec3> overlay_points(
			const MirrorToolState& state,
			quader::QVec3 hover_position,
			bool has_hover) const
	{
		return drawn_plane_tool_logic().overlay_points(drawn_plane_state_ref(state.plane), hover_position, has_hover);
	}

	[[nodiscard]] std::vector<quader::QVec3> plane_points(const MirrorToolState& state) const
	{
		return drawn_plane_tool_logic().plane_points(drawn_plane_state_ref(state.plane));
	}

	[[nodiscard]] bool point_repeats_existing(const MirrorToolState& state, quader::QVec3 point) const
	{
		return drawn_plane_tool_logic().point_repeats_existing(drawn_plane_state_ref(state.plane), point);
	}

	[[nodiscard]] bool point_repeats_other(const MirrorToolState& state, quader::QVec3 point, int point_index) const
	{
		return drawn_plane_tool_logic().point_repeats_other(drawn_plane_state_ref(state.plane), point, point_index);
	}

	[[nodiscard]] MirrorPreviewRequest preview_request(MirrorToolState& state) const
	{
		MirrorPreviewRequest request;
		request.point_count = static_cast<int>(state.plane.points.size());
		request.target_count = static_cast<int>(state.target_object_ids.size());
		request.preview_valid = state.preview_valid;
		if (!can_preview(state)) {
			request.ok = false;
			request.message = state.plane.points.size() < 2 ? "Mirror needs at least two points." : "Select one or more mesh objects to mirror.";
			request.can_preview = false;
			return request;
		}
		request.ok = true;
		request.can_preview = true;
		request.object_ids = state.target_object_ids;
		request.points = plane_points(state);
		return request;
	}

	[[nodiscard]] MirrorToolResult set_preview_result(
			MirrorToolState& state,
			bool ok,
			const MirrorPreviewCounts& counts,
			std::string message) const
	{
		if (!ok) {
			invalidate_mirror_plane_preview(state);
			return { false, false, message.empty() ? "invalid mirror plane" : std::move(message) };
		}
		state.preview_valid = true;
		state.preview_counts = counts;
		return { true, false };
	}

	[[nodiscard]] MirrorToolResult invalidate_preview(MirrorToolState& state) const
	{
		invalidate_mirror_plane_preview(state);
		return { true, false };
	}

	[[nodiscard]] MirrorToolResult commit_reset(MirrorToolState& state, const std::vector<int>& result_object_ids) const
	{
		const int target_count = static_cast<int>(state.target_object_ids.size());
		std::vector<int> dirty_ids = merge_unique_ids(state.target_object_ids, result_object_ids);
		reset(state);
		MirrorToolResult result;
		result.ok = true;
		result.target_count = target_count;
		result.dirty_object_ids = std::move(dirty_ids);
		return result;
	}

	void reset(MirrorToolState& state) const
	{
		state = MirrorToolState();
	}

	[[nodiscard]] bool is_dragging(const MirrorToolState& state) const
	{
		return drawn_plane_tool_logic().is_dragging(drawn_plane_state_ref(state.plane));
	}

	[[nodiscard]] bool is_active(const MirrorToolState& state) const
	{
		return !state.plane.points.empty() || !state.target_object_ids.empty() || is_dragging(state) || state.preview_valid;
	}

	[[nodiscard]] bool can_preview(const MirrorToolState& state) const
	{
		return state.plane.points.size() >= 2U && !state.target_object_ids.empty();
	}

	[[nodiscard]] MirrorPreviewMetadata preview_metadata(const MirrorToolState& state) const
	{
		MirrorPreviewMetadata metadata;
		metadata.preview_valid = state.preview_valid;
		metadata.can_preview = can_preview(state);
		metadata.point_count = static_cast<int>(state.plane.points.size());
		metadata.target_count = state.preview_counts.target_count > 0 ? state.preview_counts.target_count : static_cast<int>(state.target_object_ids.size());
		metadata.mirrored_count = state.preview_counts.mirrored_count;
		return metadata;
	}

	[[nodiscard]] std::string preview_status(const MirrorToolState& state, const MirrorPreviewCounts& fallback_counts) const
	{
		const MirrorPreviewMetadata metadata = preview_metadata(state);
		const int mirrored_count = metadata.mirrored_count > 0 ? metadata.mirrored_count : fallback_counts.mirrored_count;
		const int target_count = metadata.target_count > 0 ? metadata.target_count : fallback_counts.target_count;
		return "Mirror preview: " +
			std::to_string(mirrored_count) +
			"/" +
			std::to_string(target_count) +
			" mirrored. Enter/Space confirms.";
	}

	[[nodiscard]] bool update_hover(MirrorHoverState& state, bool valid, int object_id, quader::QVec3 position, double epsilon) const
	{
		return drawn_plane_tool_logic().update_hover(state, valid, object_id, position, epsilon);
	}

	[[nodiscard]] MirrorToolResult refresh_preview(MirrorToolState& state, std::span<const PolygonObject> objects) const
	{
		QDR_PROFILE_SCOPE("qdr_editor_core.MirrorTool.refresh_preview");
		if (!can_preview(state)) {
			return invalidate_preview(state);
		}

		const DrawnPlaneSurface surface = drawn_plane_surface_from_points(plane_points(state));
		if (!surface.valid) {
			return set_preview_result(state, false, {}, surface.message);
		}

		MirrorPreviewCounts counts;
		counts.target_count = static_cast<int>(state.target_object_ids.size());
		for (const int object_id : state.target_object_ids) {
			const PolygonObject* object = object_by_id(objects, object_id);
			if (object == nullptr) {
				return set_preview_result(state, false, {}, "Mirror target mesh was not found.");
			}
			PolygonObject copy = *object;
			mirror_document_across_drawn_plane(copy.document, surface);
			if (quader_poly::compile_document(copy.document).indices.empty()) {
				return set_preview_result(state, false, {}, "Mirror preview failed.");
			}
			++counts.mirrored_count;
		}
		return set_preview_result(state, true, counts, {});
	}

	[[nodiscard]] MirrorToolResult refresh_preview_from_selection(
			MirrorToolState& state,
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects) const
	{
		MirrorToolResult result = set_targets_from_selection(state, selection);
		if (!result.ok) {
			return result;
		}
		return refresh_preview(state, objects);
	}

	[[nodiscard]] std::vector<quader_poly::Document> preview_documents(
			const MirrorToolState& state,
			std::span<const PolygonObject> objects) const
	{
		QDR_PROFILE_SCOPE("qdr_editor_core.MirrorTool.preview_documents");
		std::vector<quader_poly::Document> documents;
		if (!can_preview(state)) {
			return documents;
		}
		const DrawnPlaneSurface surface = drawn_plane_surface_from_points(plane_points(state));
		if (!surface.valid) {
			return documents;
		}
		for (const int object_id : state.target_object_ids) {
			const PolygonObject* object = object_by_id(objects, object_id);
			if (object == nullptr) {
				continue;
			}
			quader_poly::Document document = object->document;
			mirror_document_across_drawn_plane(document, surface);
			documents.push_back(std::move(document));
		}
		return documents;
	}

	[[nodiscard]] MirrorToolResult update_hover_from_ray(
			EditorRuntimeState& runtime,
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const MirrorRayUpdateInput& input) const
	{
		const MirrorToolResult target_result = set_targets_from_selection(runtime.mirror, selection);
		if (!target_result.ok) {
			return target_result;
		}

		const CutGridPointResult point =
			cut_grid_point_from_ray(input.camera, input.ray, input.grid_size);
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Mirror needs a valid grid point." : point.message };
		}
		const bool hover_changed = update_hover(runtime.mirror_hover, true, -1, point.position, 0.000001);
		if (!input.dragging) {
			(void)refresh_preview(runtime.mirror, objects);
			return { true, hover_changed };
		}

		MirrorToolResult result;
		if (runtime.mirror.plane.dragging_new_point) {
			result = update_dragged_new_point(runtime.mirror, point.position);
		} else if (runtime.mirror.plane.dragging_point_index >= 0) {
			result = update_dragged_point(runtime.mirror, point.position, point.plane_normal);
		} else if (runtime.mirror.plane.dragging_line) {
			result = update_dragged_line(runtime.mirror, point.position);
		} else {
			result = { true, hover_changed };
		}

		if (result.ok) {
			(void)refresh_preview(runtime.mirror, objects);
		}
		return result;
	}

	[[nodiscard]] MirrorToolResult click_from_ray(
			EditorRuntimeState& runtime,
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const MirrorRayUpdateInput& input) const
	{
		const MirrorToolResult target_result = set_targets_from_selection(runtime.mirror, selection);
		if (!target_result.ok) {
			return target_result;
		}

		const CutGridPointResult point =
			cut_grid_point_from_ray(input.camera, input.ray, input.grid_size);
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Mirror needs a valid grid point." : point.message };
		}
		(void)update_hover(runtime.mirror_hover, true, -1, point.position, 0.000001);

		MirrorToolResult result;
		if (runtime.mirror.plane.points.empty()) {
			result = capture_first_point(runtime.mirror, selection, point.position, point.plane_normal);
		} else if (runtime.mirror.plane.points.size() == 1U) {
			result = capture_second_point(runtime.mirror, point.position);
		} else {
			result = { true, false };
		}

		if (result.ok) {
			(void)refresh_preview(runtime.mirror, objects);
		}
		return result;
	}

	[[nodiscard]] MirrorToolResult begin_line_drag_from_ray(EditorRuntimeState& runtime, const MirrorRayUpdateInput& input) const
	{
		const CutGridPointResult point =
			cut_grid_point_from_ray(input.camera, input.ray, input.grid_size);
		if (!point.ok) {
			return { false, false, point.message.empty() ? "Mirror needs a valid grid point." : point.message };
		}
		return begin_line_drag(runtime.mirror, point.position);
	}
};

const MirrorTool& mirror_tool_instance()
{
	static const MirrorTool tool;
	return tool;
}

} // namespace

const EditorTool& mirror_tool()
{
	return mirror_tool_instance();
}

void mirror_document_across_drawn_plane(quader_poly::Document& document, const DrawnPlaneSurface& surface)
{
	mirror_operation_service().mirror_document(document, surface);
}

DrawnPlaneSurface mirror_world_axis_surface(TransformAxis axis)
{
	return mirror_operation_service().world_axis_surface(axis);
}

std::vector<quader::QVec3> mirror_world_axis_plane_points(TransformAxis axis)
{
	return mirror_operation_service().world_axis_plane_points(axis);
}

DrawnPlaneSurface mirror_mesh_flip_surface(
		ViewportFlipOrientation orientation,
		quader::QVec3 pivot)
{
	return mirror_operation_service().mesh_flip_surface(orientation, pivot);
}

CommandResult mirror_objects_across_surface(
		std::vector<PolygonObject>& objects,
		std::span<const int> object_ids,
		const DrawnPlaneSurface& surface,
		int& next_object_id,
		int& active_object_id,
		SelectionMode& mode)
{
	return mirror_operation_service().mirror_objects(
		objects,
		object_ids,
		surface,
		next_object_id,
		active_object_id,
		mode);
}

CommandResult flip_objects_by_orientation(
		std::vector<PolygonObject>& objects,
		std::span<const int> object_ids,
		ViewportFlipOrientation orientation,
		int& active_object_id,
		SelectionMode& mode)
{
	return mirror_operation_service().flip_objects(
		objects,
		object_ids,
		orientation,
		active_object_id,
		mode);
}

std::vector<int> mirror_target_object_ids_for_selection(const RectangleSelectionState& selection)
{
	return mirror_tool_instance().target_object_ids_for_selection(selection);
}

MirrorToolResult set_mirror_targets_from_selection(MirrorToolState& state, const RectangleSelectionState& selection)
{
	return mirror_tool_instance().set_targets_from_selection(state, selection);
}

MirrorToolResult capture_first_mirror_point(MirrorToolState& state, const RectangleSelectionState& selection, quader::QVec3 position, quader::QVec3 plane_normal)
{
	return mirror_tool_instance().capture_first_point(state, selection, position, plane_normal);
}

MirrorToolResult capture_second_mirror_point(MirrorToolState& state, quader::QVec3 position)
{
	return mirror_tool_instance().capture_second_point(state, position);
}

MirrorToolResult update_dragged_new_mirror_point(MirrorToolState& state, quader::QVec3 position)
{
	return mirror_tool_instance().update_dragged_new_point(state, position);
}

MirrorToolResult update_dragged_mirror_point(MirrorToolState& state, quader::QVec3 position, quader::QVec3 plane_normal)
{
	return mirror_tool_instance().update_dragged_point(state, position, plane_normal);
}

MirrorToolResult begin_mirror_point_drag(MirrorToolState& state, int point_index)
{
	return mirror_tool_instance().begin_point_drag(state, point_index);
}

MirrorToolResult begin_mirror_line_drag(MirrorToolState& state, quader::QVec3 anchor)
{
	return mirror_tool_instance().begin_line_drag(state, anchor);
}

MirrorToolResult update_dragged_mirror_line(MirrorToolState& state, quader::QVec3 position)
{
	return mirror_tool_instance().update_dragged_line(state, position);
}

MirrorToolResult end_mirror_point_drag(MirrorToolState& state)
{
	return mirror_tool_instance().end_point_drag(state);
}

MirrorToolResult end_new_mirror_point_drag(MirrorToolState& state)
{
	return mirror_tool_instance().end_new_point_drag(state);
}

MirrorToolResult end_mirror_line_drag(MirrorToolState& state)
{
	return mirror_tool_instance().end_line_drag(state);
}

std::vector<quader::QVec3> mirror_overlay_points(const MirrorToolState& state, quader::QVec3 hover_position, bool has_hover)
{
	return mirror_tool_instance().overlay_points(state, hover_position, has_hover);
}

std::vector<quader::QVec3> mirror_plane_points(const MirrorToolState& state)
{
	return mirror_tool_instance().plane_points(state);
}

bool mirror_point_repeats_existing(const MirrorToolState& state, quader::QVec3 point)
{
	return mirror_tool_instance().point_repeats_existing(state, point);
}

bool mirror_point_repeats_other(const MirrorToolState& state, quader::QVec3 point, int point_index)
{
	return mirror_tool_instance().point_repeats_other(state, point, point_index);
}

MirrorPreviewRequest mirror_preview_request(MirrorToolState& state)
{
	return mirror_tool_instance().preview_request(state);
}

MirrorToolResult set_mirror_preview_result(MirrorToolState& state, bool ok, const MirrorPreviewCounts& counts, std::string message)
{
	return mirror_tool_instance().set_preview_result(state, ok, counts, std::move(message));
}

MirrorToolResult invalidate_mirror_preview(MirrorToolState& state)
{
	return mirror_tool_instance().invalidate_preview(state);
}

MirrorToolResult commit_reset_mirror(MirrorToolState& state, const std::vector<int>& result_object_ids)
{
	return mirror_tool_instance().commit_reset(state, result_object_ids);
}

void reset_mirror(MirrorToolState& state)
{
	mirror_tool_instance().reset(state);
}

bool mirror_is_dragging(const MirrorToolState& state)
{
	return mirror_tool_instance().is_dragging(state);
}

bool mirror_is_active(const MirrorToolState& state)
{
	return mirror_tool_instance().is_active(state);
}

bool mirror_can_preview(const MirrorToolState& state)
{
	return mirror_tool_instance().can_preview(state);
}

MirrorPreviewMetadata mirror_preview_metadata(const MirrorToolState& state)
{
	return mirror_tool_instance().preview_metadata(state);
}

std::string mirror_preview_status(const MirrorToolState& state, const MirrorPreviewCounts& fallback_counts)
{
	return mirror_tool_instance().preview_status(state, fallback_counts);
}

bool update_mirror_hover(MirrorHoverState& state, bool valid, int object_id, quader::QVec3 position, double epsilon)
{
	return mirror_tool_instance().update_hover(state, valid, object_id, position, epsilon);
}

MirrorToolResult refresh_mirror_preview(MirrorToolState& state, std::span<const PolygonObject> objects)
{
	return mirror_tool_instance().refresh_preview(state, objects);
}

MirrorToolResult refresh_mirror_preview_from_selection(
		MirrorToolState& state,
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects)
{
	return mirror_tool_instance().refresh_preview_from_selection(state, selection, objects);
}

std::vector<quader_poly::Document> mirror_preview_documents(
		const MirrorToolState& state,
		std::span<const PolygonObject> objects)
{
	return mirror_tool_instance().preview_documents(state, objects);
}

MirrorToolResult update_mirror_hover_from_ray(
		EditorRuntimeState& runtime,
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const MirrorRayUpdateInput& input)
{
	return mirror_tool_instance().update_hover_from_ray(runtime, selection, objects, input);
}

MirrorToolResult click_mirror_from_ray(
		EditorRuntimeState& runtime,
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const MirrorRayUpdateInput& input)
{
	return mirror_tool_instance().click_from_ray(runtime, selection, objects, input);
}

MirrorToolResult begin_mirror_line_drag_from_ray(
		EditorRuntimeState& runtime,
		const MirrorRayUpdateInput& input)
{
	return mirror_tool_instance().begin_line_drag_from_ray(runtime, input);
}

} // namespace quader_editor_core
