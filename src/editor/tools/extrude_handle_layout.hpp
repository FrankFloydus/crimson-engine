////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <quader/modeling/types.hpp>

namespace quader {

class ViewportCameraController;
struct ExtrudeHandleState;

/// Identifies the disk that controls the current extrusion amount.
enum class ExtrudeHandlePart { None, NewExtrusion };

/// Shares the exact projected affordances between hit testing and overlay drawing.
struct ExtrudeHandleLayout final {
  bool valid = false;
  modeling::FaceExtrudeMode mode = modeling::FaceExtrudeMode::Region;
  QVec3 pivot = {};
  QVec3 button_center = {};
  QVec3 camera_right = {1.0F, 0.0F, 0.0F};
  QVec3 camera_up = {0.0F, 1.0F, 0.0F};
  QVec2 pivot_screen = {};
  QVec2 button_center_screen = {};
  float world_units_per_pixel = 0.0F;
  float button_world_radius = 0.0F;
  float button_radius_pixels = 0.0F;
  float raster_scale = 1.0F;
};

/// Sizes the disk at the pivot and preserves perspective along its world-normal stem.
[[nodiscard]] ExtrudeHandleLayout build_extrude_handle_layout(
    const ViewportCameraController &camera, const ExtrudeHandleState &handle,
    float raster_scale = 1.0F);

/// Hits the full new-extrusion disk; the stem is decorative.
[[nodiscard]] ExtrudeHandlePart hit_extrude_handle(
    const ExtrudeHandleLayout &layout, QVec2 pointer);

} // namespace quader
