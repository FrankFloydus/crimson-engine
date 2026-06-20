#pragma once

#include "quader/scene/Components.h"

namespace quader {

inline Vec3 localPivotForWorldPoint(const TransformComponent& transform, Vec3 worldPivot) {
    return transformPoint(inverse(composeTransform(transform.spatialTransform())), worldPivot);
}

inline void pinLocalPivotToWorldPoint(TransformComponent& transform, Vec3 localPivot, Vec3 worldPivot) {
    Transform spatial = transform.spatialTransform();
    spatial.position = {};
    transform.position = worldPivot - transformPoint(composeTransform(spatial), localPivot);
}

inline Vec3 eulerRadiansAfterWorldRotationDelta(Vec3 currentEulerRadians, Vec3 worldRotationDeltaRadians) {
    const glm::quat delta = glm::quat(toGlm(worldRotationDeltaRadians));
    const glm::quat current = toGlm(quatFromEulerRadians(currentEulerRadians));
    return fromGlm(glm::eulerAngles(glm::normalize(delta * current)));
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
