////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/glm_bridge.hpp"
#include "scene/transform_pivot.hpp"

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace quader {

inline QMat4 scene_transform_identity_matrix() { return QMat4{}; }

inline QMat4 scene_transform_multiply(const QMat4 &left, const QMat4 &right) {
  return from_glm(to_glm(left) * to_glm(right));
}

inline bool scene_transform_is_finite(const TransformComponent &transform) {
  const auto finite = [](QVec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };
  return finite(transform.position) && finite(transform.rotation_euler) &&
         finite(transform.scale);
}

inline bool scene_matrix_is_finite(const QMat4 &matrix) {
  return std::all_of(matrix.values.begin(), matrix.values.end(),
                     [](float value) { return std::isfinite(value); });
}

inline glm::dmat4 scene_matrix_to_double(const QMat4 &matrix) {
  glm::dmat4 result{1.0};
  for (glm::length_t column = 0; column < 4; ++column) {
    for (glm::length_t row = 0; row < 4; ++row) {
      result[column][row] = static_cast<double>(
          matrix.values[static_cast<std::size_t>(column * 4 + row)]);
    }
  }
  return result;
}

inline std::optional<QMat4>
try_scene_matrix_inverse(const QMat4 &matrix) {
  if (!scene_matrix_is_finite(matrix)) {
    return std::nullopt;
  }

  const glm::dmat4 inverse_matrix = glm::inverse(scene_matrix_to_double(matrix));
  QMat4 result;
  constexpr double kMaximumFloat =
      static_cast<double>(std::numeric_limits<float>::max());
  for (glm::length_t column = 0; column < 4; ++column) {
    for (glm::length_t row = 0; row < 4; ++row) {
      const double value = inverse_matrix[column][row];
      if (!std::isfinite(value) || std::abs(value) > kMaximumFloat) {
        return std::nullopt;
      }
      result.values[static_cast<std::size_t>(column * 4 + row)] =
          static_cast<float>(value);
    }
  }
  return scene_matrix_is_finite(result) ? std::optional<QMat4>(result)
                                        : std::nullopt;
}

inline std::optional<TransformComponent> try_transform_component_from_matrix(
    const QMat4 &matrix, QVec3 reference_euler_radians = {}) {
  if (!scene_matrix_is_finite(matrix)) {
    return std::nullopt;
  }

  glm::dmat4 conditioned_matrix = scene_matrix_to_double(matrix);
  const double linear_determinant =
      glm::determinant(glm::dmat3{conditioned_matrix});
  if (!std::isfinite(linear_determinant) || linear_determinant == 0.0) {
    return std::nullopt;
  }

  // GLM rejects valid float transforms when the absolute 3x3 determinant is
  // below its fixed epsilon. Normalize that determinant before decomposition,
  // then restore the authored scale. This keeps Quader's valid 0.001 scale
  // floor representable without accepting singular matrices.
  const double linear_conditioning =
      1.0 / std::cbrt(std::abs(linear_determinant));
  if (!std::isfinite(linear_conditioning) || linear_conditioning <= 0.0) {
    return std::nullopt;
  }
  for (glm::length_t column = 0; column < 3; ++column) {
    for (glm::length_t row = 0; row < 3; ++row) {
      conditioned_matrix[column][row] *= linear_conditioning;
    }
  }

  glm::dvec3 scale{1.0};
  glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
  glm::dvec3 translation{0.0};
  glm::dvec3 skew{0.0};
  glm::dvec4 perspective{0.0};
  const auto finite_vec3 = [](const glm::dvec3 &value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };
  const auto finite_vec4 = [](const glm::dvec4 &value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
  };
  const auto finite_quat = [](const glm::dquat &value) {
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
  };
  if (!glm::decompose(conditioned_matrix, scale, orientation, translation,
                      skew, perspective) ||
      !finite_vec3(scale) || !finite_vec3(translation) ||
      !finite_vec3(skew) || !finite_vec4(perspective) ||
      !finite_quat(orientation)) {
    return std::nullopt;
  }

  scale /= linear_conditioning;
  const double orientation_length = glm::length(orientation);
  if (!finite_vec3(scale) || !std::isfinite(orientation_length) ||
      orientation_length == 0.0) {
    return std::nullopt;
  }
  orientation = glm::normalize(orientation);
  TransformComponent result{
      .position = from_glm(glm::vec3{translation}),
      .rotation_euler = equivalent_euler_radians_closest_to(
          from_glm(glm::vec3{glm::eulerAngles(orientation)}),
          reference_euler_radians),
      .scale = from_glm(glm::vec3{scale}),
  };
  if (!scene_transform_is_finite(result)) {
    return std::nullopt;
  }

  return result;
}

inline std::optional<TransformComponent>
try_scene_local_transform_preserving_world(
    const QMat4 &parent_world_matrix, const QMat4 &child_world_matrix,
    const TransformComponent &current_local_transform) {
  if (!scene_transform_is_finite(current_local_transform) ||
      !scene_matrix_is_finite(child_world_matrix)) {
    return std::nullopt;
  }
  const std::optional<QMat4> parent_inverse =
      try_scene_matrix_inverse(parent_world_matrix);
  if (!parent_inverse.has_value()) {
    return std::nullopt;
  }

  std::optional<TransformComponent> result =
      try_transform_component_from_matrix(
          scene_transform_multiply(*parent_inverse, child_world_matrix),
          current_local_transform.rotation_euler);
  if (result.has_value()) {
    result->custom_pivot = current_local_transform.custom_pivot;
  }
  return result;
}

} // namespace quader
