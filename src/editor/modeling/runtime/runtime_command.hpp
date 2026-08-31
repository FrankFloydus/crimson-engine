////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/modeling/operations/operation_schema.hpp>

#include <variant>

namespace quader_editor_core {

/**
 * Immutable selection facts consumed by retained runtime tool commands.
 * Native hosts can provide this view from an SDK-derived selection snapshot
 * without materializing a mutable EditorSession.
 */
struct EditorRuntimeSelectionView {
  const std::vector<PolygonObject> *object_records = nullptr;
  SelectionMode mode = SelectionMode::Object;
  int active_object_id = -1;

  [[nodiscard]] const std::vector<PolygonObject> &objects() const noexcept {
    static const std::vector<PolygonObject> empty;
    return object_records != nullptr ? *object_records : empty;
  }

  [[nodiscard]] SelectionMode selection_mode() const noexcept { return mode; }
  [[nodiscard]] int active_object() const noexcept { return active_object_id; }
  [[nodiscard]] std::vector<int> selected_object_ids() const {
    return quader_editor_core::selected_object_ids(objects());
  }
  [[nodiscard]] bool has_selection() const {
    return quader_editor_core::has_selection(objects(), mode,
                                             active_object_id);
  }
};

/**
 * Enumerates EditorRuntimeCommandId values used by the modeling layer.
 */
enum class EditorRuntimeCommandId {
  ClearHover,
  SetActiveTool,
  UpdateInsertEdgeLoopHover,
  BeginInsertEdgeLoopDrag,
  UpdateInsertEdgeLoopDrag,
  CancelInsertEdgeLoop,
  ActivateCut,
  UpdateCutPreview,
  SetCutMode,
  CancelCut,
  UpdateCutHover,
  ClickCut,
  BeginCutPointDrag,
  BeginCutLineDrag,
  EndCutDrag,
  ActivateMirror,
  UpdateMirrorPreview,
  CancelMirror,
  UpdateMirrorHover,
  ClickMirror,
  BeginMirrorPointDrag,
  BeginMirrorLineDrag,
  EndMirrorDrag,
  UpdatePolyHover,
  AcceptPolyPoint,
  CancelPoly,
  UpdateBoxHover,
  BeginBoxFootprint,
  UpdateBoxFootprint,
  BeginBoxHeight,
  UpdateBoxHeight,
  CancelBox,
};

/**
 * Represents a Set Active Tool Runtime Command Settings value used by the modeling runtime domain.
 */
struct SetActiveToolRuntimeCommandSettings {
  ViewportInteractionTool tool = ViewportInteractionTool::Select;
};

/**
 * Represents an Insert Edge Loop Pick Runtime Command Settings value used by the modeling runtime domain.
 */
struct InsertEdgeLoopPickRuntimeCommandSettings {
	quader_poly::Ray ray;
	int target_object_id = -1;
	std::optional<PickHitRecord> resolved_hit;
	std::optional<float> resolved_factor;
	bool resolved_factor_from_world = false;
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
 * Represents a Poly Hover Runtime Command Settings value used by the modeling runtime domain.
 */
struct PolyHoverRuntimeCommandSettings {
	ViewportCameraState camera;
	quader_poly::Ray ray;
	// Authoritative only when surface_hit_resolved is true; an empty value then
	// represents a host-resolved miss and must retain the viewport-grid fallback.
	std::optional<PickHitRecord> resolved_surface_hit;
	bool surface_hit_resolved = false;
	double grid_size = 1.0;
	bool snap_enabled = true;
	bool constrain_axis = false;
};

/**
 * Represents a Box Ray Runtime Command Settings value used by the modeling runtime domain.
 */
struct BoxRayRuntimeCommandSettings {
	quader_poly::Ray ray;
	// Authoritative only when surface_hit_resolved is true; an empty value then
	// represents a host-resolved miss and must not fall back to raw geometry.
	std::optional<PickHitRecord> resolved_surface_hit;
	bool surface_hit_resolved = false;
	double grid_size = 1.0;
	bool snap_enabled = true;
};

/**
 * Represents a Box Height Runtime Command Settings value used by the modeling runtime domain.
 */
struct BoxHeightRuntimeCommandSettings {
	quader::QVec2 screen_position;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool snap_enabled = true;
	bool has_camera = false;
	ViewportCameraState camera;
};

using EditorRuntimeCommandSettings = std::variant<
		std::monostate,
		SetActiveToolRuntimeCommandSettings,
		InsertEdgeLoopPickRuntimeCommandSettings,
		SetCutModeRuntimeCommandSettings,
		CutRayRuntimeCommandSettings,
		CutPointDragRuntimeCommandSettings,
		MirrorRayRuntimeCommandSettings,
		MirrorPointDragRuntimeCommandSettings,
		PolyHoverRuntimeCommandSettings,
		BoxRayRuntimeCommandSettings,
		BoxHeightRuntimeCommandSettings>;

/**
 * Represents an Editor Runtime Command value used by the modeling runtime domain.
 */
struct EditorRuntimeCommand {
  EditorRuntimeCommandId command_id =
      EditorRuntimeCommandId::ClearHover;
  EditorRuntimeCommandSettings settings = std::monostate{};
};

/**
 * Stores the Editor Runtime Command Result data contract used by the modeling runtime domain.
 */
struct EditorRuntimeCommandResult {
	CommandResult command;
	EditorRuntimeStatus runtime;
	bool clear_selection_requested = false;
};

/**
 * Executes the Cut hover command against a borrowed, read-only session view.
 *
 * Cut hover changes only EditorRuntime state. Keeping this path const prevents
 * high-frequency pointer motion from copying the session's complete object
 * projection merely to satisfy the mutable generic command boundary.
 */
[[nodiscard]] EditorRuntimeCommandResult execute_cut_hover_runtime_command(
		const EditorSession& session,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command);
[[nodiscard]] EditorRuntimeCommandResult execute_cut_hover_runtime_command(
		const EditorRuntimeSelectionView& selection,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command);

[[nodiscard]] EditorRuntimeCommandResult execute_runtime_command(
		EditorSession& session,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command);
[[nodiscard]] EditorRuntimeCommandResult execute_runtime_command(
		const EditorRuntimeSelectionView& selection,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command);

} // namespace quader_editor_core
