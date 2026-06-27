////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/q_vec3.hpp"

#include <cmath>

namespace quader {

/**
 * Represents a QQuat value used by the Quader native app.
 */
struct QQuat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr QQuat() = default;
    constexpr QQuat(float xValue, float yValue, float zValue, float wValue)
        : x(xValue), y(yValue), z(zValue), w(wValue) {}
};

inline QQuat normalize(QQuat value) {
    const float lengthSquared =
            value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= 0.000001f * 0.000001f) {
        return {};
    }
    const float invLength = 1.0f / std::sqrt(lengthSquared);
    return {
            value.x * invLength,
            value.y * invLength,
            value.z * invLength,
            value.w * invLength,
    };
}

inline QQuat quatFromEulerRadians(QVec3 eulerRadians) {
    const QVec3 c{
            std::cos(eulerRadians.x * 0.5f),
            std::cos(eulerRadians.y * 0.5f),
            std::cos(eulerRadians.z * 0.5f),
    };
    const QVec3 s{
            std::sin(eulerRadians.x * 0.5f),
            std::sin(eulerRadians.y * 0.5f),
            std::sin(eulerRadians.z * 0.5f),
    };
    return {
            s.x * c.y * c.z - c.x * s.y * s.z,
            c.x * s.y * c.z + s.x * c.y * s.z,
            c.x * c.y * s.z - s.x * s.y * c.z,
            c.x * c.y * c.z + s.x * s.y * s.z,
    };
}

} // namespace quader
