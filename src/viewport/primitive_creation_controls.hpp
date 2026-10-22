////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "viewport/bounds_resize_controls.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace quader {

/// Shapes that currently expose creation controls in the viewport.
enum class ViewportPrimitiveCreationShape { Box, Cylinder, Cone, Sphere, Torus, Stairs };
/// Sphere topology used by the projected creation controls.
enum class ViewportSphereType { Uv, Quad };
/// Semantic resize intent sent from a projected dot to the primitive owner.
enum class ViewportPrimitiveResizeControl {
  PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ, Radius,
  OuterRadius, InnerRadius
};

/** Immutable creation state supplied to native tools without a public API dependency. */
struct ViewportPrimitiveCreationState final {
  std::int32_t object_id = -1;
  QVec3 local_min;
  QVec3 local_max;
  QVec3 world_origin;
  // Transformed local unit vectors, including scale (not normalized).
  QVec3 world_x;
  QVec3 world_y;
  QVec3 world_z;
  std::array<int, 3> subdivisions = {1, 1, 1};
  bool flip_faces = false;
  bool resizing = false;
  std::uint64_t revision = 0;
  ViewportPrimitiveCreationShape shape = ViewportPrimitiveCreationShape::Box;
  int side_count = 32;
  bool caps = true;
  int radial_segments = 32;
  int rings = 16;
  bool hemisphere = false;
  int major_segments = 32;
  int minor_segments = 12;
  double tube_radius_ratio = 0.2;
  ViewportSphereType sphere_type = ViewportSphereType::Uv;
  int sphere_subdivision = 3;
};

/** One projected resize dot or segment button. */
struct PrimitiveCreationControl final {
  QVec3 world_position;
  QVec2 screen_position;
  float depth = 0.0f;
  float radius_pixels = 0.0f;
  bool visible = false;
  bool enabled = true;
  bool subdivision_decrement = false;
  QVec3 resize_axis = {1.0f, 0.0f, 0.0f};
  ViewportPrimitiveResizeControl resize_control = ViewportPrimitiveResizeControl::PositiveX;
  int segment_count = 1;
  int minimum_segments = 1;
  int maximum_segments = 128;
};

/** Six bounds handles share the common layout. Legacy segment slots stay hidden. */
struct PrimitiveCreationControlsFrame final {
  BoundsResizeFrame bounds;
  std::array<PrimitiveCreationControl, 9> controls;
  int hovered = -1;
  int active = -1;
  bool visible = false;
  std::uint64_t revision = 0;
  std::array<QVec3, 65> highlight_points{};
  std::size_t highlight_point_count = 0;
  std::string_view label_key;
  /// World-space distance unless label_is_count; the presenter converts to map units.
  double label_value = 0.0;
  bool label_is_count = false;
  QVec2 label_position;
};

} // namespace quader
