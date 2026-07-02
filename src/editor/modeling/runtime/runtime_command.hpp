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
 * Enumerates EditorRuntimeCommandId values used by the modeling layer.
 */
enum class EditorRuntimeCommandId {
  CopySelectedMeshes,
  PasteCopiedMeshes,
  ClearSelection,
  SelectAll,
  InvertSelection,
  DeleteSelection,
  MarkClean,
  Undo,
  Redo,
  ClearHover,
  SetSelectionMode,
  SetActiveTool,
  PickSelect,
  UpdateInsertEdgeLoopHover,
  BeginInsertEdgeLoopDrag,
  UpdateInsertEdgeLoopDrag,
  CommitInsertEdgeLoopDrag,
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
  FinalizeCut,
  ActivateMirror,
  UpdateMirrorPreview,
  CancelMirror,
  UpdateMirrorHover,
  ClickMirror,
  BeginMirrorPointDrag,
  BeginMirrorLineDrag,
  EndMirrorDrag,
  FinalizeMirror,
  MirrorAxis,
  FlipView,
  CreateBoxFromBounds,
  UpdateBoxHover,
  BeginBoxFootprint,
  UpdateBoxFootprint,
  BeginBoxHeight,
  UpdateBoxHeight,
  CommitBox,
  CancelBox,
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
  ViewportInteractionTool tool = ViewportInteractionTool::Select;
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
 * Represents a Mirror QAxis Runtime Command Settings value used by the modeling runtime domain.
 */
struct MirrorAxisRuntimeCommandSettings {
  TransformAxis axis = TransformAxis::None;
};

/**
 * Represents a View Flip Runtime Command Settings value used by the modeling runtime domain.
 */
struct ViewFlipRuntimeCommandSettings {
  ViewportFlipOrientation orientation = ViewportFlipOrientation::Horizontal;
};

/**
 * Represents a Box Ray Runtime Command Settings value used by the modeling runtime domain.
 */
struct BoxRayRuntimeCommandSettings {
	quader_poly::Ray ray;
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

/**
 * Represents a Create Box From Bounds Runtime Command Settings value used by the modeling runtime domain.
 */
struct CreateBoxFromBoundsRuntimeCommandSettings {
	quader::QVec3 min_bounds;
	quader::QVec3 max_bounds;
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
  EditorRuntimeCommandId command_id =
      EditorRuntimeCommandId::CopySelectedMeshes;
  EditorRuntimeCommandSettings settings = std::monostate{};
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
