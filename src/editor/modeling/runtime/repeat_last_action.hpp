////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/interactions/transform_drag.hpp>
#include <quader/modeling/mesh/face_texture_actions.hpp>

#include <optional>

namespace quader_editor_core {

/**
 * Enumerates RepeatableActionKind values used by the modeling layer.
 */
enum class RepeatableActionKind {
  None,
  Operation,
  Transform,
  FaceTexture,
};

/**
 * Enumerates RepeatTransformDirectionMode values used by the modeling layer.
 */
enum class RepeatTransformDirectionMode {
  WorldDelta,
  SelectionForward,
};

/**
 * Represents a Repeat Transform Action value used by the modeling runtime domain.
 */
struct RepeatTransformAction {
  ViewportInteractionTool tool = ViewportInteractionTool::Unknown;
  TransformDragOperation operation = TransformDragOperation::Transform;
  TransformAxis axis = TransformAxis::None;
  TransformSpace space = TransformSpace::Global;
  quader_poly::Transform3 orientation;
  bool duplicate_selected = false;
  quader::QVec3 pivot;
  quader::QVec3 move_delta;
  double rotate_angle_degrees = 0.0;
  quader::QVec3 scale{1.0F, 1.0F, 1.0F};
  RepeatTransformDirectionMode direction_mode =
      RepeatTransformDirectionMode::WorldDelta;
  float signed_forward_distance = 0.0F;
};

/**
 * Stores the Repeat Last Action State data contract used by the modeling runtime domain.
 */
struct RepeatLastActionState {
  RepeatableActionKind last_action_kind = RepeatableActionKind::None;
  std::optional<EditorOperationCommand> last_operation;
  std::optional<EditorOperationCommand> pending_preview_operation;
  std::optional<RepeatTransformAction> last_transform;
  std::optional<quader::modeling::FaceTextureActionOptions> last_face_texture;
};

[[nodiscard]] bool operation_is_repeatable(EditorOperationId operation_id);
void record_repeatable_operation(
		RepeatLastActionState& state,
		const EditorOperationCommand& command,
		const CommandResult& result);
void record_repeatable_transform(
		RepeatLastActionState& state,
		const EditorSession& session,
		const TransformDragState& drag,
		bool duplicate_selected);
} // namespace quader_editor_core
