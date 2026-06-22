////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace quader {

/**
 * Represents a Vec2 value used by the Quader native app.
 */
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float xValue, float yValue) : x(xValue), y(yValue) {}
    explicit Vec2(glm::vec2 value) : x(value.x), y(value.y) {}
};

/**
 * Represents a Vec3 value used by the Quader native app.
 */
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}
    explicit Vec3(glm::vec3 value) : x(value.x), y(value.y), z(value.z) {}
};

/**
 * Represents a Quat value used by the Quader native app.
 */
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float xValue, float yValue, float zValue, float wValue)
        : x(xValue), y(yValue), z(zValue), w(wValue) {}
    explicit Quat(glm::quat value) : x(value.x), y(value.y), z(value.z), w(value.w) {}
};

/**
 * Represents a Mat4 value used by the Quader native app.
 */
struct Mat4 {
    std::array<float, 16> values = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
    };

    Mat4() = default;
    explicit Mat4(const glm::mat4& value) {
        const float* raw = &value[0][0];
        std::copy(raw, raw + values.size(), values.begin());
    }
};

/**
 * Represents a Transform value used by the Quader native app.
 */
struct Transform {
    Vec3 position = {};
    Quat rotation = {};
    Vec3 scale = {1.0f, 1.0f, 1.0f};
};

inline glm::vec2 toGlm(Vec2 value) {
    return {value.x, value.y};
}

inline glm::vec3 toGlm(Vec3 value) {
    return {value.x, value.y, value.z};
}

inline glm::quat toGlm(Quat value) {
    return {value.w, value.x, value.y, value.z};
}

inline glm::mat4 toGlm(const Mat4& value) {
    glm::mat4 result{1.0f};
    std::copy(value.values.begin(), value.values.end(), &result[0][0]);
    return result;
}

inline Vec2 fromGlm(glm::vec2 value) {
    return Vec2{value};
}

inline Vec3 fromGlm(glm::vec3 value) {
    return Vec3{value};
}

inline Quat fromGlm(glm::quat value) {
    return Quat{value};
}

inline Mat4 fromGlm(const glm::mat4& value) {
    return Mat4{value};
}

inline Vec2 operator+(Vec2 a, Vec2 b) {
    return fromGlm(toGlm(a) + toGlm(b));
}

inline Vec2 operator-(Vec2 a, Vec2 b) {
    return fromGlm(toGlm(a) - toGlm(b));
}

inline Vec2 operator*(Vec2 v, float s) {
    return fromGlm(toGlm(v) * s);
}

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return fromGlm(toGlm(a) + toGlm(b));
}

inline Vec3 operator-(Vec3 a, Vec3 b) {
    return fromGlm(toGlm(a) - toGlm(b));
}

inline Vec3 operator*(Vec3 v, float s) {
    return fromGlm(toGlm(v) * s);
}

inline Vec3 operator*(float s, Vec3 v) {
    return v * s;
}

inline Vec3 operator/(Vec3 v, float s) {
    return fromGlm(toGlm(v) / s);
}

inline float dot(Vec3 a, Vec3 b) {
    return glm::dot(toGlm(a), toGlm(b));
}

inline Vec3 cross(Vec3 a, Vec3 b) {
    return fromGlm(glm::cross(toGlm(a), toGlm(b)));
}

inline float length(Vec3 v) {
    return glm::length(toGlm(v));
}

inline Vec3 normalize(Vec3 v) {
    const float len = length(v);
    if (len <= 0.000001f) {
        return {};
    }
    return v / len;
}

inline float clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

inline Quat normalize(Quat value) {
    return fromGlm(glm::normalize(toGlm(value)));
}

inline Quat quatFromEulerRadians(Vec3 eulerRadians) {
    return fromGlm(glm::quat(toGlm(eulerRadians)));
}

inline Mat4 composeTransform(const Transform& transform) {
    const glm::mat4 translation = glm::translate(glm::mat4{1.0f}, toGlm(transform.position));
    const glm::mat4 rotation = glm::mat4_cast(toGlm(transform.rotation));
    const glm::mat4 scale = glm::scale(glm::mat4{1.0f}, toGlm(transform.scale));
    return fromGlm(translation * rotation * scale);
}

inline Mat4 inverse(const Mat4& matrix) {
    return fromGlm(glm::inverse(toGlm(matrix)));
}

inline Vec3 transformPoint(const Mat4& matrix, Vec3 point) {
    const glm::vec4 result = toGlm(matrix) * glm::vec4{toGlm(point), 1.0f};
    return fromGlm(glm::vec3{result} / result.w);
}

inline Vec3 transformVector(const Mat4& matrix, Vec3 vector) {
    const glm::vec4 result = toGlm(matrix) * glm::vec4{toGlm(vector), 0.0f};
    return fromGlm(glm::vec3{result});
}

} // namespace quader
