////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/glm_bridge.hpp"
#include "scene/components.hpp"

namespace quader {

inline QVec3 localPivotForWorldPoint(const TransformComponent& transform, QVec3 worldPivot) {
    return transformPoint(inverse(composeTransform(transform.spatialTransform())), worldPivot);
}

inline void pinLocalPivotToWorldPoint(TransformComponent& transform, QVec3 localPivot, QVec3 worldPivot) {
    QTransform spatial = transform.spatialTransform();
    spatial.position = {};
    transform.position = worldPivot - transformPoint(composeTransform(spatial), localPivot);
}

inline float radiansNearReference(float radians, float reference) {
    constexpr float TwoPi = 6.28318530717958647692f;
    if (!std::isfinite(radians) || !std::isfinite(reference)) {
        return radians;
    }
    return radians + TwoPi * std::round((reference - radians) / TwoPi);
}

inline float positiveRadians(float radians) {
    constexpr float TwoPi = 6.28318530717958647692f;
    if (!std::isfinite(radians)) {
        return radians;
    }
    float value = std::fmod(radians, TwoPi);
    if (value < 0.0f) {
        value += TwoPi;
    }
    if (std::abs(value) <= 0.00001f || std::abs(value - TwoPi) <= 0.00001f) {
        return 0.0f;
    }
    return value;
}

inline QVec3 positiveEulerRadians(QVec3 eulerRadians) {
    return {
            positiveRadians(eulerRadians.x),
            positiveRadians(eulerRadians.y),
            positiveRadians(eulerRadians.z),
    };
}

inline QVec3 equivalentEulerRadiansClosestTo(QVec3 eulerRadians, QVec3 referenceEulerRadians) {
    constexpr float Pi = 3.14159265358979323846f;
    const auto normalizeCandidate = [&](QVec3 candidate) {
        return QVec3{
                radiansNearReference(candidate.x, referenceEulerRadians.x),
                radiansNearReference(candidate.y, referenceEulerRadians.y),
                radiansNearReference(candidate.z, referenceEulerRadians.z),
        };
    };
    const auto score = [&](QVec3 candidate) {
        const QVec3 delta = candidate - referenceEulerRadians;
        return dot(delta, delta);
    };

    const QVec3 primary = normalizeCandidate(eulerRadians);
    const QVec3 alternate = normalizeCandidate({
            eulerRadians.x + Pi,
            Pi - eulerRadians.y,
            eulerRadians.z + Pi,
    });
    return score(alternate) < score(primary) ? alternate : primary;
}

inline QVec3 eulerRadiansAfterWorldRotationDelta(QVec3 currentEulerRadians, QVec3 worldRotationDeltaRadians) {
    const glm::quat delta = glm::quat(toGlm(worldRotationDeltaRadians));
    const glm::quat current = toGlm(quatFromEulerRadians(currentEulerRadians));
    return equivalentEulerRadiansClosestTo(
            fromGlm(glm::eulerAngles(glm::normalize(delta * current))),
            currentEulerRadians);
}

inline void applyWorldRotationDeltaAroundPivot(
        TransformComponent& transform,
        QVec3 worldRotationDeltaRadians,
        QVec3 localPivot,
        QVec3 worldPivot) {
    transform.rotationEuler = eulerRadiansAfterWorldRotationDelta(transform.rotationEuler, worldRotationDeltaRadians);
    pinLocalPivotToWorldPoint(transform, localPivot, worldPivot);
}

} // namespace quader
