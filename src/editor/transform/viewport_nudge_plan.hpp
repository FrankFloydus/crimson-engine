////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/qvec3.hpp"

#include <optional>

namespace quader {

/// Requests a discrete transform in the active viewport's screen directions.
enum class ViewportNudgeAction {
  MoveUp,
  MoveDown,
  MoveLeft,
  MoveRight,
  RotateUp,
  RotateDown,
  RotateLeft,
  RotateRight,
};

/// Identifies the positive captured gizmo axis used by a rotation plan.
enum class ViewportNudgeAxis {
  None,
  X,
  Y,
  Z,
};

/// Captures camera and gizmo directions without retaining live editor state.
struct ViewportNudgePlanRequest {
  QVec3 camera_right = {1.0F, 0.0F, 0.0F};
  QVec3 camera_up = {0.0F, 1.0F, 0.0F};
  QVec3 camera_forward = {0.0F, 0.0F, -1.0F};
  bool orthographic = false;
  QVec3 gizmo_x_axis = {1.0F, 0.0F, 0.0F};
  QVec3 gizmo_y_axis = {0.0F, 1.0F, 0.0F};
  QVec3 gizmo_z_axis = {0.0F, 0.0F, 1.0F};
  /// Uses native Y-up or the component-local Z normal for perspective
  /// correction.
  ViewportNudgeAxis up_axis = ViewportNudgeAxis::Y;
  double grid_step = 1.0;
  bool grid_snap_enabled = true;
  ViewportNudgeAction action = ViewportNudgeAction::MoveUp;
};

/// Resolves translation in world space or rotation around a positive gizmo
/// axis.
struct ViewportNudgePlan {
  QVec3 move_delta = {};
  ViewportNudgeAxis rotate_axis = ViewportNudgeAxis::None;
  double rotate_angle_degrees = 0.0;
};

/// Returns no plan for unusable frames, actions or translation steps.
[[nodiscard]] std::optional<ViewportNudgePlan>
make_viewport_nudge_plan(const ViewportNudgePlanRequest &request);

} // namespace quader
