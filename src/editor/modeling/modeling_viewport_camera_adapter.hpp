////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/interactions/viewport_projection.hpp"
#include "editor/modeling/modeling_types.hpp"

#include <algorithm>

namespace quader {

/// Compares every projection input before rebuilding cached screen geometry.
[[nodiscard]] inline bool modeling_viewport_camera_matches(
    const ModelingViewportCamera &left, const ModelingViewportCamera &right) {
  const auto same_vector = [](QVec3 a, QVec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
  };
  return left.orthographic == right.orthographic &&
         left.affine_model_space == right.affine_model_space &&
         same_vector(left.origin, right.origin) &&
         same_vector(left.x_axis, right.x_axis) &&
         same_vector(left.y_axis, right.y_axis) &&
         same_vector(left.z_axis, right.z_axis) &&
         left.viewport_size.x == right.viewport_size.x &&
         left.viewport_size.y == right.viewport_size.y &&
         left.orthographic_size == right.orthographic_size &&
         left.fov_degrees == right.fov_degrees;
}

/** Converts the native adapter camera into the canonical modeling runtime DTO. */
[[nodiscard]] inline quader_editor_core::ViewportCameraState
modeling_runtime_camera(const ModelingViewportCamera &value) {
  quader_editor_core::ViewportCameraState camera;
  camera.projection = value.orthographic
                          ? quader_editor_core::CameraProjection::Orthographic
                          : quader_editor_core::CameraProjection::Perspective;
  camera.affine_model_space = value.affine_model_space;
  camera.transform = {
      .x_axis = value.x_axis,
      .y_axis = value.y_axis,
      .z_axis = value.z_axis,
      .origin = value.origin,
  };
  camera.viewport_size = {
      std::max(value.viewport_size.x, 1.0f),
      std::max(value.viewport_size.y, 1.0f),
  };
  camera.orthographic_size = std::max(value.orthographic_size, 0.0001);
  camera.fov_degrees = value.fov_degrees;
  return camera;
}

} // namespace quader
