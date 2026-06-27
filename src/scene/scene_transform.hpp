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

inline QMat4 sceneTransformIdentityMatrix() {
    return QMat4{};
}

inline QMat4 sceneTransformMultiply(const QMat4& left, const QMat4& right) {
    return fromGlm(toGlm(left) * toGlm(right));
}

inline TransformComponent transformComponentFromMatrix(
        const QMat4& matrix,
        QVec3 referenceEulerRadians = {}) {
    glm::vec3 scale{1.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 translation{0.0f};
    glm::vec3 skew{0.0f};
    glm::vec4 perspective{0.0f};
    if (!glm::decompose(toGlm(matrix), scale, orientation, translation, skew, perspective)) {
        return TransformComponent{};
    }

    orientation = glm::normalize(orientation);
    return TransformComponent{
            .position = fromGlm(translation),
            .rotationEuler = equivalentEulerRadiansClosestTo(fromGlm(glm::eulerAngles(orientation)), referenceEulerRadians),
            .scale = fromGlm(scale),
    };
}

inline QMat4 sceneLocalMatrixFromWorldMatrix(QMat4 parentWorldMatrix, QMat4 worldMatrix) {
    return sceneTransformMultiply(inverse(parentWorldMatrix), worldMatrix);
}

} // namespace quader
