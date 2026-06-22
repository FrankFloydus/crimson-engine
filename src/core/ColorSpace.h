////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace quader {

/**
 * Enumerates TextureColorSpace values used by the core layer.
 */
enum class TextureColorSpace {
    Srgb,
    Linear
};

inline float srgbToLinear(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    if (value <= 0.04045f) {
        return value / 12.92f;
    }
    return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline float linearToSrgb(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    if (value <= 0.0031308f) {
        return value * 12.92f;
    }
    return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

inline std::uint8_t normalizedToByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

inline std::uint8_t srgbByteToLinearByte(std::uint8_t value) {
    return normalizedToByte(srgbToLinear(static_cast<float>(value) / 255.0f));
}

inline std::uint32_t rgba8SrgbToLinear(std::uint32_t rgba) {
    const auto r = static_cast<std::uint8_t>(rgba & 0xffu);
    const auto g = static_cast<std::uint8_t>((rgba >> 8u) & 0xffu);
    const auto b = static_cast<std::uint8_t>((rgba >> 16u) & 0xffu);
    const auto a = static_cast<std::uint8_t>((rgba >> 24u) & 0xffu);
    return static_cast<std::uint32_t>(srgbByteToLinearByte(r)) |
            (static_cast<std::uint32_t>(srgbByteToLinearByte(g)) << 8u) |
            (static_cast<std::uint32_t>(srgbByteToLinearByte(b)) << 16u) |
            (static_cast<std::uint32_t>(a) << 24u);
}

} // namespace quader
