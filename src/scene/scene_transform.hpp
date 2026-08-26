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

namespace scene_transform_detail {

inline bool scene_rotation_quaternion_is_finite(const glm::dquat &value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}

inline std::optional<glm::dquat>
scene_rotation_quaternion_from_normalized_basis(const glm::dmat3 &basis) {
  const double m00 = basis[0][0];
  const double m11 = basis[1][1];
  const double m22 = basis[2][2];
  double w = 0.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  const auto signed_root = [](double trace,
                              bool negative) -> std::optional<double> {
    if (!std::isfinite(trace) || trace <= 0.0) {
      return std::nullopt;
    }
    const double root = 2.0 * std::sqrt(trace);
    return negative ? -root : root;
  };

  std::optional<double> scale;
  if (m22 < 0.0) {
    if (m00 > m11) {
      scale = signed_root(1.0 + m00 - m11 - m22,
                          basis[1][2] < basis[2][1]);
      if (!scale.has_value()) {
        return std::nullopt;
      }
      x = 0.25 * *scale;
      w = (basis[1][2] - basis[2][1]) / *scale;
      y = (basis[0][1] + basis[1][0]) / *scale;
      z = (basis[2][0] + basis[0][2]) / *scale;
    } else {
      scale = signed_root(1.0 - m00 + m11 - m22,
                          basis[2][0] < basis[0][2]);
      if (!scale.has_value()) {
        return std::nullopt;
      }
      y = 0.25 * *scale;
      w = (basis[2][0] - basis[0][2]) / *scale;
      x = (basis[0][1] + basis[1][0]) / *scale;
      z = (basis[1][2] + basis[2][1]) / *scale;
    }
  } else if (m00 < -m11) {
    scale = signed_root(1.0 - m00 - m11 + m22,
                        basis[0][1] < basis[1][0]);
    if (!scale.has_value()) {
      return std::nullopt;
    }
    z = 0.25 * *scale;
    w = (basis[0][1] - basis[1][0]) / *scale;
    x = (basis[2][0] + basis[0][2]) / *scale;
    y = (basis[1][2] + basis[2][1]) / *scale;
  } else {
    scale = signed_root(1.0 + m00 + m11 + m22, false);
    if (!scale.has_value()) {
      return std::nullopt;
    }
    w = 0.25 * *scale;
    x = (basis[1][2] - basis[2][1]) / *scale;
    y = (basis[2][0] - basis[0][2]) / *scale;
    z = (basis[0][1] - basis[1][0]) / *scale;
  }

  glm::dquat rotation{w, x, y, z};
  if (!scene_rotation_quaternion_is_finite(rotation) ||
      glm::dot(rotation, rotation) <= 0.0) {
    return std::nullopt;
  }
  return glm::normalize(rotation);
}

} // namespace scene_transform_detail

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

/**
 * Extracts the finite, right-handed rotation represented by a world matrix.
 *
 * Authored non-uniform scale is removed before quaternion extraction. A
 * mirrored basis is made right-handed deterministically by reversing all
 * three normalized axes, preserving the established pivot-transfer behavior.
 */
inline std::optional<glm::mat3>
try_scene_pure_world_rotation_basis(const QMat4 &world_matrix) {
  if (!scene_matrix_is_finite(world_matrix)) {
    return std::nullopt;
  }

  const glm::mat4 world = to_glm(world_matrix);
  glm::dmat3 normalized_basis;
  for (glm::length_t column = 0; column < 3; ++column) {
    const glm::dvec3 basis_vector{world[column]};
    const double basis_length = glm::length(basis_vector);
    if (!std::isfinite(basis_length) || basis_length == 0.0) {
      return std::nullopt;
    }
    normalized_basis[column] = basis_vector / basis_length;
  }

  const double determinant = glm::determinant(normalized_basis);
  if (!std::isfinite(determinant) || determinant == 0.0) {
    return std::nullopt;
  }
  if (determinant < 0.0) {
    normalized_basis = -normalized_basis;
  }

  const std::optional<glm::dquat> orientation =
      scene_transform_detail::scene_rotation_quaternion_from_normalized_basis(
          normalized_basis);
  if (!orientation.has_value()) {
    return std::nullopt;
  }

  const glm::mat3 rotation = glm::mat3(glm::mat3_cast(*orientation));
  for (glm::length_t column = 0; column < 3; ++column) {
    for (glm::length_t row = 0; row < 3; ++row) {
      if (!std::isfinite(rotation[column][row])) {
        return std::nullopt;
      }
    }
  }
  return rotation;
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
