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
struct BridgePreviewHandleState;

/// Identifies the independent constrained control at either end of a bridge.
enum class BridgePreviewHandlePart { None, Start, End };

/// Shares the projected handle disks between pointer picking and overlay drawing.
struct BridgePreviewHandleLayout final {
  std::array<QVec3, 2> centers = {};
  std::array<QVec2, 2> screen_centers = {};
  std::array<float, 2> world_radii = {};
  std::array<bool, 2> visible = {};
  QVec3 camera_right = {1.0F, 0.0F, 0.0F};
  QVec3 camera_up = {0.0F, 1.0F, 0.0F};
  float radius_pixels = 0.0F;
};

[[nodiscard]] BridgePreviewHandleLayout build_bridge_preview_handle_layout(
    const ViewportCameraController &camera,
    const BridgePreviewHandleState &handle, float raster_scale = 1.0F);

[[nodiscard]] BridgePreviewHandlePart hit_bridge_preview_handle(
    const BridgePreviewHandleLayout &layout, QVec2 pointer);

} // namespace quader
