////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/editor_runtime.h>
#include <editor/modeling/operations/operation_schema.h>

#include <variant>

namespace quader_editor_core {

/**
 * Enumerates EditorRuntimeCommandId values used by the modeling layer.
 */
enum class EditorRuntimeCommandId {
	copy_selected_meshes,
	paste_copied_meshes,
	clear_selection,
	select_all,
	invert_selection,
	delete_selection,
	mark_clean,
	undo,
	redo,
	clear_hover,
	set_selection_mode,
	set_active_tool,
	pick_select,
	update_insert_edge_loop_hover,
	begin_insert_edge_loop_drag,
	update_insert_edge_loop_drag,
	commit_insert_edge_loop_drag,
	cancel_insert_edge_loop,
	activate_cut,
	update_cut_preview,
	set_cut_mode,
	cancel_cut,
	update_cut_hover,
	click_cut,
	begin_cut_point_drag,
	begin_cut_line_drag,
	end_cut_drag,
	finalize_cut,
	activate_mirror,
	update_mirror_preview,
	cancel_mirror,
	update_mirror_hover,
	click_mirror,
	begin_mirror_point_drag,
	begin_mirror_line_drag,
	end_mirror_drag,
	finalize_mirror,
	mirror_axis,
	flip_view,
	create_box_from_bounds,
	update_box_hover,
	begin_box_footprint,
	update_box_footprint,
	begin_box_height,
	update_box_height,
	commit_box,
	cancel_box,
};

/**
 * Represents a Paste Copied Meshes Runtime Command Settings value used by the modeling runtime domain.
 */
struct PasteCopiedMeshesRuntimeCommandSettings {
	CommandVec3Fact target_center;
};

/**
 * Represents a Set Selection Mode Runtime Command Settings value used by the modeling runtime domain.
 */
struct SetSelectionModeRuntimeCommandSettings {
	int selection_mode = 0;
};

/**
 * Represents a Set Active Tool Runtime Command Settings value used by the modeling runtime domain.
 */
struct SetActiveToolRuntimeCommandSettings {
	ViewportInteractionTool tool = ViewportInteractionTool::select;
};

/**
 * Represents a Pick Select Runtime Command Settings value used by the modeling runtime domain.
 */
struct PickSelectRuntimeCommandSettings {
	quader_poly::Ray ray;
	int click_count = 1;
	bool control_pressed = false;
	bool shift_pressed = false;
	bool command_pressed = false;
	bool alt_pressed = false;
	double vertex_radius = 0.0;
	double edge_radius = 0.0;
	bool has_hidden_material_slot = false;
	std::uint32_t hidden_material_slot = 0;
};

/**
 * Represents an Insert Edge Loop Pick Runtime Command Settings value used by the modeling runtime domain.
 */
struct InsertEdgeLoopPickRuntimeCommandSettings {
	quader_poly::Ray ray;
	double grid_size = 1.0;
	double vertex_radius = 0.0;
	double edge_radius = 0.0;
	bool snap_enabled = true;
};

/**
 * Represents a Set Cut Mode Runtime Command Settings value used by the modeling runtime domain.
 */
struct SetCutModeRuntimeCommandSettings {
	int mode = 0;
};

/**
 * Represents a Cut Ray Runtime Command Settings value used by the modeling runtime domain.
 */
struct CutRayRuntimeCommandSettings {
	ViewportCameraState camera;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool dragging = false;
};

/**
 * Represents a Cut Point Drag Runtime Command Settings value used by the modeling runtime domain.
 */
struct CutPointDragRuntimeCommandSettings {
	int point_index = 0;
};

/**
 * Represents a Mirror Ray Runtime Command Settings value used by the modeling runtime domain.
 */
struct MirrorRayRuntimeCommandSettings {
	ViewportCameraState camera;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool dragging = false;
};

/**
 * Represents a Mirror Point Drag Runtime Command Settings value used by the modeling runtime domain.
 */
struct MirrorPointDragRuntimeCommandSettings {
	int point_index = 0;
};

/**
 * Represents a Mirror Axis Runtime Command Settings value used by the modeling runtime domain.
 */
struct MirrorAxisRuntimeCommandSettings {
	TransformAxis axis = TransformAxis::none;
};

/**
 * Represents a View Flip Runtime Command Settings value used by the modeling runtime domain.
 */
struct ViewFlipRuntimeCommandSettings {
	ViewportFlipOrientation orientation = ViewportFlipOrientation::horizontal;
};

/**
 * Represents a Box Ray Runtime Command Settings value used by the modeling runtime domain.
 */
struct BoxRayRuntimeCommandSettings {
	quader_poly::Ray ray;
	double grid_size = 1.0;
};

/**
 * Represents a Box Height Runtime Command Settings value used by the modeling runtime domain.
 */
struct BoxHeightRuntimeCommandSettings {
	quader_poly::Vec2 screen_position;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool has_camera = false;
	ViewportCameraState camera;
};

/**
 * Represents a Create Box From Bounds Runtime Command Settings value used by the modeling runtime domain.
 */
struct CreateBoxFromBoundsRuntimeCommandSettings {
	quader_poly::Vec3 min_bounds;
	quader_poly::Vec3 max_bounds;
};

using EditorRuntimeCommandSettings = std::variant<
		std::monostate,
		PasteCopiedMeshesRuntimeCommandSettings,
		SetSelectionModeRuntimeCommandSettings,
		SetActiveToolRuntimeCommandSettings,
		PickSelectRuntimeCommandSettings,
		InsertEdgeLoopPickRuntimeCommandSettings,
		SetCutModeRuntimeCommandSettings,
		CutRayRuntimeCommandSettings,
		CutPointDragRuntimeCommandSettings,
		MirrorRayRuntimeCommandSettings,
		MirrorPointDragRuntimeCommandSettings,
		MirrorAxisRuntimeCommandSettings,
		ViewFlipRuntimeCommandSettings,
		BoxRayRuntimeCommandSettings,
		BoxHeightRuntimeCommandSettings,
		CreateBoxFromBoundsRuntimeCommandSettings>;

/**
 * Represents an Editor Runtime Command value used by the modeling runtime domain.
 */
struct EditorRuntimeCommand {
	EditorRuntimeCommandId command_id = EditorRuntimeCommandId::copy_selected_meshes;
	EditorRuntimeCommandSettings settings = std::monostate {};
};

/**
 * Stores the Editor Runtime Command Result data contract used by the modeling runtime domain.
 */
struct EditorRuntimeCommandResult {
	CommandResult command;
	EditorRuntimeStatus runtime;
};

[[nodiscard]] EditorRuntimeCommandResult execute_runtime_command(
		EditorSession& session,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command);

} // namespace quader_editor_core
