////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <optional>

#include <imgui.h>

namespace quader {

inline float viewport_distance_squared(ImVec2 a, ImVec2 b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return dx * dx + dy * dy;
}

inline float viewport_distance_to_segment_squared(ImVec2 point, ImVec2 a,
                                                  ImVec2 b) {
  const ImVec2 ab{b.x - a.x, b.y - a.y};
  const ImVec2 ap{point.x - a.x, point.y - a.y};
  const float length_squared = ab.x * ab.x + ab.y * ab.y;
  if (length_squared <= 0.0001f) {
    return viewport_distance_squared(point, a);
  }

  const float t =
      std::clamp((ap.x * ab.x + ap.y * ab.y) / length_squared, 0.0f, 1.0f);
  const ImVec2 closest{a.x + ab.x * t, a.y + ab.y * t};
  return viewport_distance_squared(point, closest);
}

inline std::optional<ImVec2>
project_world_to_viewport(QVec3 world, const ViewportCameraController &camera,
                          ImVec2 item_min, ImVec2 item_size) {
  (void)item_size;

  const ViewportScreenPoint projected = camera.project_world_point(world);
  if (projected.depth <= camera.state().near_plane ||
      projected.depth >= camera.state().far_plane) {
    return std::nullopt;
  }

  return ImVec2{
      item_min.x + projected.position.x,
      item_min.y + projected.position.y,
  };
}

} // namespace quader
