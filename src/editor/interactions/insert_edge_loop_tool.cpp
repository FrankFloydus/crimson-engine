////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/insert_edge_loop_tool.h>
#include <editor/interactions/tool_snapping.h>
#include <editor/modeling/runtime/tool_operation_contracts.h>

#include "editor/modeling/runtime/session_internal.h"

#include <algorithm>
#include <optional>

namespace quader_editor_core {
namespace {

/**
 * Implements Insert Edge Loop Tool behavior for the modal modeling interaction layer.
 */
class InsertEdgeLoopTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::insert_edge_loop;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Insert Edge Loop";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool keep_active_when_current_tool = false) const override
	{
		reset_editor_runtime_insert_edge_loop_state(runtime, keep_active_when_current_tool);
	}
};

InsertEdgeLoopPreview build_preview(const std::vector<PolygonObject>& objects, int object_id, quader_poly::Edge edge, float factor)
{
	InsertEdgeLoopPreview preview;
	preview.object_id = object_id;
	preview.edge = quader_poly::make_edge(edge.a, edge.b);
	preview.factor = std::clamp(factor, 0.01F, 0.99F);

	const int object_index = object_index_by_id(objects, object_id);
	if (object_index < 0) {
		preview.message = "Select an edge to insert an edge loop.";
		return preview;
	}

	const quader_poly::OverlayData overlay = quader_poly::build_edge_loop_preview(objects[object_index].document, preview.edge, preview.factor);
	preview.lines.reserve(overlay.lines.size());
	for (const quader_poly::OverlayLine& line : overlay.lines) {
		preview.lines.push_back({
			SelectionOverlayLayer::hover_edge,
			line.start,
			line.end,
			line.color,
			line.width_pixels,
		});
	}
	preview.valid = !preview.lines.empty();
	if (!preview.valid) {
		preview.message = "Edge loop insertion needs at least one quad face.";
	}
	return preview;
}

bool has_seed(const InsertEdgeLoopToolState& state)
{
	return state.object_id > 0 &&
		state.edge.a != quader_poly::invalid_element_id &&
		state.edge.b != quader_poly::invalid_element_id &&
		state.edge.a != state.edge.b;
}

void reset(InsertEdgeLoopToolState& state)
{
	state = {};
}

void clear_preview(
		InsertEdgeLoopToolState& state,
		SelectionHoverState& hover,
		int object_id,
		quader_poly::Edge edge,
		float factor,
		bool dragging)
{
	state.active = true;
	state.dragging = dragging;
	state.object_id = object_id;
	state.edge = quader_poly::make_edge(edge.a, edge.b);
	state.factor = std::clamp(factor, 0.01F, 0.99F);
	state.preview_lines.clear();

	hover.object_id = object_id;
	hover.kind = SelectionHoverKind::edge;
	hover.edge = state.edge;
	hover.vertex_id = quader_poly::invalid_element_id;
	hover.face_id = quader_poly::invalid_element_id;
	hover.selected_modifier_pressed = false;
	hover.candidate_object_id = object_id;
}

bool set_preview(
		InsertEdgeLoopToolState& state,
		const std::vector<PolygonObject>& objects,
		SelectionHoverState& hover,
		int object_id,
		quader_poly::Edge edge,
		float factor,
		bool dragging)
{
	const InsertEdgeLoopPreview preview = build_preview(objects, object_id, edge, factor);
	state.active = true;
	state.dragging = dragging;
	state.object_id = object_id;
	state.edge = quader_poly::make_edge(edge.a, edge.b);
	state.factor = std::clamp(factor, 0.01F, 0.99F);
	state.preview_lines = preview.valid ? preview.lines : std::vector<SelectionOverlayLine> {};

	hover.object_id = object_id;
	hover.kind = SelectionHoverKind::edge;
	hover.edge = state.edge;
	hover.vertex_id = quader_poly::invalid_element_id;
	hover.face_id = quader_poly::invalid_element_id;
	hover.selected_modifier_pressed = false;
	hover.candidate_object_id = object_id;
	return preview.valid;
}

bool update_locked_seed(
		InsertEdgeLoopToolState& state,
		const std::vector<PolygonObject>& objects,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool dragging)
{
	if (!has_seed(state)) {
		return false;
	}

	const int object_index = object_index_by_id(objects, state.object_id);
	if (object_index < 0) {
		return false;
	}

	const std::optional<float> raw_factor = quader_poly::edge_factor_from_ray(objects[object_index].document, state.edge, ray);
	const std::optional<float> factor = snap_insert_edge_loop_preview_factor(
		objects[object_index].document,
		state.edge,
		raw_factor.value_or(state.factor),
		grid_size,
		snap_enabled);
	if (!factor.has_value()) {
		clear_preview(state, hover, state.object_id, state.edge, raw_factor.value_or(state.factor), dragging);
		return false;
	}
	return set_preview(state, objects, hover, state.object_id, state.edge, *factor, dragging);
}

} // namespace

const EditorTool& insert_edge_loop_tool()
{
	static const InsertEdgeLoopTool tool;
	return tool;
}

InsertEdgeLoopPreview build_insert_edge_loop_preview(const std::vector<PolygonObject>& objects, int object_id, quader_poly::Edge edge, float factor)
{
	return build_preview(objects, object_id, edge, factor);
}

bool insert_edge_loop_tool_has_seed(const InsertEdgeLoopToolState& state)
{
	return has_seed(state);
}

void reset_insert_edge_loop_tool(InsertEdgeLoopToolState& state)
{
	reset(state);
}

void clear_insert_edge_loop_preview(
		InsertEdgeLoopToolState& state,
		SelectionHoverState& hover,
		int object_id,
		quader_poly::Edge edge,
		float factor,
		bool dragging)
{
	clear_preview(state, hover, object_id, edge, factor, dragging);
}

bool set_insert_edge_loop_preview(
		InsertEdgeLoopToolState& state,
		const std::vector<PolygonObject>& objects,
		SelectionHoverState& hover,
		int object_id,
		quader_poly::Edge edge,
		float factor,
		bool dragging)
{
	return set_preview(state, objects, hover, object_id, edge, factor, dragging);
}

bool update_insert_edge_loop_locked_seed(
		InsertEdgeLoopToolState& state,
		const std::vector<PolygonObject>& objects,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool dragging)
{
	return update_locked_seed(state, objects, hover, ray, grid_size, snap_enabled, dragging);
}

CommandResult EditorSession::insert_edge_loop(float factor)
{
	const CommandResult result = quader_editor_core::insert_edge_loop(state_.objects, factor);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::insert_edge_loop(int object_id, quader_poly::Edge edge, float factor)
{
	const CommandResult result = quader_editor_core::insert_edge_loop(state_.objects, object_id, edge, factor, state_.active_object_id, state_.selection_mode);
	note_content_result(result);
	return result;
}

bool EditorSession::set_insert_edge_loop_preview_from_pick(
		InsertEdgeLoopToolState& state,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		double grid_size,
		quader_poly::PickOptions pick_options,
		bool snap_enabled,
		bool dragging) const
{
	pick_options.mode = quader_poly::SelectionMode::edge;
	const std::optional<PickHitRecord> hit = pick_nearest({ ray, pick_options });
	if (!hit.has_value() || hit->kind != quader_poly::ElementKind::edge) {
		if (update_insert_edge_loop_locked_seed(state, state_.objects, hover, ray, grid_size, snap_enabled, dragging)) {
			return true;
		}
		state.active = true;
		return false;
	}

	const int object_index = object_index_by_id(state_.objects, hit->object_id);
	const std::optional<float> raw_factor = object_index < 0 ?
		std::optional<float> {} :
		quader_poly::edge_factor_from_position(state_.objects[object_index].document, hit->edge, hit->local_position);
	const std::optional<float> factor = object_index < 0 ?
		std::optional<float> { raw_factor.value_or(0.5F) } :
		snap_insert_edge_loop_preview_factor(state_.objects[object_index].document, hit->edge, raw_factor.value_or(0.5F), grid_size, snap_enabled);
	if (!factor.has_value()) {
		clear_insert_edge_loop_preview(state, hover, hit->object_id, hit->edge, raw_factor.value_or(0.5F), dragging);
		return false;
	}
	return set_insert_edge_loop_preview(state, state_.objects, hover, hit->object_id, hit->edge, *factor, dragging);
}

bool EditorSession::update_insert_edge_loop_drag_factor(
		InsertEdgeLoopToolState& state,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled) const
{
	const int object_index = object_index_by_id(state_.objects, state.object_id);
	if (object_index < 0) {
		state.preview_lines.clear();
		return false;
	}

	const std::optional<float> factor = quader_poly::edge_factor_from_ray(state_.objects[object_index].document, state.edge, ray);
	const std::optional<float> snapped_factor = snap_insert_edge_loop_preview_factor(
		state_.objects[object_index].document,
		state.edge,
		factor.value_or(state.factor),
		grid_size,
		snap_enabled);
	if (!snapped_factor.has_value()) {
		clear_insert_edge_loop_preview(state, hover, state.object_id, state.edge, factor.value_or(state.factor), true);
		return false;
	}
	return set_insert_edge_loop_preview(state, state_.objects, hover, state.object_id, state.edge, *snapped_factor, true);
}

} // namespace quader_editor_core
