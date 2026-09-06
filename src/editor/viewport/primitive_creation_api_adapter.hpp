////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_primitive_creation_types.hpp"
#include "viewport/primitive_creation_controls.hpp"

#include <optional>

namespace quader {

/** Converts public creation DTOs at the editor viewport boundary, outside native tools. */
class ViewportPrimitiveCreationApiAdapter final {
public:
  [[nodiscard]] static ViewportPrimitiveCreationState
  from_api(const QuaderPrimitiveCreationState &primitive) {
    return {
        .object_id = primitive.object_id,
        .local_min = primitive.local_min,
        .local_max = primitive.local_max,
        .world_origin = primitive.world_origin,
        .world_x = primitive.world_x,
        .world_y = primitive.world_y,
        .world_z = primitive.world_z,
        .subdivisions = primitive.subdivisions,
        .flip_faces = primitive.flip_faces,
        .resizing = primitive.resizing,
        .revision = primitive.revision,
        .shape = from_api(primitive.shape),
        .side_count = primitive.side_count,
        .caps = primitive.caps,
        .radial_segments = primitive.radial_segments,
        .rings = primitive.rings,
        .hemisphere = primitive.hemisphere,
        .major_segments = primitive.major_segments,
        .minor_segments = primitive.minor_segments,
        .tube_radius_ratio = primitive.tube_radius_ratio,
    };
  }

  [[nodiscard]] static std::optional<ViewportPrimitiveCreationState>
  from_api(const std::optional<QuaderPrimitiveCreationState> &primitive) {
    return primitive ? std::optional<ViewportPrimitiveCreationState>{from_api(*primitive)}
                     : std::nullopt;
  }

  [[nodiscard]] static QuaderPrimitiveResizeControl
  to_api(ViewportPrimitiveResizeControl control) {
    switch (control) {
    case ViewportPrimitiveResizeControl::PositiveX: return QuaderPrimitiveResizeControl::PositiveX;
    case ViewportPrimitiveResizeControl::NegativeX: return QuaderPrimitiveResizeControl::NegativeX;
    case ViewportPrimitiveResizeControl::PositiveY: return QuaderPrimitiveResizeControl::PositiveY;
    case ViewportPrimitiveResizeControl::NegativeY: return QuaderPrimitiveResizeControl::NegativeY;
    case ViewportPrimitiveResizeControl::PositiveZ: return QuaderPrimitiveResizeControl::PositiveZ;
    case ViewportPrimitiveResizeControl::NegativeZ: return QuaderPrimitiveResizeControl::NegativeZ;
    case ViewportPrimitiveResizeControl::Radius: return QuaderPrimitiveResizeControl::Radius;
    case ViewportPrimitiveResizeControl::OuterRadius: return QuaderPrimitiveResizeControl::OuterRadius;
    case ViewportPrimitiveResizeControl::InnerRadius: return QuaderPrimitiveResizeControl::InnerRadius;
    }
    return static_cast<QuaderPrimitiveResizeControl>(control);
  }

  [[nodiscard]] static ViewportPrimitiveCreationShape
  from_api(QuaderPrimitiveCreationShape shape) {
    switch (shape) {
    case QuaderPrimitiveCreationShape::Box: return ViewportPrimitiveCreationShape::Box;
    case QuaderPrimitiveCreationShape::Cylinder: return ViewportPrimitiveCreationShape::Cylinder;
    case QuaderPrimitiveCreationShape::Cone: return ViewportPrimitiveCreationShape::Cone;
    case QuaderPrimitiveCreationShape::Sphere: return ViewportPrimitiveCreationShape::Sphere;
    case QuaderPrimitiveCreationShape::Torus: return ViewportPrimitiveCreationShape::Torus;
    }
    return ViewportPrimitiveCreationShape::Box;
  }

  [[nodiscard]] static ViewportPrimitiveCreationState
  from_api(const QuaderPrimitiveCubeState &cube) {
    return {
        .object_id = cube.object_id,
        .local_min = cube.local_min,
        .local_max = cube.local_max,
        .world_origin = cube.world_origin,
        .world_x = cube.world_x,
        .world_y = cube.world_y,
        .world_z = cube.world_z,
        .subdivisions = cube.subdivisions,
        .flip_faces = cube.flip_faces,
        .resizing = cube.resizing,
        .revision = cube.revision,
    };
  }

  [[nodiscard]] static std::optional<ViewportPrimitiveCreationState>
  from_api(const std::optional<QuaderPrimitiveCubeState> &cube) {
    return cube.has_value()
               ? std::optional<ViewportPrimitiveCreationState>{from_api(*cube)}
               : std::nullopt;
  }

};

} // namespace quader
