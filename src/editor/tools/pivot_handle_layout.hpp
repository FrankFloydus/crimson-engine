////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <array>

namespace quader {

class ViewportCameraController;
struct PivotHandleState;

/// Semantic free-placement and constrained-axis targets shared by draw and pick.
enum class PivotHandlePart { None, Center, X, Y, Z };

/// The transform owner supplies the same world/local frame to input and drawing.
struct PivotHandleLayoutOptions final {
  float icon_size_pixels = 20.0f;
  QVec3 world_x = {1.0f, 0.0f, 0.0f};
  QVec3 world_y = {0.0f, 1.0f, 0.0f};
  QVec3 world_z = {0.0f, 0.0f, 1.0f};
};

/// A visible compact shaft and tip together with its exact screen hit capsule.
struct PivotAxisHandle final {
  PivotHandlePart part = PivotHandlePart::None;
  QVec3 world_direction;
  QVec3 world_start;
  QVec3 world_tip;
  QVec2 screen_start;
  QVec2 screen_tip;
  float radius_pixels = 0.0f;
  float tip_world_units_per_pixel = 0.0f;
  bool visible = false;
};

/// Exact camera-facing icon and compact axes consumed by drawing and picking.
struct PivotHandleLayout final {
  bool valid = false;
  bool persistent = false;
  QVec3 center;
  QVec2 center_screen;
  QVec3 camera_right = {1.0f, 0.0f, 0.0f};
  QVec3 camera_up = {0.0f, 1.0f, 0.0f};
  float world_units_per_pixel = 0.0f;
  float icon_radius_pixels = 0.0f;
  float disc_radius_pixels = 0.0f;
  float ring_inner_radius_pixels = 0.0f;
  float raster_scale = 1.0f;
  std::array<PivotAxisHandle, 3> axes;
};

[[nodiscard]] PivotHandleLayout build_pivot_handle_layout(
    const ViewportCameraController &camera, const PivotHandleState &handle,
    float raster_scale = 1.0f,
    const PivotHandleLayoutOptions &options = {});

/// The whole visible icon is the free-placement target, including its dark gap.
[[nodiscard]] PivotHandlePart hit_pivot_handle(
    const PivotHandleLayout &layout, QVec2 pointer);

} // namespace quader
