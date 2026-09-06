////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <array>
#include <cstdint>

namespace quader {

/// Identifies a cube face's local axis, independent of its world placement.
enum class QuaderPrimitiveCubeAxis { X, Y, Z };

/// Authored shapes that expose live creation controls through the native API.
enum class QuaderPrimitiveCreationShape { Box, Cylinder, Cone, Sphere, Torus };

/// Chooses one movable end or radial boundary, in placement-local axes.
enum class QuaderPrimitiveResizeControl {
  PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ, Radius,
  OuterRadius, InnerRadius
};

/// Read-only projection of the newest selected, editable primitive.
struct QuaderPrimitiveCreationState {
  QuaderModelingObjectId object_id = kInvalidQuaderModelingObjectId;
  QuaderPrimitiveCreationShape shape = QuaderPrimitiveCreationShape::Box;
  QVec3 local_min;
  QVec3 local_max;
  QVec3 world_origin;
  // Transformed local unit vectors, including scale (not normalized).
  QVec3 world_x;
  QVec3 world_y;
  QVec3 world_z;
  std::array<int, 3> subdivisions = {1, 1, 1};
  int side_count = 32;
  bool caps = true;
  int radial_segments = 32;
  int rings = 16;
  bool hemisphere = false;
  int major_segments = 32;
  int minor_segments = 12;
  double tube_radius_ratio = 0.2;
  bool flip_faces = false;
  bool resizing = false;
  std::uint64_t revision = 0;
};

/// Read-only projection of the newest selected, still-editable Box.
struct QuaderPrimitiveCubeState {
  QuaderModelingObjectId object_id = kInvalidQuaderModelingObjectId;
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
};

} // namespace quader
