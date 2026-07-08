////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cmath>

namespace quader {

/**
 * Represents a 3D vector value used by Quader modeling internals.
 */
struct QVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr QVec3() = default;
    constexpr QVec3(float x_value, float y_value, float z_value)
        : x(x_value), y(y_value), z(z_value) {}
};

inline QVec3 operator+(QVec3 a, QVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline QVec3 operator-(QVec3 a, QVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline QVec3 operator*(QVec3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline QVec3 operator*(float s, QVec3 v) {
    return v * s;
}

inline QVec3 operator/(QVec3 v, float s) {
    return {v.x / s, v.y / s, v.z / s};
}

inline QVec3& operator+=(QVec3& left, QVec3 right) {
    left = left + right;
    return left;
}

inline float dot(QVec3 a, QVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline QVec3 cross(QVec3 a, QVec3 b) {
    return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
    };
}

inline float length_squared(QVec3 v) {
    return dot(v, v);
}

inline float length(QVec3 v) {
    return std::sqrt(length_squared(v));
}

inline QVec3 normalize_or_zero(QVec3 v) {
    const float len = length(v);
    if (len <= 0.000001f) {
        return {};
    }
    return v / len;
}

inline QVec3 normalize(QVec3 v) {
    return normalize_or_zero(v);
}

} // namespace quader

