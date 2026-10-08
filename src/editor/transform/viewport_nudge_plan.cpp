////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/viewport_nudge_plan.hpp"

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace quader {
namespace {

using Frame = std::array<QVec3, 3>;

struct SignedAxis {
  std::size_t index = 0;
  float sign = 1.0F;
};

std::optional<Frame> normalized_frame(Frame frame) {
  for (auto &axis : frame) {
    const double magnitude =
        std::hypot(static_cast<double>(axis.x), static_cast<double>(axis.y),
                   static_cast<double>(axis.z));
    if (!std::isfinite(magnitude) || magnitude == 0.0) {
      return std::nullopt;
    }
    axis = {static_cast<float>(axis.x / magnitude),
            static_cast<float>(axis.y / magnitude),
            static_cast<float>(axis.z / magnitude)};
  }
  constexpr float kMinimumVolume = 0.000001F;
  if (std::abs(dot(cross(frame[0], frame[1]), frame[2])) <= kMinimumVolume) {
    return std::nullopt;
  }
  return frame;
}

SignedAxis nearest_signed_axis(QVec3 direction, const Frame &frame) {
  SignedAxis result;
  float best_alignment = -std::numeric_limits<float>::infinity();
  // Equal scores retain the first direction in +X,+Y,+Z,-X,-Y,-Z order.
  for (const float sign : {1.0F, -1.0F}) {
    for (std::size_t index = 0; index < frame.size(); ++index) {
      const float alignment = sign * dot(direction, frame[index]);
      if (alignment > best_alignment) {
        result = {index, sign};
        best_alignment = alignment;
      }
    }
  }
  return result;
}

} // namespace

std::optional<ViewportNudgePlan>
make_viewport_nudge_plan(const ViewportNudgePlanRequest &request) {
  const auto camera = normalized_frame(
      {request.camera_right, request.camera_up, request.camera_forward});
  const auto gizmo = normalized_frame(
      {request.gizmo_x_axis, request.gizmo_y_axis, request.gizmo_z_axis});
  if (!camera || !gizmo) {
    return std::nullopt;
  }

  std::size_t up_index = 0;
  switch (request.up_axis) {
  case ViewportNudgeAxis::X:
    up_index = 0;
    break;
  case ViewportNudgeAxis::Y:
    up_index = 1;
    break;
  case ViewportNudgeAxis::Z:
    up_index = 2;
    break;
  default:
    return std::nullopt;
  }

  const auto horizontal = nearest_signed_axis((*camera)[0] * -1.0F, *gizmo);
  const auto vertical = nearest_signed_axis((*camera)[1], *gizmo);
  auto forward = nearest_signed_axis((*camera)[2], *gizmo);
  if (!request.orthographic) {
    constexpr float kVerticalCorrectionThreshold = 0.2F;
    const float vertical_alignment = dot((*camera)[2], (*gizmo)[up_index]);
    const float vertical_sign = vertical_alignment < 0.0F ? -1.0F : 1.0F;
    if (std::abs(vertical_alignment) > kVerticalCorrectionThreshold &&
        (forward.index != up_index || forward.sign != vertical_sign)) {
      forward = {vertical.index, vertical.sign * vertical_sign};
    }
  }

  SignedAxis axis;
  float direction = 1.0F;
  bool rotate = false;
  switch (request.action) {
  case ViewportNudgeAction::MoveUp:
    axis = vertical;
    break;
  case ViewportNudgeAction::MoveDown:
    axis = vertical;
    direction = -1.0F;
    break;
  case ViewportNudgeAction::MoveLeft:
    axis = horizontal;
    break;
  case ViewportNudgeAction::MoveRight:
    axis = horizontal;
    direction = -1.0F;
    break;
  case ViewportNudgeAction::RotateUp:
    axis = horizontal;
    rotate = true;
    break;
  case ViewportNudgeAction::RotateDown:
    axis = horizontal;
    direction = -1.0F;
    rotate = true;
    break;
  case ViewportNudgeAction::RotateLeft:
    axis = forward;
    direction = -1.0F;
    rotate = true;
    break;
  case ViewportNudgeAction::RotateRight:
    axis = forward;
    rotate = true;
    break;
  default:
    return std::nullopt;
  }

  ViewportNudgePlan result;
  if (rotate) {
    constexpr std::array kAxes = {ViewportNudgeAxis::X, ViewportNudgeAxis::Y,
                                  ViewportNudgeAxis::Z};
    result.rotate_axis = kAxes[axis.index];
    result.rotate_angle_degrees = 45.0 * axis.sign * direction;
    return result;
  }

  const double step = request.grid_snap_enabled ? request.grid_step : 1.0;
  if (!std::isfinite(step) || step <= 0.0 ||
      step > std::numeric_limits<float>::max()) {
    return std::nullopt;
  }
  result.move_delta =
      (*gizmo)[axis.index] * (axis.sign * direction * static_cast<float>(step));
  if (result.move_delta.x == 0.0F && result.move_delta.y == 0.0F &&
      result.move_delta.z == 0.0F) {
    return std::nullopt;
  }
  return result;
}

} // namespace quader
