////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/glm_bridge.hpp"
#include "scene/scene_transform.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace quader {

inline constexpr std::uint32_t kLinearArrayLayoutMinimumCount = 2u;
inline constexpr std::uint32_t kLinearArrayLayoutMaximumCount = 128u;
inline constexpr std::uint32_t kLinearArrayLayoutMaximumElements = 1024u;

/**
 * Describes one source's renderer-neutral linear-array layout.
 */
struct LinearArrayLayoutRequest {
  QMat4 source_world_transform;
  QVec3 span_world = {};
  std::uint32_t count = 4u;
};

/**
 * Returns generated world matrices in start-to-end order.
 *
 * The source at the start position is omitted so this payload can be consumed
 * directly by both preview instancing and transformed-duplicate commit.
 */
struct LinearArrayLayoutResult {
  bool ok = false;
  std::string message;
  std::vector<QMat4> generated_world_transforms;
};

/**
 * Builds one source's linear-array world matrices without mutating editor
 * state.
 */
inline LinearArrayLayoutResult
build_linear_array_layout(const LinearArrayLayoutRequest &request) {
  const auto failure = [](std::string message) {
    return LinearArrayLayoutResult{
        .ok = false,
        .message = std::move(message),
    };
  };
  const bool finite_span = std::isfinite(request.span_world.x) &&
                           std::isfinite(request.span_world.y) &&
                           std::isfinite(request.span_world.z);
  if (!scene_matrix_is_finite(request.source_world_transform) ||
      !finite_span) {
    return failure("Linear Array layout inputs must be finite.");
  }
  if (request.count < kLinearArrayLayoutMinimumCount ||
      request.count > kLinearArrayLayoutMaximumCount) {
    return failure("Linear Array count must be between 2 and 128.");
  }

  LinearArrayLayoutResult result{.ok = true};
  result.generated_world_transforms.reserve(request.count - 1u);
  const glm::mat4 source_world = to_glm(request.source_world_transform);
  for (std::uint32_t index = 1u; index < request.count; ++index) {
    const float factor = static_cast<float>(index) /
                         static_cast<float>(request.count - 1u);
    const QMat4 world = from_glm(
        glm::translate(glm::mat4{1.0f}, to_glm(request.span_world * factor)) *
        source_world);
    if (!scene_matrix_is_finite(world)) {
      return failure("Linear Array generated a non-finite transform.");
    }
    result.generated_world_transforms.push_back(world);
  }
  return result;
}

} // namespace quader
