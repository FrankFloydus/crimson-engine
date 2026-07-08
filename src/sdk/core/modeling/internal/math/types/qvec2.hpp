////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/**
 * Represents a 2D vector value used by Quader modeling internals.
 */
struct QVec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr QVec2() = default;
    constexpr QVec2(float x_value, float y_value) : x(x_value), y(y_value) {}
};

inline QVec2 operator+(QVec2 a, QVec2 b) {
    return {a.x + b.x, a.y + b.y};
}

inline QVec2 operator-(QVec2 a, QVec2 b) {
    return {a.x - b.x, a.y - b.y};
}

inline QVec2 operator*(QVec2 v, float s) {
    return {v.x * s, v.y * s};
}

inline QVec2 operator*(float s, QVec2 v) {
    return v * s;
}

inline QVec2 operator/(QVec2 v, float s) {
    return {v.x / s, v.y / s};
}

inline QVec2& operator+=(QVec2& left, QVec2 right) {
    left = left + right;
    return left;
}

} // namespace quader

