////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.hpp"

#include <diagnostics/profile.hpp>
#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/interaction_api.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace quader_editor_core {

namespace {

void reset_preview_presentation_tools(
		BoxToolState& box,
		SelectionHoverState& hover,
		InsertEdgeLoopToolState& insert_edge_loop,
		KnifeToolState& knife,
		CutToolState& cut,
		MirrorToolState& mirror,
		PolyToolState& poly,
		TransformDragState& transform_drag)
{
	hover = {};
	reset_insert_edge_loop_tool(insert_edge_loop);
	reset_knife_tool(knife);
	reset_cut(cut);
	reset_mirror(mirror);
	reset_poly(poly);
	reset_transform_drag(transform_drag);
	reset_box(box);
}

} // namespace

void clear_editor_preview_presentation_state(
		BoxToolState& box,
		SelectionHoverState& hover,
		InsertEdgeLoopToolState& insert_edge_loop,
		KnifeToolState& knife,
		CutToolState& cut,
		MirrorToolState& mirror,
		PolyToolState& poly,
		TransformDragState& transform_drag)
{
	reset_preview_presentation_tools(box, hover, insert_edge_loop, knife, cut, mirror, poly, transform_drag);
}

void reset_editor_document_runtime_state(
		BoxToolState& box,
		SelectionHoverState& hover,
		InsertEdgeLoopToolState& insert_edge_loop,
		KnifeToolState& knife,
		CutToolState& cut,
		MirrorToolState& mirror,
		PolyToolState& poly,
		TransformDragState& transform_drag,
		double& box_grid_size)
{
	reset_preview_presentation_tools(box, hover, insert_edge_loop, knife, cut, mirror, poly, transform_drag);
	box_grid_size = 1.0;
}

bool discard_editor_transient_state(
		BoxToolState& box,
		SelectionHoverState& hover,
		InsertEdgeLoopToolState& insert_edge_loop,
		KnifeToolState& knife,
		CutToolState& cut,
		MirrorToolState& mirror,
		PolyToolState& poly,
		TransformDragState& transform_drag,
		double& box_grid_size)
{
	const bool changed = transform_drag.active ||
		box.drag_state != BoxDragState::Idle || hover.object_id > 0 ||
		insert_edge_loop.active || knife.active || cut_is_active(cut) ||
		mirror_is_active(mirror) || poly.active;
	reset_preview_presentation_tools(box, hover, insert_edge_loop, knife, cut, mirror, poly, transform_drag);
	box_grid_size = 1.0;
	return changed;
}

bool editor_runtime_box_active(const BoxToolState& box)
{
  return box.drag_state != BoxDragState::Idle || box.preview_valid ||
         !box.hover_cell_points.empty();
}

EditorRuntimeStatus editor_runtime_status(const EditorRuntimeState& runtime)
{
	const KnifeToolPreview knife = knife_tool_preview(runtime.knife);
	const PolyToolPreview poly = poly_tool_preview(runtime.poly);
	const MirrorPreviewMetadata mirror = mirror_preview_metadata(runtime.mirror);
        return {
            runtime.active_tool,
            runtime.gizmo_hover_axis,
            runtime.hover.object_id > 0 ||
                runtime.hover.candidate_object_id > 0,
            runtime.hover.candidate_object_id > 0,
            editor_runtime_box_active(runtime.box),
            runtime.box_grid_size,
            runtime.box_grid_size == kDefaultEditorRuntimeBoxGridSize,
            runtime.insert_edge_loop.active,
            runtime.insert_edge_loop.dragging,
            !runtime.insert_edge_loop.preview_lines.empty(),
            knife.active,
            knife.has_active_stroke,
            knife.has_current,
            knife.can_commit,
            cut_is_active(runtime.cut) ||
                runtime.active_tool == ViewportInteractionTool::Cut,
            cut_can_preview(runtime.cut),
            cut_preview_metadata(runtime.cut).preview_valid,
            cut_preview_metadata(runtime.cut).point_count,
            mirror_is_active(runtime.mirror) ||
                runtime.active_tool == ViewportInteractionTool::Mirror,
            mirror.can_preview,
            mirror.preview_valid,
            mirror.point_count,
            poly.active || runtime.active_tool == ViewportInteractionTool::Poly,
            poly.closed,
            poly.has_current,
            poly.finalizable,
            static_cast<int>(poly.point_count),
            runtime.transform_drag.active,
        };
}

ViewportToolActivationPlan plan_editor_runtime_tool_activation(
		const EditorRuntimeState& runtime,
		const EditorRuntimeToolActivationRequest& request)
{
	ViewportToolActivationRequest policy_request;
	policy_request.current_tool = runtime.active_tool;
	policy_request.requested_tool = request.requested_tool;
	policy_request.selection_mode = request.selection_mode;
	policy_request.transform_drag_active = runtime.transform_drag.active && !request.transform_preview_cleared;
	policy_request.transform_preview_active = request.transform_preview_active && !request.transform_preview_cleared;
	policy_request.hover_has_object = runtime.hover.object_id > 0;
	policy_request.hover_has_candidate_object = runtime.hover.candidate_object_id > 0;
	policy_request.selected_object_count = request.selected_object_count;
	policy_request.selected_cut_target_object_count = request.selected_cut_target_object_count;
	return plan_viewport_tool_activation(policy_request);
}

EditorRuntimeToolActivationRequest editor_runtime_tool_activation_request(
		const EditorSession& session,
		const EditorRuntimeState&,
		ViewportInteractionTool requested_tool)
{
	const std::size_t selected_object_count = session.selected_object_ids().size();
        return {
            requested_tool,
            session.selection_mode(),
            selected_object_count,
            selected_object_count,
            false,
            false,
        };
}

void clear_editor_runtime_hover(EditorRuntimeState& runtime)
{
	runtime.hover = {};
}

void set_editor_runtime_hover(EditorRuntimeState& runtime, SelectionHoverState hover)
{
	runtime.hover = std::move(hover);
}

void apply_editor_runtime_hover_update(EditorRuntimeState& runtime, const SelectionHoverUpdateResult& result)
{
	runtime.hover = result.hover;
}

void promote_editor_runtime_hover_object_to_candidate(EditorRuntimeState& runtime)
{
	if (runtime.hover.object_id > 0 && runtime.hover.candidate_object_id <= 0) {
		runtime.hover.candidate_object_id = runtime.hover.object_id;
	}
}

ViewportInteractionTool editor_runtime_active_tool(const EditorRuntimeState& runtime)
{
	return runtime.active_tool;
}

TransformAxis editor_runtime_gizmo_hover_axis(const EditorRuntimeState& runtime)
{
	return runtime.gizmo_hover_axis;
}

const SelectionHoverState& editor_runtime_hover(const EditorRuntimeState& runtime)
{
	return runtime.hover;
}

const BoxToolState& editor_runtime_box_state(const EditorRuntimeState& runtime)
{
	return runtime.box;
}

double editor_runtime_box_grid_size(const EditorRuntimeState& runtime)
{
	return runtime.box_grid_size;
}

void set_editor_runtime_box_grid_size(EditorRuntimeState& runtime, double grid_size)
{
	runtime.box_grid_size = grid_size;
}

void reset_editor_runtime_box_grid_size(EditorRuntimeState& runtime)
{
  runtime.box_grid_size = kDefaultEditorRuntimeBoxGridSize;
}

void reset_editor_runtime_box_state(EditorRuntimeState& runtime)
{
	reset_box(runtime.box);
	reset_editor_runtime_box_grid_size(runtime);
}

void set_editor_runtime_box_state(EditorRuntimeState& runtime, BoxToolState box)
{
	runtime.box = std::move(box);
}

void clear_editor_runtime_gizmo_hover(EditorRuntimeState& runtime)
{
  runtime.gizmo_hover_axis = TransformAxis::None;
}

void set_editor_runtime_gizmo_hover_axis(EditorRuntimeState& runtime, TransformAxis axis)
{
	runtime.gizmo_hover_axis = axis;
}

void set_editor_runtime_gizmo_hover_from_pick(EditorRuntimeState& runtime, const TransformGizmoPickHit& hit)
{
  runtime.gizmo_hover_axis = hit.hit ? hit.axis : TransformAxis::None;
}

void clear_editor_runtime_preview_presentation_state(EditorRuntimeState& runtime)
{
	clear_editor_preview_presentation_state(
		runtime.box,
		runtime.hover,
		runtime.insert_edge_loop,
		runtime.knife,
		runtime.cut,
		runtime.mirror,
		runtime.poly,
		runtime.transform_drag);
	runtime.cut_hover = {};
	runtime.mirror_hover = {};
	clear_editor_runtime_gizmo_hover(runtime);
}

void clear_editor_runtime_selection_presentation_state(EditorRuntimeState& runtime)
{
	clear_editor_runtime_hover(runtime);
	clear_editor_runtime_gizmo_hover(runtime);
	clear_editor_runtime_transform_drag_state(runtime);
}

void reset_editor_runtime_document_state(EditorRuntimeState& runtime)
{
	reset_editor_document_runtime_state(
		runtime.box,
		runtime.hover,
		runtime.insert_edge_loop,
		runtime.knife,
		runtime.cut,
		runtime.mirror,
		runtime.poly,
		runtime.transform_drag,
		runtime.box_grid_size);
	runtime.cut_hover = {};
	runtime.mirror_hover = {};
        runtime.active_tool = ViewportInteractionTool::Select;
        clear_editor_runtime_gizmo_hover(runtime);
}

bool discard_editor_runtime_transient_state(
		EditorRuntimeState& runtime)
{
	const bool changed = discard_editor_transient_state(
		runtime.box,
		runtime.hover,
		runtime.insert_edge_loop,
		runtime.knife,
		runtime.cut,
		runtime.mirror,
		runtime.poly,
		runtime.transform_drag,
		runtime.box_grid_size);
	runtime.cut_hover = {};
	runtime.mirror_hover = {};
	clear_editor_runtime_gizmo_hover(runtime);
	return changed;
}

bool cancel_editor_runtime_transform_preview_state(EditorRuntimeState& runtime)
{
  const bool changed = runtime.transform_drag.active;
  reset_transform_drag(runtime.transform_drag);
  clear_editor_runtime_gizmo_hover(runtime);
  return changed;
}

void set_editor_runtime_transform_drag_state(EditorRuntimeState& runtime, TransformDragState state)
{
	runtime.transform_drag = std::move(state);
}

void begin_editor_runtime_transform_drag_state(
		EditorRuntimeState& runtime,
		ViewportInteractionTool tool,
		TransformDragOperation operation,
		TransformAxis axis,
		quader::QVec3 pivot)
{
	reset_transform_drag(runtime.transform_drag);
	runtime.transform_drag.active = true;
	runtime.transform_drag.tool = sanitize_viewport_interaction_tool(tool);
	runtime.transform_drag.operation = operation;
	runtime.transform_drag.axis = axis;
	runtime.transform_drag.pivot = pivot;
}

const TransformDragState& editor_runtime_transform_drag_state(const EditorRuntimeState& runtime)
{
	return runtime.transform_drag;
}

bool editor_runtime_transform_drag_active(const EditorRuntimeState& runtime)
{
	return runtime.transform_drag.active;
}

void set_editor_runtime_transform_drag_move_state(EditorRuntimeState& runtime, MoveToolState state)
{
	runtime.transform_drag.move = state;
}

void set_editor_runtime_transform_drag_rotate_state(EditorRuntimeState& runtime, RotateToolState state)
{
	runtime.transform_drag.rotate = state;
}

void set_editor_runtime_transform_drag_scale_state(EditorRuntimeState& runtime, ScaleToolState state)
{
	runtime.transform_drag.scale = state;
}

CommandResult apply_editor_runtime_transform_drag_preview(
		const EditorSession& session,
		EditorRuntimeState& runtime,
		const TransformDragPreviewInput& input)
{
	return quader_editor_core::apply_transform_drag_preview(session, runtime.transform_drag, input);
}

void clear_editor_runtime_transform_drag_state(EditorRuntimeState& runtime)
{
	reset_transform_drag(runtime.transform_drag);
}

const InsertEdgeLoopToolState& editor_runtime_insert_edge_loop_state(const EditorRuntimeState& runtime)
{
	return runtime.insert_edge_loop;
}

void reset_editor_runtime_insert_edge_loop_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_insert_edge_loop)
{
  const bool keep_active =
      keep_active_when_current_tool_is_insert_edge_loop &&
      runtime.active_tool == ViewportInteractionTool::InsertEdgeLoop;
  reset_insert_edge_loop_tool(runtime.insert_edge_loop);
  runtime.insert_edge_loop.active = keep_active;
}

void set_editor_runtime_insert_edge_loop_state(EditorRuntimeState& runtime, InsertEdgeLoopToolState state)
{
	runtime.insert_edge_loop = std::move(state);
}

void set_editor_runtime_insert_edge_loop_active(EditorRuntimeState& runtime, bool active)
{
	runtime.insert_edge_loop.active = active;
}

void set_editor_runtime_insert_edge_loop_dragging(EditorRuntimeState& runtime, bool dragging)
{
	runtime.insert_edge_loop.dragging = dragging;
}

const KnifeToolState& editor_runtime_knife_state(const EditorRuntimeState& runtime)
{
	return runtime.knife;
}

void reset_editor_runtime_knife_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_cut)
{
  const bool keep_active =
      keep_active_when_current_tool_is_cut &&
      runtime.active_tool == ViewportInteractionTool::Knife;
  reset_knife_tool(runtime.knife);
  runtime.knife.active = keep_active;
}

void set_editor_runtime_knife_active(EditorRuntimeState& runtime, bool active)
{
	runtime.knife.active = active;
}

void set_editor_runtime_knife_state(EditorRuntimeState& runtime, KnifeToolState state)
{
	runtime.knife = std::move(state);
}

const CutToolState& editor_runtime_cut_state(const EditorRuntimeState& runtime)
{
	return runtime.cut;
}

const CutHoverState& editor_runtime_cut_hover(const EditorRuntimeState& runtime)
{
	return runtime.cut_hover;
}

void reset_editor_runtime_cut_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_cut)
{
  const bool keep_active = keep_active_when_current_tool_is_cut &&
                           runtime.active_tool == ViewportInteractionTool::Cut;
  reset_cut(runtime.cut);
  runtime.cut_hover = {};
  if (keep_active) {
    runtime.cut.preview_valid = false;
  }
}

void set_editor_runtime_cut_state(EditorRuntimeState& runtime, CutToolState state)
{
	runtime.cut = std::move(state);
}

void set_editor_runtime_cut_hover(EditorRuntimeState& runtime, CutHoverState hover)
{
	runtime.cut_hover = hover;
}

void clear_editor_runtime_cut_hover(EditorRuntimeState& runtime)
{
	runtime.cut_hover = {};
}

const MirrorToolState& editor_runtime_mirror_state(const EditorRuntimeState& runtime)
{
	return runtime.mirror;
}

const MirrorHoverState& editor_runtime_mirror_hover(const EditorRuntimeState& runtime)
{
	return runtime.mirror_hover;
}

void reset_editor_runtime_mirror_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_mirror)
{
  const bool keep_active =
      keep_active_when_current_tool_is_mirror &&
      runtime.active_tool == ViewportInteractionTool::Mirror;
  reset_mirror(runtime.mirror);
  runtime.mirror_hover = {};
  if (keep_active) {
    runtime.mirror.preview_valid = false;
  }
}

void set_editor_runtime_mirror_state(EditorRuntimeState& runtime, MirrorToolState state)
{
	runtime.mirror = std::move(state);
}

void set_editor_runtime_mirror_hover(EditorRuntimeState& runtime, MirrorHoverState hover)
{
	runtime.mirror_hover = hover;
}

void clear_editor_runtime_mirror_hover(EditorRuntimeState& runtime)
{
	runtime.mirror_hover = {};
}

const PolyToolState& editor_runtime_poly_state(const EditorRuntimeState& runtime)
{
	return runtime.poly;
}

void reset_editor_runtime_poly_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_poly)
{
  const bool keep_active = keep_active_when_current_tool_is_poly &&
                           runtime.active_tool == ViewportInteractionTool::Poly;
  reset_poly(runtime.poly);
  runtime.poly.active = keep_active;
}

void set_editor_runtime_poly_state(EditorRuntimeState& runtime, PolyToolState state)
{
	runtime.poly = std::move(state);
}

void set_editor_runtime_poly_active(EditorRuntimeState& runtime, bool active)
{
	runtime.poly.active = active;
}

void apply_editor_runtime_tool_activation_plan(
		EditorRuntimeState& runtime,
		const ViewportToolActivationPlan& plan)
{
	if (plan.reset_transform_drag) {
		reset_transform_drag(runtime.transform_drag);
	}
	runtime.active_tool = plan.active_tool;
	if (plan.clear_gizmo_hover) {
		clear_editor_runtime_gizmo_hover(runtime);
	}
	if (plan.reset_insert_edge_loop_tool) {
		reset_insert_edge_loop_tool(runtime.insert_edge_loop);
	}
	if (plan.reset_knife_tool) {
		reset_knife_tool(runtime.knife);
	}
	if (plan.reset_cut_tool) {
		reset_cut(runtime.cut);
		runtime.cut_hover = {};
	}
	if (plan.reset_mirror_tool) {
		reset_mirror(runtime.mirror);
		runtime.mirror_hover = {};
	}
	if (plan.reset_poly_tool) {
		reset_poly(runtime.poly);
	}
	runtime.insert_edge_loop.active = plan.insert_edge_loop_tool_active;
	runtime.knife.active = plan.knife_tool_active;
        if (!plan.mirror_tool_active &&
            plan.active_tool != ViewportInteractionTool::Mirror) {
          runtime.mirror_hover = {};
        }
        runtime.poly.active = plan.poly_tool_active;
	if (plan.promote_hover_object_to_candidate) {
		runtime.hover.candidate_object_id = runtime.hover.object_id;
	}
	if (plan.reset_box_tool) {
		reset_box(runtime.box);
	}
	if (plan.reset_box_grid_size) {
		reset_editor_runtime_box_grid_size(runtime);
	}
}

EditorRuntimeToolActivationResult activate_editor_runtime_tool(
		EditorRuntimeState& runtime,
		const EditorRuntimeToolActivationRequest& request)
{
	EditorRuntimeToolActivationResult result;
	result.plan = plan_editor_runtime_tool_activation(runtime, request);
	if (result.plan.activation_blocked) {
		result.status = editor_runtime_status(runtime);
		return result;
	}
	if (result.plan.cancel_transform_preview && !request.transform_preview_cleared) {
		result.requires_transform_preview_cancel = true;
		result.status = editor_runtime_status(runtime);
		return result;
	}

	apply_editor_runtime_tool_activation_plan(runtime, result.plan);
	result.applied = true;
	result.status = editor_runtime_status(runtime);
	return result;
}

EditorRuntimeToolActivationResult set_editor_runtime_active_tool(
		const EditorSession& session,
		EditorRuntimeState& runtime,
		ViewportInteractionTool requested_tool)
{
	EditorRuntimeToolActivationRequest request =
		editor_runtime_tool_activation_request(session, runtime, requested_tool);
	EditorRuntimeToolActivationResult result = activate_editor_runtime_tool(runtime, request);
	if (result.requires_transform_preview_cancel) {
		const bool preview_cancelled =
			cancel_editor_runtime_transform_preview_state(runtime);
		request.transform_preview_cleared = true;
		result = activate_editor_runtime_tool(runtime, request);
		result.transform_preview_cancelled = preview_cancelled;
	}
	return result;
}

EditorRuntime::EditorRuntime(EditorRuntimeState state) : state_(std::move(state)) {}

const EditorRuntimeState& EditorRuntime::state() const
{
	return state_;
}

EditorRuntimeState& EditorRuntime::state()
{
	return state_;
}

void EditorRuntime::set_state(EditorRuntimeState state)
{
	state_ = std::move(state);
}

EditorRuntimeStatus EditorRuntime::status() const
{
	return editor_runtime_status(state_);
}

ViewportInteractionTool EditorRuntime::active_tool() const
{
	return editor_runtime_active_tool(state_);
}

TransformAxis EditorRuntime::gizmo_hover_axis() const
{
	return editor_runtime_gizmo_hover_axis(state_);
}

const SelectionHoverState& EditorRuntime::hover() const
{
	return editor_runtime_hover(state_);
}

const BoxToolState& EditorRuntime::box_state() const
{
	return editor_runtime_box_state(state_);
}

bool EditorRuntime::box_active() const
{
	return editor_runtime_box_active(state_.box);
}

double EditorRuntime::box_grid_size() const
{
	return editor_runtime_box_grid_size(state_);
}

const InsertEdgeLoopToolState& EditorRuntime::insert_edge_loop_state() const
{
	return editor_runtime_insert_edge_loop_state(state_);
}

const KnifeToolState& EditorRuntime::knife_state() const
{
	return editor_runtime_knife_state(state_);
}

const CutToolState& EditorRuntime::cut_state() const
{
	return editor_runtime_cut_state(state_);
}

const CutHoverState& EditorRuntime::cut_hover() const
{
	return editor_runtime_cut_hover(state_);
}

const MirrorToolState& EditorRuntime::mirror_state() const
{
	return editor_runtime_mirror_state(state_);
}

const MirrorHoverState& EditorRuntime::mirror_hover() const
{
	return editor_runtime_mirror_hover(state_);
}

const PolyToolState& EditorRuntime::poly_state() const
{
	return editor_runtime_poly_state(state_);
}

const TransformDragState& EditorRuntime::transform_drag_state() const
{
	return editor_runtime_transform_drag_state(state_);
}

bool EditorRuntime::transform_drag_active() const
{
	return editor_runtime_transform_drag_active(state_);
}

void EditorRuntime::set_hover(SelectionHoverState hover)
{
	set_editor_runtime_hover(state_, std::move(hover));
}

void EditorRuntime::apply_hover_update(const SelectionHoverUpdateResult& result)
{
	apply_editor_runtime_hover_update(state_, result);
}

void EditorRuntime::promote_hover_object_to_candidate()
{
	promote_editor_runtime_hover_object_to_candidate(state_);
}

void EditorRuntime::clear_hover()
{
	clear_editor_runtime_hover(state_);
}

void EditorRuntime::set_box_grid_size(double grid_size)
{
	set_editor_runtime_box_grid_size(state_, grid_size);
}

void EditorRuntime::reset_box_state()
{
	reset_editor_runtime_box_state(state_);
}

void EditorRuntime::set_box_state(BoxToolState box)
{
	set_editor_runtime_box_state(state_, std::move(box));
}

BoxToolStateResult EditorRuntime::set_box_construction_plane(
		quader::QVec3 origin,
		quader::QVec3 normal)
{
	return quader_editor_core::set_box_construction_plane(state_.box, origin, normal);
}

BoxConstructionPointResult EditorRuntime::box_construction_point_from_ray(const quader_poly::Ray& ray) const
{
	return quader_editor_core::box_construction_point_from_ray(state_.box.plane, ray);
}

BoxToolStateResult EditorRuntime::begin_box_footprint(
		quader::QVec3 raw_start,
		quader::QVec3 raw_end,
		bool grid_enabled)
{
	return quader_editor_core::begin_box_footprint(
		state_.box,
		raw_start,
		raw_end,
		grid_enabled,
		state_.box_grid_size);
}

BoxToolStateResult EditorRuntime::update_box_footprint(
		quader::QVec3 raw_end,
		bool grid_enabled)
{
	return quader_editor_core::update_box_footprint(
		state_.box,
		raw_end,
		grid_enabled,
		state_.box_grid_size);
}

BoxToolStateResult EditorRuntime::update_box_hover_cell(
		quader::QVec3 local_point,
		bool grid_enabled)
{
	return quader_editor_core::update_box_hover_cell(state_.box, local_point, grid_enabled, state_.box_grid_size);
}

double EditorRuntime::box_height_distance_for_ray(const quader_poly::Ray& ray) const
{
	return quader_editor_core::box_height_distance_for_ray(state_.box, ray, state_.box_grid_size);
}

BoxToolStateResult EditorRuntime::begin_box_height(
		quader::QVec2 screen_position,
		double axis_distance,
		bool has_axis_distance,
		bool grid_enabled)
{
	return quader_editor_core::begin_box_height(
		state_.box,
		screen_position,
		axis_distance,
		has_axis_distance,
		grid_enabled,
		state_.box_grid_size);
}

BoxToolStateResult EditorRuntime::update_box_height_preview(
		double axis_distance,
		bool has_axis_distance,
		quader::QVec2 screen_position,
		bool grid_enabled)
{
	return quader_editor_core::update_box_height_preview(
		state_.box,
		axis_distance,
		has_axis_distance,
		screen_position,
		grid_enabled,
		state_.box_grid_size);
}

BoxPreviewDescriptor EditorRuntime::box_preview_descriptor(bool grid_enabled) const
{
	return quader_editor_core::box_preview_descriptor(state_.box, grid_enabled, state_.box_grid_size);
}

void EditorRuntime::clear_preview_presentation_state()
{
	clear_editor_runtime_preview_presentation_state(state_);
}

void EditorRuntime::clear_selection_presentation_state()
{
	clear_editor_runtime_selection_presentation_state(state_);
}

void EditorRuntime::reset_document_state()
{
	reset_editor_runtime_document_state(state_);
}

bool EditorRuntime::discard_transient_state()
{
	return discard_editor_runtime_transient_state(state_);
}

void EditorRuntime::set_transform_drag_state(TransformDragState state)
{
	set_editor_runtime_transform_drag_state(state_, std::move(state));
}

void EditorRuntime::begin_transform_drag_state(
		ViewportInteractionTool tool,
		TransformDragOperation operation,
		TransformAxis axis,
		quader::QVec3 pivot)
{
	begin_editor_runtime_transform_drag_state(state_, tool, operation, axis, pivot);
}

void EditorRuntime::set_transform_drag_move_state(MoveToolState state)
{
	set_editor_runtime_transform_drag_move_state(state_, state);
}

void EditorRuntime::set_transform_drag_rotate_state(RotateToolState state)
{
	set_editor_runtime_transform_drag_rotate_state(state_, state);
}

void EditorRuntime::set_transform_drag_scale_state(ScaleToolState state)
{
	set_editor_runtime_transform_drag_scale_state(state_, state);
}

CommandResult EditorRuntime::apply_transform_drag_preview(
		const EditorSession& session,
		const TransformDragPreviewInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.EditorRuntime.apply_transform_drag_preview");
	return apply_editor_runtime_transform_drag_preview(session, state_, input);
}

void EditorRuntime::clear_transform_drag_state()
{
	clear_editor_runtime_transform_drag_state(state_);
}

EditorRuntimeToolActivationResult EditorRuntime::activate_tool(const EditorRuntimeToolActivationRequest& request)
{
	return activate_editor_runtime_tool(state_, request);
}

EditorRuntimeToolActivationResult EditorRuntime::set_active_tool(const EditorSession& session, ViewportInteractionTool requested_tool)
{
	return set_editor_runtime_active_tool(session, state_, requested_tool);
}

void EditorRuntime::reset_insert_edge_loop_state(bool keep_active_when_current_tool_is_insert_edge_loop)
{
	reset_editor_runtime_insert_edge_loop_state(state_, keep_active_when_current_tool_is_insert_edge_loop);
}

void EditorRuntime::set_insert_edge_loop_state(InsertEdgeLoopToolState state)
{
	set_editor_runtime_insert_edge_loop_state(state_, std::move(state));
}

void EditorRuntime::set_insert_edge_loop_active(bool active)
{
	set_editor_runtime_insert_edge_loop_active(state_, active);
}

void EditorRuntime::set_insert_edge_loop_dragging(bool dragging)
{
	set_editor_runtime_insert_edge_loop_dragging(state_, dragging);
}

bool EditorRuntime::set_insert_edge_loop_preview_from_pick(
		EditorSession& session,
		const quader_poly::Ray& ray,
		double grid_size,
		double vertex_radius,
		double edge_radius,
		bool snap_enabled,
		bool dragging,
		int target_object_id)
{
	quader_poly::PickOptions pick_options =
		selection_pick_options_from_radii(vertex_radius, edge_radius);
	return session.set_insert_edge_loop_preview_from_pick(
		state_.insert_edge_loop,
		state_.hover,
		ray,
		grid_size,
		pick_options,
		snap_enabled,
		dragging,
		target_object_id);
}

bool EditorRuntime::update_insert_edge_loop_drag_factor(
		EditorSession& session,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled)
{
	return session.update_insert_edge_loop_drag_factor(
		state_.insert_edge_loop,
		state_.hover,
		ray,
		grid_size,
		snap_enabled);
}

void EditorRuntime::reset_knife_state(bool keep_active_when_current_tool_is_knife)
{
	reset_editor_runtime_knife_state(state_, keep_active_when_current_tool_is_knife);
}

void EditorRuntime::set_knife_state(KnifeToolState state)
{
	set_editor_runtime_knife_state(state_, std::move(state));
}

void EditorRuntime::set_knife_active(bool active)
{
	set_editor_runtime_knife_active(state_, active);
}

void EditorRuntime::clear_knife_hover()
{
	quader_editor_core::clear_knife_hover(state_.knife, state_.hover);
}

bool EditorRuntime::update_knife_preview_from_pick(
		EditorSession& session,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		int target_object_id)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.EditorRuntime.update_knife_preview_from_pick");
	return quader_editor_core::update_knife_preview_from_pick(
		session,
		state_.knife,
		state_.hover,
		ray,
		grid_size,
		snap_enabled,
		constrain_axis,
		target_object_id);
}

bool EditorRuntime::update_insert_edge_loop_drag_factor_from_world(
		EditorSession& session,
		std::optional<float> resolved_factor)
{
	if (!resolved_factor.has_value()) {
		clear_insert_edge_loop_preview(
			state_.insert_edge_loop, state_.hover,
			state_.insert_edge_loop.object_id, state_.insert_edge_loop.edge,
			state_.insert_edge_loop.factor, true);
		return false;
	}
	return set_insert_edge_loop_preview(
		state_.insert_edge_loop, session.objects(), state_.hover,
		state_.insert_edge_loop.object_id, state_.insert_edge_loop.edge,
		*resolved_factor, true);
}

bool EditorRuntime::update_knife_preview_from_hit(
		EditorSession& session,
		const PickHitRecord& hit,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.EditorRuntime.update_knife_preview_from_hit");
	return quader_editor_core::update_knife_preview_from_hit(
		session, state_.knife, state_.hover, hit, grid_size,
		snap_enabled, constrain_axis);
}

bool EditorRuntime::set_insert_edge_loop_preview_from_hit(
		EditorSession& session,
		const PickHitRecord& hit,
		std::optional<float> resolved_factor,
		bool dragging)
{
	return session.set_insert_edge_loop_preview_from_hit(
		state_.insert_edge_loop, state_.hover, hit, resolved_factor, dragging);
}

bool EditorRuntime::accept_knife_current_point(double grid_size)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.EditorRuntime.accept_knife_current_point");
	return quader_editor_core::accept_knife_current_point(state_.knife, grid_size);
}

void EditorRuntime::reset_cut_state(bool keep_active_when_current_tool_is_cut)
{
	reset_editor_runtime_cut_state(state_, keep_active_when_current_tool_is_cut);
}

void EditorRuntime::set_cut_state(CutToolState state)
{
	set_editor_runtime_cut_state(state_, std::move(state));
}

void EditorRuntime::set_cut_hover(CutHoverState hover)
{
	set_editor_runtime_cut_hover(state_, hover);
}

void EditorRuntime::clear_cut_hover()
{
	clear_editor_runtime_cut_hover(state_);
}

CutToolResult EditorRuntime::refresh_cut_preview_from_selection(
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects)
{
	return quader_editor_core::refresh_cut_preview_from_selection(state_.cut, selection, objects);
}

CutToolResult EditorRuntime::update_cut_hover_from_ray(
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const CutRayUpdateInput& input)
{
	return quader_editor_core::update_cut_hover_from_ray(state_, selection, objects, input);
}

CutToolResult EditorRuntime::click_cut_from_ray(
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const CutRayUpdateInput& input)
{
	return quader_editor_core::click_cut_from_ray(state_, selection, objects, input);
}

CutToolResult EditorRuntime::begin_cut_point_drag(int point_index)
{
	return quader_editor_core::begin_cut_point_drag(state_.cut, point_index);
}

CutToolResult EditorRuntime::begin_cut_line_drag(const CutRayUpdateInput& input)
{
	return quader_editor_core::begin_cut_line_drag_from_ray(state_, input);
}

CutToolResult EditorRuntime::end_cut_drag()
{
	if (state_.cut.dragging_new_point) {
		return quader_editor_core::end_new_cut_point_drag(state_.cut);
	}
	if (state_.cut.dragging_point_index >= 0) {
		return quader_editor_core::end_cut_point_drag(state_.cut);
	}
	if (state_.cut.dragging_line) {
		return quader_editor_core::end_cut_line_drag(state_.cut);
	}
	return { true, false };
}

CutToolResult EditorRuntime::set_cut_mode(CutMode mode)
{
	return quader_editor_core::set_cut_mode(state_.cut, mode);
}

void EditorRuntime::reset_mirror_state(bool keep_active_when_current_tool_is_mirror)
{
	reset_editor_runtime_mirror_state(state_, keep_active_when_current_tool_is_mirror);
}

void EditorRuntime::set_mirror_state(MirrorToolState state)
{
	set_editor_runtime_mirror_state(state_, std::move(state));
}

void EditorRuntime::set_mirror_hover(MirrorHoverState hover)
{
	set_editor_runtime_mirror_hover(state_, hover);
}

void EditorRuntime::clear_mirror_hover()
{
	clear_editor_runtime_mirror_hover(state_);
}

MirrorToolResult EditorRuntime::refresh_mirror_preview_from_selection(
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects)
{
	return quader_editor_core::refresh_mirror_preview_from_selection(state_.mirror, selection, objects);
}

MirrorToolResult EditorRuntime::update_mirror_hover_from_ray(
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const MirrorRayUpdateInput& input)
{
	return quader_editor_core::update_mirror_hover_from_ray(state_, selection, objects, input);
}

MirrorToolResult EditorRuntime::click_mirror_from_ray(
		const RectangleSelectionState& selection,
		std::span<const PolygonObject> objects,
		const MirrorRayUpdateInput& input)
{
	return quader_editor_core::click_mirror_from_ray(state_, selection, objects, input);
}

MirrorToolResult EditorRuntime::begin_mirror_point_drag(int point_index)
{
	return quader_editor_core::begin_mirror_point_drag(state_.mirror, point_index);
}

MirrorToolResult EditorRuntime::begin_mirror_line_drag(const MirrorRayUpdateInput& input)
{
	return quader_editor_core::begin_mirror_line_drag_from_ray(state_, input);
}

MirrorToolResult EditorRuntime::end_mirror_drag()
{
	if (state_.mirror.plane.dragging_new_point) {
		return quader_editor_core::end_new_mirror_point_drag(state_.mirror);
	}
	if (state_.mirror.plane.dragging_point_index >= 0) {
		return quader_editor_core::end_mirror_point_drag(state_.mirror);
	}
	if (state_.mirror.plane.dragging_line) {
		return quader_editor_core::end_mirror_line_drag(state_.mirror);
	}
	return { true, false };
}

void EditorRuntime::reset_poly_state(bool keep_active_when_current_tool_is_poly)
{
	reset_editor_runtime_poly_state(state_, keep_active_when_current_tool_is_poly);
}

void EditorRuntime::set_poly_state(PolyToolState state)
{
	set_editor_runtime_poly_state(state_, std::move(state));
}

void EditorRuntime::set_poly_active(bool active)
{
	set_editor_runtime_poly_active(state_, active);
}

PolyToolResult EditorRuntime::update_poly_hover(
		quader::QVec3 position,
		quader::QVec3 plane_normal,
		bool constrain_axis,
		double grid_size,
		bool snap_enabled)
{
	return quader_editor_core::update_poly_hover(
		state_.poly,
		position,
		plane_normal,
		constrain_axis,
		grid_size,
		snap_enabled);
}

PolyToolResult EditorRuntime::accept_poly_current_point()
{
	return quader_editor_core::accept_poly_current_point(state_.poly);
}

void EditorRuntime::set_gizmo_hover_axis(TransformAxis axis)
{
	set_editor_runtime_gizmo_hover_axis(state_, axis);
}

void EditorRuntime::set_gizmo_hover_from_pick(const TransformGizmoPickHit& hit)
{
	set_editor_runtime_gizmo_hover_from_pick(state_, hit);
}

void EditorRuntime::clear_gizmo_hover()
{
	clear_editor_runtime_gizmo_hover(state_);
}

} // namespace quader_editor_core
