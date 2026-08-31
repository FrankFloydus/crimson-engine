////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/box_tool.hpp>
#include <editor/interactions/cut_tool.hpp>
#include <editor/interactions/mirror_tool.hpp>
#include <editor/interactions/poly_tool.hpp>
#include <editor/interactions/transform_drag.hpp>
#include <editor/interactions/transform_gizmo.hpp>
#include <editor/modeling/runtime/tool_runtime_policy.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace quader_editor_core {

inline constexpr double kDefaultEditorRuntimeBoxGridSize = 1.0;

/**
 * Stores the Editor Runtime State data contract used by the modeling runtime domain.
 */
struct EditorRuntimeState {
  ViewportInteractionTool active_tool = ViewportInteractionTool::Select;
  SelectionHoverState hover;
  BoxToolState box;
  double box_grid_size = kDefaultEditorRuntimeBoxGridSize;
  InsertEdgeLoopToolState insert_edge_loop;
  KnifeToolState knife;
  CutToolState cut;
  CutHoverState cut_hover;
  MirrorToolState mirror;
  MirrorHoverState mirror_hover;
  PolyToolState poly;
  TransformDragState transform_drag;
  TransformAxis gizmo_hover_axis = TransformAxis::None;
};

/**
 * Represents an Editor Runtime Status value used by the modeling runtime domain.
 */
struct EditorRuntimeStatus {
  ViewportInteractionTool active_tool = ViewportInteractionTool::Select;
  TransformAxis gizmo_hover_axis = TransformAxis::None;
  bool hover_active = false;
  bool hover_has_candidate_object = false;
  bool box_active = false;
  double box_grid_size = kDefaultEditorRuntimeBoxGridSize;
  bool box_grid_at_default = true;
  bool insert_edge_loop_active = false;
  bool insert_edge_loop_dragging = false;
  bool insert_edge_loop_has_preview = false;
  bool knife_active = false;
  bool knife_has_active_stroke = false;
  bool knife_has_current = false;
  bool knife_can_commit = false;
  bool cut_active = false;
  bool cut_can_preview = false;
  bool cut_preview_valid = false;
  int cut_point_count = 0;
  bool mirror_active = false;
  bool mirror_can_preview = false;
  bool mirror_preview_valid = false;
  int mirror_point_count = 0;
  bool poly_active = false;
  bool poly_closed = false;
  bool poly_has_current = false;
  bool poly_can_commit = false;
  int poly_point_count = 0;
  bool transform_drag_active = false;
};

/**
 * Stores the Editor Runtime Tool Activation Request data contract used by the modeling runtime domain.
 */
struct EditorRuntimeToolActivationRequest {
  ViewportInteractionTool requested_tool = ViewportInteractionTool::Select;
  SelectionMode selection_mode = SelectionMode::Object;
  std::size_t selected_object_count = 0;
  std::size_t selected_cut_target_object_count = 0;
  bool transform_preview_active = false;
  bool transform_preview_cleared = false;
};

/**
 * Stores the Editor Runtime Tool Activation Result data contract used by the modeling runtime domain.
 */
struct EditorRuntimeToolActivationResult {
	ViewportToolActivationPlan plan;
	EditorRuntimeStatus status;
	bool applied = false;
	bool requires_transform_preview_cancel = false;
	bool transform_preview_cancelled = false;
	bool selection_mode_applied = false;
};

bool editor_runtime_box_active(const BoxToolState& box);
EditorRuntimeStatus editor_runtime_status(const EditorRuntimeState& runtime);
ViewportToolActivationPlan plan_editor_runtime_tool_activation(
		const EditorRuntimeState& runtime,
		const EditorRuntimeToolActivationRequest& request);
EditorRuntimeToolActivationRequest editor_runtime_tool_activation_request(
		const EditorSession& session,
		const EditorRuntimeState& runtime,
		ViewportInteractionTool requested_tool);
void clear_editor_runtime_hover(EditorRuntimeState& runtime);
void set_editor_runtime_hover(EditorRuntimeState& runtime, SelectionHoverState hover);
void apply_editor_runtime_hover_update(EditorRuntimeState& runtime, const SelectionHoverUpdateResult& result);
void promote_editor_runtime_hover_object_to_candidate(EditorRuntimeState& runtime);
ViewportInteractionTool editor_runtime_active_tool(const EditorRuntimeState& runtime);
TransformAxis editor_runtime_gizmo_hover_axis(const EditorRuntimeState& runtime);
const SelectionHoverState& editor_runtime_hover(const EditorRuntimeState& runtime);
const BoxToolState& editor_runtime_box_state(const EditorRuntimeState& runtime);
double editor_runtime_box_grid_size(const EditorRuntimeState& runtime);
void set_editor_runtime_box_grid_size(EditorRuntimeState& runtime, double grid_size);
void reset_editor_runtime_box_grid_size(EditorRuntimeState& runtime);
void reset_editor_runtime_box_state(EditorRuntimeState& runtime);
void set_editor_runtime_box_state(EditorRuntimeState& runtime, BoxToolState box);
void clear_editor_runtime_gizmo_hover(EditorRuntimeState& runtime);
void set_editor_runtime_gizmo_hover_axis(EditorRuntimeState& runtime, TransformAxis axis);
void set_editor_runtime_gizmo_hover_from_pick(EditorRuntimeState& runtime, const TransformGizmoPickHit& hit);
void clear_editor_runtime_preview_presentation_state(EditorRuntimeState& runtime);
void clear_editor_runtime_selection_presentation_state(EditorRuntimeState& runtime);
void reset_editor_runtime_document_state(EditorRuntimeState& runtime);
bool discard_editor_runtime_transient_state(EditorRuntimeState& runtime);
bool cancel_editor_runtime_transform_preview_state(EditorRuntimeState& runtime);
void set_editor_runtime_transform_drag_state(EditorRuntimeState& runtime, TransformDragState state);
void begin_editor_runtime_transform_drag_state(
		EditorRuntimeState& runtime,
		ViewportInteractionTool tool,
		TransformDragOperation operation,
		TransformAxis axis,
		quader::QVec3 pivot);
void begin_editor_runtime_transform_drag_state(
		EditorRuntimeState& runtime,
		ViewportInteractionTool tool,
		TransformDragOperation operation,
		TransformAxis axis,
		TransformSpace space,
		quader_poly::Transform3 orientation,
		quader::QVec3 pivot);
const TransformDragState& editor_runtime_transform_drag_state(const EditorRuntimeState& runtime);
bool editor_runtime_transform_drag_active(const EditorRuntimeState& runtime);
void set_editor_runtime_transform_drag_move_state(EditorRuntimeState& runtime, MoveToolState state);
void set_editor_runtime_transform_drag_rotate_state(EditorRuntimeState& runtime, RotateToolState state);
void set_editor_runtime_transform_drag_scale_state(EditorRuntimeState& runtime, ScaleToolState state);
CommandResult apply_editor_runtime_transform_drag_preview(
		const EditorSession& session,
		EditorRuntimeState& runtime,
		const TransformDragPreviewInput& input);
void clear_editor_runtime_transform_drag_state(EditorRuntimeState& runtime);
const InsertEdgeLoopToolState& editor_runtime_insert_edge_loop_state(const EditorRuntimeState& runtime);
void reset_editor_runtime_insert_edge_loop_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_insert_edge_loop = false);
void set_editor_runtime_insert_edge_loop_state(EditorRuntimeState& runtime, InsertEdgeLoopToolState state);
void set_editor_runtime_insert_edge_loop_active(EditorRuntimeState& runtime, bool active);
void set_editor_runtime_insert_edge_loop_dragging(EditorRuntimeState& runtime, bool dragging);
const KnifeToolState& editor_runtime_knife_state(const EditorRuntimeState& runtime);
void reset_editor_runtime_knife_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_cut = false);
void set_editor_runtime_knife_active(EditorRuntimeState& runtime, bool active);
void set_editor_runtime_knife_state(EditorRuntimeState& runtime, KnifeToolState state);
const CutToolState& editor_runtime_cut_state(const EditorRuntimeState& runtime);
const CutHoverState& editor_runtime_cut_hover(const EditorRuntimeState& runtime);
void reset_editor_runtime_cut_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_cut = false);
void set_editor_runtime_cut_state(EditorRuntimeState& runtime, CutToolState state);
void set_editor_runtime_cut_hover(EditorRuntimeState& runtime, CutHoverState hover);
void clear_editor_runtime_cut_hover(EditorRuntimeState& runtime);
const MirrorToolState& editor_runtime_mirror_state(const EditorRuntimeState& runtime);
const MirrorHoverState& editor_runtime_mirror_hover(const EditorRuntimeState& runtime);
void reset_editor_runtime_mirror_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_mirror = false);
void set_editor_runtime_mirror_state(EditorRuntimeState& runtime, MirrorToolState state);
void set_editor_runtime_mirror_hover(EditorRuntimeState& runtime, MirrorHoverState hover);
void clear_editor_runtime_mirror_hover(EditorRuntimeState& runtime);
const PolyToolState& editor_runtime_poly_state(const EditorRuntimeState& runtime);
void reset_editor_runtime_poly_state(
		EditorRuntimeState& runtime,
		bool keep_active_when_current_tool_is_poly = false);
void set_editor_runtime_poly_state(EditorRuntimeState& runtime, PolyToolState state);
void set_editor_runtime_poly_active(EditorRuntimeState& runtime, bool active);
void apply_editor_runtime_tool_activation_plan(
		EditorRuntimeState& runtime,
		const ViewportToolActivationPlan& plan);
EditorRuntimeToolActivationResult activate_editor_runtime_tool(
		EditorRuntimeState& runtime,
		const EditorRuntimeToolActivationRequest& request);
EditorRuntimeToolActivationResult set_editor_runtime_active_tool(
		const EditorSession& session,
		EditorRuntimeState& runtime,
		ViewportInteractionTool requested_tool);

/**
 * Owns Editor Runtime behavior within the modeling runtime domain.
 */
class EditorRuntime {
public:
	EditorRuntime() = default;
	explicit EditorRuntime(EditorRuntimeState state);

	[[nodiscard]] const EditorRuntimeState& state() const;
	[[nodiscard]] EditorRuntimeState& state();
	void set_state(EditorRuntimeState state);

	[[nodiscard]] EditorRuntimeStatus status() const;
	[[nodiscard]] ViewportInteractionTool active_tool() const;
	[[nodiscard]] TransformAxis gizmo_hover_axis() const;
	[[nodiscard]] const SelectionHoverState& hover() const;
	[[nodiscard]] const BoxToolState& box_state() const;
	[[nodiscard]] bool box_active() const;
	[[nodiscard]] double box_grid_size() const;
	[[nodiscard]] const InsertEdgeLoopToolState& insert_edge_loop_state() const;
	[[nodiscard]] const KnifeToolState& knife_state() const;
	[[nodiscard]] const CutToolState& cut_state() const;
	[[nodiscard]] const CutHoverState& cut_hover() const;
	[[nodiscard]] const MirrorToolState& mirror_state() const;
	[[nodiscard]] const MirrorHoverState& mirror_hover() const;
	[[nodiscard]] const PolyToolState& poly_state() const;
	[[nodiscard]] const TransformDragState& transform_drag_state() const;
	[[nodiscard]] bool transform_drag_active() const;

	void set_hover(SelectionHoverState hover);
	void apply_hover_update(const SelectionHoverUpdateResult& result);
	void promote_hover_object_to_candidate();
	void clear_hover();
	void set_box_grid_size(double grid_size);
	void reset_box_state();
	void set_box_state(BoxToolState box);
	[[nodiscard]] BoxToolStateResult set_box_construction_plane(
			quader::QVec3 origin,
			quader::QVec3 normal);
	[[nodiscard]] BoxConstructionPointResult box_construction_point_from_ray(const quader_poly::Ray& ray) const;
	[[nodiscard]] BoxToolStateResult begin_box_footprint(
			quader::QVec3 raw_start,
			quader::QVec3 raw_end,
			bool grid_enabled);
	[[nodiscard]] BoxToolStateResult update_box_footprint(
			quader::QVec3 raw_end,
			bool grid_enabled);
	[[nodiscard]] BoxToolStateResult update_box_hover_cell(
			quader::QVec3 local_point,
			bool grid_enabled);
	[[nodiscard]] double box_height_distance_for_ray(const quader_poly::Ray& ray) const;
	[[nodiscard]] BoxToolStateResult begin_box_height(
			quader::QVec2 screen_position,
			double axis_distance,
			bool has_axis_distance,
			bool grid_enabled);
	[[nodiscard]] BoxToolStateResult update_box_height_preview(
			double axis_distance,
			bool has_axis_distance,
			quader::QVec2 screen_position,
			bool grid_enabled);
	[[nodiscard]] BoxPreviewDescriptor box_preview_descriptor(bool grid_enabled) const;
	void clear_preview_presentation_state();
	void clear_selection_presentation_state();
	void reset_document_state();
	bool discard_transient_state();
	void set_transform_drag_state(TransformDragState state);
	void begin_transform_drag_state(
		ViewportInteractionTool tool,
		TransformDragOperation operation,
		TransformAxis axis,
		quader::QVec3 pivot);
	void begin_transform_drag_state(
		ViewportInteractionTool tool,
		TransformDragOperation operation,
		TransformAxis axis,
		TransformSpace space,
		quader_poly::Transform3 orientation,
		quader::QVec3 pivot);
	void set_transform_drag_move_state(MoveToolState state);
	void set_transform_drag_rotate_state(RotateToolState state);
	void set_transform_drag_scale_state(ScaleToolState state);
	[[nodiscard]] CommandResult apply_transform_drag_preview(
		const EditorSession& session,
		const TransformDragPreviewInput& input);
	[[nodiscard]] CommandResult apply_transform_drag_preview(
		std::span<const PolygonObject> selection_objects,
		SelectionMode selection_mode,
		const TransformDragPreviewInput& input);
	void clear_transform_drag_state();
	EditorRuntimeToolActivationResult activate_tool(const EditorRuntimeToolActivationRequest& request);
	EditorRuntimeToolActivationResult set_active_tool(const EditorSession& session, ViewportInteractionTool requested_tool);
	void reset_insert_edge_loop_state(bool keep_active_when_current_tool_is_insert_edge_loop = false);
	void set_insert_edge_loop_state(InsertEdgeLoopToolState state);
	void set_insert_edge_loop_active(bool active);
	void set_insert_edge_loop_dragging(bool dragging);
	[[nodiscard]] bool set_insert_edge_loop_preview_from_pick(
			EditorSession& session,
			const quader_poly::Ray& ray,
			double grid_size,
			double vertex_radius,
			double edge_radius,
			bool snap_enabled,
			bool dragging,
			int target_object_id = -1);
	[[nodiscard]] bool set_insert_edge_loop_preview_from_pick(
			const std::vector<PolygonObject>& objects,
			const quader_poly::Ray& ray,
			double grid_size,
			double vertex_radius,
			double edge_radius,
			bool snap_enabled,
			bool dragging,
			int target_object_id = -1);
	[[nodiscard]] bool set_insert_edge_loop_preview_from_hit(
			EditorSession& session,
			const PickHitRecord& hit,
			std::optional<float> resolved_factor,
			bool dragging);
	[[nodiscard]] bool set_insert_edge_loop_preview_from_hit(
			const std::vector<PolygonObject>& objects,
			const PickHitRecord& hit,
			std::optional<float> resolved_factor,
			bool dragging);
	[[nodiscard]] bool update_insert_edge_loop_drag_factor(
			EditorSession& session,
			const quader_poly::Ray& ray,
			double grid_size,
			bool snap_enabled);
	[[nodiscard]] bool update_insert_edge_loop_drag_factor(
			const std::vector<PolygonObject>& objects,
			const quader_poly::Ray& ray,
			double grid_size,
			bool snap_enabled);
	[[nodiscard]] bool update_insert_edge_loop_drag_factor_from_world(
			EditorSession& session,
			std::optional<float> resolved_factor);
	[[nodiscard]] bool update_insert_edge_loop_drag_factor_from_world(
			const std::vector<PolygonObject>& objects,
			std::optional<float> resolved_factor);
	void reset_knife_state(bool keep_active_when_current_tool_is_knife = false);
	void set_knife_state(KnifeToolState state);
	void set_knife_active(bool active);
	void clear_knife_hover();
	[[nodiscard]] bool update_knife_preview_from_pick(
			EditorSession& session,
			const quader_poly::Ray& ray,
			double grid_size,
			bool snap_enabled,
			bool constrain_axis,
			int target_object_id = -1);
	[[nodiscard]] bool update_knife_preview_from_pick(
			const std::vector<PolygonObject>& objects,
			const quader_poly::Ray& ray,
			double grid_size,
			bool snap_enabled,
			bool constrain_axis,
			int target_object_id = -1);
	[[nodiscard]] bool update_knife_preview_from_hit(
			EditorSession& session,
			const PickHitRecord& hit,
			double grid_size,
			bool snap_enabled,
			bool constrain_axis);
	[[nodiscard]] bool update_knife_preview_from_hit(
			const std::vector<PolygonObject>& objects,
			const PickHitRecord& hit,
			double grid_size,
			bool snap_enabled,
			bool constrain_axis);
	[[nodiscard]] bool accept_knife_current_point(double grid_size);
	void reset_cut_state(bool keep_active_when_current_tool_is_cut = false);
	void set_cut_state(CutToolState state);
	void set_cut_hover(CutHoverState hover);
	void clear_cut_hover();
	[[nodiscard]] CutToolResult refresh_cut_preview_from_selection(
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects);
	[[nodiscard]] CutToolResult update_cut_hover_from_ray(
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const CutRayUpdateInput& input);
	[[nodiscard]] CutToolResult click_cut_from_ray(
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const CutRayUpdateInput& input);
	[[nodiscard]] CutToolResult begin_cut_point_drag(int point_index);
	[[nodiscard]] CutToolResult begin_cut_line_drag(const CutRayUpdateInput& input);
	[[nodiscard]] CutToolResult end_cut_drag();
	[[nodiscard]] CutToolResult set_cut_mode(CutMode mode);
	void reset_mirror_state(bool keep_active_when_current_tool_is_mirror = false);
	void set_mirror_state(MirrorToolState state);
	void set_mirror_hover(MirrorHoverState hover);
	void clear_mirror_hover();
	[[nodiscard]] MirrorToolResult refresh_mirror_preview_from_selection(
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects);
	[[nodiscard]] MirrorToolResult update_mirror_hover_from_ray(
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const MirrorRayUpdateInput& input);
	[[nodiscard]] MirrorToolResult click_mirror_from_ray(
			const RectangleSelectionState& selection,
			std::span<const PolygonObject> objects,
			const MirrorRayUpdateInput& input);
	[[nodiscard]] MirrorToolResult begin_mirror_point_drag(int point_index);
	[[nodiscard]] MirrorToolResult begin_mirror_line_drag(const MirrorRayUpdateInput& input);
	[[nodiscard]] MirrorToolResult end_mirror_drag();
	void reset_poly_state(bool keep_active_when_current_tool_is_poly = false);
	void set_poly_state(PolyToolState state);
	void set_poly_active(bool active);
	[[nodiscard]] PolyToolResult update_poly_hover(
			quader::QVec3 position,
			quader::QVec3 plane_normal,
			bool constrain_axis,
			double grid_size,
			bool snap_enabled);
	[[nodiscard]] PolyToolResult accept_poly_current_point();
	void set_gizmo_hover_axis(TransformAxis axis);
	void set_gizmo_hover_from_pick(const TransformGizmoPickHit& hit);
	void clear_gizmo_hover();

private:
	EditorRuntimeState state_;
};

} // namespace quader_editor_core
