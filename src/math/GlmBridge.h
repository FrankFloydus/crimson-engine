////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/QMat4.h"
#include "math/types/QQuat.h"
#include "math/types/QTransform.h"
#include "math/Types.h"

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace quader {

inline glm::vec2 toGlm(QVec2 value) {
    return {value.x, value.y};
}

inline glm::vec3 toGlm(QVec3 value) {
    return {value.x, value.y, value.z};
}

inline glm::quat toGlm(QQuat value) {
    return {value.w, value.x, value.y, value.z};
}

inline glm::mat4 toGlm(const QMat4& value) {
    glm::mat4 result{1.0f};
    std::copy(value.values.begin(), value.values.end(), &result[0][0]);
    return result;
}

inline QVec2 fromGlm(glm::vec2 value) {
    return {value.x, value.y};
}

inline QVec3 fromGlm(glm::vec3 value) {
    return {value.x, value.y, value.z};
}

inline QQuat fromGlm(glm::quat value) {
    return {value.x, value.y, value.z, value.w};
}

inline QMat4 fromGlm(const glm::mat4& value) {
    QMat4 result;
    const float* raw = &value[0][0];
    std::copy(raw, raw + result.values.size(), result.values.begin());
    return result;
}

inline QMat4 composeTransform(const QTransform& transform) {
    const glm::mat4 translation = glm::translate(glm::mat4{1.0f}, toGlm(transform.position));
    const glm::mat4 rotation = glm::mat4_cast(toGlm(transform.rotation));
    const glm::mat4 scale = glm::scale(glm::mat4{1.0f}, toGlm(transform.scale));
    return fromGlm(translation * rotation * scale);
}

inline QMat4 inverse(const QMat4& matrix) {
    return fromGlm(glm::inverse(toGlm(matrix)));
}

inline QVec3 transformPoint(const QMat4& matrix, QVec3 point) {
    const glm::vec4 result = toGlm(matrix) * glm::vec4{toGlm(point), 1.0f};
    return fromGlm(glm::vec3{result} / result.w);
}

inline QVec3 transformVector(const QMat4& matrix, QVec3 vector) {
    const glm::vec4 result = toGlm(matrix) * glm::vec4{toGlm(vector), 0.0f};
    return fromGlm(glm::vec3{result});
}

} // namespace quader
