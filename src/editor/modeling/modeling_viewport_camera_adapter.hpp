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
