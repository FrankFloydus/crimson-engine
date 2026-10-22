////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <array>
#include <cstdint>

namespace quader {

/// Bounds in the captured transform frame; basis vectors include affine scale.
struct ViewportBoundsResizeState final {
  QVec3 local_min;
  QVec3 local_max;
  QVec3 world_origin;
  QVec3 world_x = {1.0f, 0.0f, 0.0f};
  QVec3 world_y = {0.0f, 1.0f, 0.0f};
  QVec3 world_z = {0.0f, 0.0f, 1.0f};
  std::uint64_t revision = 0;
};

/// One immutable target shared by drawing, hit testing, and press capture.
struct BoundsResizeHandle final {
  int axis = 0;
  bool positive = true;
  QVec3 world_position;
  QVec3 opposite_world_position;
  QVec2 screen_position;
  float depth = 0.0f;
  float radius_pixels = 0.0f;
  bool visible = false;
  bool enabled = true;
};

/// Slots are +X, -X, +Y, -Y, +Z, -Z. Corner indices use XYZ bit masks.
struct BoundsResizeFrame final {
  std::array<BoundsResizeHandle, 6> handles;
  std::array<QVec3, 8> corners;
  std::array<QVec3, 3> guide_starts;
  std::array<QVec3, 3> guide_ends;
  int hovered = -1;
  int active = -1;
  bool visible = false;
  float raster_scale = 1.0f;
  std::uint64_t revision = 0;
};

} // namespace quader
