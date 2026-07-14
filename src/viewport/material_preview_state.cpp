////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/material_preview_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace quader {
namespace {

constexpr float kZoomStepFactor = 1.12f;

float wrapped_angle(float radians) noexcept {
  constexpr float kFullTurn = 2.0f * std::numbers::pi_v<float>;
  float wrapped = std::fmod(radians + std::numbers::pi_v<float>, kFullTurn);
  if (wrapped < 0.0f) {
    wrapped += kFullTurn;
  }
  return wrapped - std::numbers::pi_v<float>;
}

bool finite(QVec2 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool valid(MaterialPreviewMeshKind mesh_kind) noexcept {
  switch (mesh_kind) {
  case MaterialPreviewMeshKind::Sphere:
  case MaterialPreviewMeshKind::Box:
  case MaterialPreviewMeshKind::RoundedBox:
  case MaterialPreviewMeshKind::Cylinder:
  case MaterialPreviewMeshKind::Plane:
    return true;
  }
  return false;
}

bool valid(MaterialPreviewShadingMode shading_mode) noexcept {
  switch (shading_mode) {
  case MaterialPreviewShadingMode::Wireframe:
  case MaterialPreviewShadingMode::Shaded:
  case MaterialPreviewShadingMode::Rendered:
    return true;
  }
  return false;
}

} // namespace

QQuat MaterialPreviewState::object_rotation() const noexcept {
  return quat_from_euler_radians(
      {pitch_radians_, yaw_radians_, 0.0f});
}

bool MaterialPreviewState::set_mesh_kind(
    MaterialPreviewMeshKind mesh_kind) noexcept {
  if (!valid(mesh_kind) || mesh_kind_ == mesh_kind) {
    return false;
  }
  mesh_kind_ = mesh_kind;
  mark_changed();
  return true;
}

bool MaterialPreviewState::set_shading_mode(
    MaterialPreviewShadingMode shading_mode) noexcept {
  if (!valid(shading_mode) || shading_mode_ == shading_mode) {
    return false;
  }
  shading_mode_ = shading_mode;
  mark_changed();
  return true;
}

bool MaterialPreviewState::reset_view() noexcept {
  if (yaw_radians_ == 0.0f && pitch_radians_ == 0.0f &&
      camera_distance_ == kDefaultCameraDistance) {
    return false;
  }
  yaw_radians_ = 0.0f;
  pitch_radians_ = 0.0f;
  camera_distance_ = kDefaultCameraDistance;
  mark_changed();
  return true;
}

bool MaterialPreviewState::tumble(QVec2 delta_pixels) noexcept {
  if (!finite(delta_pixels) ||
      (delta_pixels.x == 0.0f && delta_pixels.y == 0.0f)) {
    return false;
  }

  yaw_radians_ = wrapped_angle(
      yaw_radians_ + delta_pixels.x * kTumbleRadiansPerPixel);
  pitch_radians_ = std::clamp(
      pitch_radians_ + delta_pixels.y * kTumbleRadiansPerPixel,
      -kMaximumPitchRadians, kMaximumPitchRadians);
  mark_changed();
  return true;
}

bool MaterialPreviewState::zoom(float wheel_steps) noexcept {
  if (!std::isfinite(wheel_steps) || wheel_steps == 0.0f) {
    return false;
  }
  const float new_distance = std::clamp(
      camera_distance_ * std::pow(kZoomStepFactor, -wheel_steps),
      kMinimumCameraDistance, kMaximumCameraDistance);
  if (new_distance == camera_distance_) {
    return false;
  }
  camera_distance_ = new_distance;
  mark_changed();
  return true;
}

void MaterialPreviewState::mark_changed() noexcept {
  if (revision_ != std::numeric_limits<std::uint64_t>::max()) {
    ++revision_;
  }
}

MaterialPreviewInteractionResult MaterialPreviewInputRouter::route(
    MaterialPreviewState &state,
    const MaterialPreviewInteractionFrame &input) const noexcept {
  MaterialPreviewInteractionResult result;

  if (input.reset_requested) {
    result.reset = state.reset_view();
  }
  if (input.tumble_active) {
    result.tumbled = state.tumble(input.tumble_delta_pixels);
  }
  result.zoomed = state.zoom(input.zoom_wheel_steps);
  result.changed = result.reset || result.tumbled || result.zoomed;
  return result;
}

} // namespace quader
