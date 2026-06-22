////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/Components.h"

namespace quader {

inline Vec3 localPivotForWorldPoint(const TransformComponent& transform, Vec3 worldPivot) {
    return transformPoint(inverse(composeTransform(transform.spatialTransform())), worldPivot);
}

inline void pinLocalPivotToWorldPoint(TransformComponent& transform, Vec3 localPivot, Vec3 worldPivot) {
    Transform spatial = transform.spatialTransform();
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

inline Vec3 positiveEulerRadians(Vec3 eulerRadians) {
    return {
            positiveRadians(eulerRadians.x),
            positiveRadians(eulerRadians.y),
            positiveRadians(eulerRadians.z),
    };
}

inline Vec3 equivalentEulerRadiansClosestTo(Vec3 eulerRadians, Vec3 referenceEulerRadians) {
    constexpr float Pi = 3.14159265358979323846f;
    const auto normalizeCandidate = [&](Vec3 candidate) {
        return Vec3{
                radiansNearReference(candidate.x, referenceEulerRadians.x),
                radiansNearReference(candidate.y, referenceEulerRadians.y),
                radiansNearReference(candidate.z, referenceEulerRadians.z),
        };
    };
    const auto score = [&](Vec3 candidate) {
        const Vec3 delta = candidate - referenceEulerRadians;
        return dot(delta, delta);
    };

    const Vec3 primary = normalizeCandidate(eulerRadians);
    const Vec3 alternate = normalizeCandidate({
            eulerRadians.x + Pi,
            Pi - eulerRadians.y,
            eulerRadians.z + Pi,
    });
    return score(alternate) < score(primary) ? alternate : primary;
}

inline Vec3 eulerRadiansAfterWorldRotationDelta(Vec3 currentEulerRadians, Vec3 worldRotationDeltaRadians) {
    const glm::quat delta = glm::quat(toGlm(worldRotationDeltaRadians));
    const glm::quat current = toGlm(quatFromEulerRadians(currentEulerRadians));
    return equivalentEulerRadiansClosestTo(
            fromGlm(glm::eulerAngles(glm::normalize(delta * current))),
            currentEulerRadians);
}

inline void applyWorldRotationDeltaAroundPivot(
        TransformComponent& transform,
        Vec3 worldRotationDeltaRadians,
        Vec3 localPivot,
        Vec3 worldPivot) {
    transform.rotationEuler = eulerRadiansAfterWorldRotationDelta(transform.rotationEuler, worldRotationDeltaRadians);
    pinLocalPivotToWorldPoint(transform, localPivot, worldPivot);
}

} // namespace quader
