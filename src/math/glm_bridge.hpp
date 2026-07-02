////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/qmat4.hpp"
#include "math/types/qquat.hpp"
#include "math/types/qtransform.hpp"
#include "math/types.hpp"

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace quader {

inline glm::vec2 to_glm(QVec2 value) { return {value.x, value.y}; }

inline glm::vec3 to_glm(QVec3 value) { return {value.x, value.y, value.z}; }

inline glm::quat to_glm(QQuat value) {
  return {value.w, value.x, value.y, value.z};
}

inline glm::mat4 to_glm(const QMat4 &value) {
  glm::mat4 result{1.0f};
  std::copy(value.values.begin(), value.values.end(), &result[0][0]);
  return result;
}

inline QVec2 from_glm(glm::vec2 value) { return {value.x, value.y}; }

inline QVec3 from_glm(glm::vec3 value) { return {value.x, value.y, value.z}; }

inline QQuat from_glm(glm::quat value) {
  return {value.x, value.y, value.z, value.w};
}

inline QMat4 from_glm(const glm::mat4 &value) {
  QMat4 result;
  const float *raw = &value[0][0];
  std::copy(raw, raw + result.values.size(), result.values.begin());
  return result;
}

inline QMat4 compose_transform(const QTransform &transform) {
  const glm::mat4 translation =
      glm::translate(glm::mat4{1.0f}, to_glm(transform.position));
  const glm::mat4 rotation = glm::mat4_cast(to_glm(transform.rotation));
  const glm::mat4 scale = glm::scale(glm::mat4{1.0f}, to_glm(transform.scale));
  return from_glm(translation * rotation * scale);
}

inline QMat4 inverse(const QMat4& matrix) {
  return from_glm(glm::inverse(to_glm(matrix)));
}

inline QVec3 transform_point(const QMat4 &matrix, QVec3 point) {
  const glm::vec4 result = to_glm(matrix) * glm::vec4{to_glm(point), 1.0f};
  return from_glm(glm::vec3{result} / result.w);
}

inline QVec3 transform_vector(const QMat4 &matrix, QVec3 vector) {
  const glm::vec4 result = to_glm(matrix) * glm::vec4{to_glm(vector), 0.0f};
  return from_glm(glm::vec3{result});
}

} // namespace quader
