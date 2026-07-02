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

namespace quader {

inline QMat4 scene_transform_identity_matrix() { return QMat4{}; }

inline QMat4 scene_transform_multiply(const QMat4 &left, const QMat4 &right) {
  return from_glm(to_glm(left) * to_glm(right));
}

inline TransformComponent
transform_component_from_matrix(const QMat4 &matrix,
                                QVec3 reference_euler_radians = {}) {
  glm::vec3 scale{1.0f};
  glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 translation{0.0f};
  glm::vec3 skew{0.0f};
  glm::vec4 perspective{0.0f};
  if (!glm::decompose(to_glm(matrix), scale, orientation, translation, skew,
                      perspective)) {
    return TransformComponent{};
  }

  orientation = glm::normalize(orientation);
  return TransformComponent{
      .position = from_glm(translation),
      .rotation_euler = equivalent_euler_radians_closest_to(
          from_glm(glm::eulerAngles(orientation)), reference_euler_radians),
      .scale = from_glm(scale),
  };
}

inline QMat4 scene_local_matrix_from_world_matrix(QMat4 parent_world_matrix,
                                                  QMat4 world_matrix) {
  return scene_transform_multiply(inverse(parent_world_matrix), world_matrix);
}

} // namespace quader
