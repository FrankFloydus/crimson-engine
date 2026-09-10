////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/radial_array_mode.hpp"
#include "scene/scene_transform.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace quader {

inline constexpr std::uint32_t kRadialArrayLayoutMinimumCopies = 2u;
inline constexpr std::uint32_t kRadialArrayLayoutMaximumCopies = 128u;
inline constexpr std::uint32_t kRadialArrayLayoutMaximumElements = 1024u;
inline constexpr double kRadialArrayLayoutMaximumRepeatSpacing = 16384.0;

/**
 * Describes one source's renderer-neutral radial-array layout.
 */
struct RadialArrayLayoutRequest {
  QMat4 source_world_transform;
  QVec3 source_pivot_world = {};
  QVec3 center_world = {};
  QVec3 plane_normal_world = {0.0f, 1.0f, 0.0f};
  double degrees = 360.0;
  std::uint32_t copies = 8u;
  std::uint32_t repeats = 1u;
  double repeat_spacing = 1.0;
  RadialArrayMode mode = RadialArrayMode::Total;
};

/**
 * Returns generated world matrices in deterministic ring/angle order.
 *
 * The source itself (base ring, angle zero) is intentionally omitted so the
 * same payload can be consumed directly by viewport preview instancing and by
 * the duplicate commit path.
 */
struct RadialArrayLayoutResult {
  bool ok = false;
  std::string message;
  QVec3 normalized_plane_normal_world = {0.0f, 1.0f, 0.0f};
  QVec3 outward_direction_world = {1.0f, 0.0f, 0.0f};
  std::vector<QMat4> generated_world_transforms;
};

namespace radial_array_layout_detail {

inline bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

inline QVec3 projected_onto_plane(QVec3 value, QVec3 unit_normal) {
  return value - unit_normal * dot(value, unit_normal);
}

inline QVec3 normalized_projected_direction(QVec3 value, QVec3 unit_normal) {
  const QVec3 projected = projected_onto_plane(value, unit_normal);
  constexpr float kDirectionEpsilon = 0.000001f;
  const float projected_length = length(projected);
  return std::isfinite(projected_length) &&
                 projected_length > kDirectionEpsilon
             ? projected / projected_length
             : QVec3{};
}

inline QVec3 deterministic_perpendicular(QVec3 unit_normal) {
  const QVec3 reference =
      std::abs(unit_normal.y) < 0.9f ? QVec3{0.0f, 1.0f, 0.0f}
                                    : QVec3{1.0f, 0.0f, 0.0f};
  return normalize_or_zero(cross(unit_normal, reference));
}

inline QVec3 source_outward_direction(const RadialArrayLayoutRequest &request,
                                      QVec3 unit_normal) {
  QVec3 direction = normalized_projected_direction(
      request.source_pivot_world - request.center_world, unit_normal);
  if (length_squared(direction) > 0.0f) {
    return direction;
  }

  direction = normalized_projected_direction(
      transform_vector(request.source_world_transform,
                       QVec3{1.0f, 0.0f, 0.0f}),
      unit_normal);
  if (length_squared(direction) > 0.0f) {
    return direction;
  }

  direction = normalized_projected_direction(
      transform_vector(request.source_world_transform,
                       QVec3{0.0f, 1.0f, 0.0f}),
      unit_normal);
  if (length_squared(direction) > 0.0f) {
    return direction;
  }
  return deterministic_perpendicular(unit_normal);
}

inline double angle_degrees(std::uint32_t index, std::uint32_t copies,
                            double degrees, RadialArrayMode mode) {
  if (mode == RadialArrayMode::Spacing) {
    return static_cast<double>(index) * degrees;
  }
  constexpr double kFullCircleDegrees = 360.0;
  constexpr double kFullCircleEpsilon = 0.000001;
  const double divisor =
      std::abs(degrees - kFullCircleDegrees) <= kFullCircleEpsilon
          ? static_cast<double>(copies)
          : static_cast<double>(copies - 1u);
  return static_cast<double>(index) * degrees / divisor;
}

} // namespace radial_array_layout_detail

/**
 * Builds one source's radial-array world matrices without mutating editor
 * state.
 */
inline RadialArrayLayoutResult
build_radial_array_layout(const RadialArrayLayoutRequest &request) {
  const auto failure = [](std::string message) {
    return RadialArrayLayoutResult{
        .ok = false,
        .message = std::move(message),
    };
  };

  if (!scene_matrix_is_finite(request.source_world_transform) ||
      !radial_array_layout_detail::finite_vector(request.source_pivot_world) ||
      !radial_array_layout_detail::finite_vector(request.center_world) ||
      !radial_array_layout_detail::finite_vector(request.plane_normal_world)) {
    return failure("Radial Array layout inputs must be finite.");
  }
  if (!std::isfinite(request.degrees) || request.degrees < 0.0 ||
      request.degrees > 360.0) {
    return failure("Radial Array degrees must be between 0 and 360.");
  }
  if (request.mode != RadialArrayMode::Total &&
      request.mode != RadialArrayMode::Spacing) {
    return failure("Radial Array angle mode is invalid.");
  }
  if (request.copies < kRadialArrayLayoutMinimumCopies ||
      request.copies > kRadialArrayLayoutMaximumCopies) {
    return failure("Radial Array copies must be between 2 and 128.");
  }
  if (request.repeats == 0u ||
      request.repeats >
          kRadialArrayLayoutMaximumElements / request.copies) {
    return failure("Radial Array repeat count is too large.");
  }
  if (!std::isfinite(request.repeat_spacing) || request.repeat_spacing < 0.0 ||
      request.repeat_spacing > kRadialArrayLayoutMaximumRepeatSpacing) {
    return failure("Radial Array repeat spacing must be between 0 and 16384.");
  }

  const float normal_length = length(request.plane_normal_world);
  if (!std::isfinite(normal_length) || normal_length <= 0.000001f) {
    return failure("Radial Array plane normal must be nonzero.");
  }
  const QVec3 normal = request.plane_normal_world / normal_length;
  const QVec3 outward =
      radial_array_layout_detail::source_outward_direction(request, normal);
  if (!radial_array_layout_detail::finite_vector(outward) ||
      length_squared(outward) <= 0.0f) {
    return failure("Radial Array could not resolve an outward direction.");
  }

  const std::size_t element_count =
      static_cast<std::size_t>(request.copies) * request.repeats;
  RadialArrayLayoutResult result{
      .ok = true,
      .normalized_plane_normal_world = normal,
      .outward_direction_world = outward,
  };
  result.generated_world_transforms.reserve(element_count - 1u);

  const glm::mat4 source_world = to_glm(request.source_world_transform);
  const glm::mat4 to_center = glm::translate(
      glm::mat4{1.0f}, to_glm(request.center_world));
  const glm::mat4 from_center = glm::translate(
      glm::mat4{1.0f}, -to_glm(request.center_world));
  for (std::uint32_t ring = 0u; ring < request.repeats; ++ring) {
    const float ring_distance = static_cast<float>(
        static_cast<double>(ring) * request.repeat_spacing);
    const glm::mat4 ring_offset = glm::translate(
        glm::mat4{1.0f}, to_glm(outward * ring_distance));
    for (std::uint32_t copy = 0u; copy < request.copies; ++copy) {
      if (ring == 0u && copy == 0u) {
        continue;
      }
      const double degrees = radial_array_layout_detail::angle_degrees(
          copy, request.copies, request.degrees, request.mode);
      const glm::mat4 rotation = glm::rotate(
          glm::mat4{1.0f}, glm::radians(static_cast<float>(degrees)),
          to_glm(normal));
      const QMat4 world = from_glm(to_center * rotation * from_center *
                                   ring_offset * source_world);
      if (!scene_matrix_is_finite(world)) {
        return failure("Radial Array generated a non-finite transform.");
      }
      result.generated_world_transforms.push_back(world);
    }
  }
  return result;
}

} // namespace quader
