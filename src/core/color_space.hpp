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
#include <cstdint>

namespace quader {

/**
 * Enumerates TextureColorSpace values used by the core layer.
 */
enum class TextureColorSpace {
    Srgb,
    Linear
};

inline float srgb_to_linear(float value) {
  value = std::clamp(value, 0.0f, 1.0f);
  if (value <= 0.04045f) {
    return value / 12.92f;
  }
  return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb(float value) {
  value = std::clamp(value, 0.0f, 1.0f);
  if (value <= 0.0031308f) {
    return value * 12.92f;
  }
  return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

inline std::uint8_t normalized_to_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f +
                                   0.5f);
}

inline std::uint8_t srgb_byte_to_linear_byte(std::uint8_t value) {
  // Overlay mesh generation converts the same small set of packed colors for
  // every line and point on every camera update.  Preserve the exact byte
  // conversion while paying the three pow() calls only once per possible
  // channel value instead of once per generated vertex batch.
  static const std::array<std::uint8_t, 256> kLookup = [] {
    std::array<std::uint8_t, 256> values{};
    for (std::size_t index = 0; index < values.size(); ++index) {
      values[index] = normalized_to_byte(
          srgb_to_linear(static_cast<float>(index) / 255.0f));
    }
    return values;
  }();
  return kLookup[value];
}

inline std::uint32_t rgba8_srgb_to_linear(std::uint32_t rgba) {
  const auto r = static_cast<std::uint8_t>(rgba & 0xffu);
  const auto g = static_cast<std::uint8_t>((rgba >> 8u) & 0xffu);
  const auto b = static_cast<std::uint8_t>((rgba >> 16u) & 0xffu);
  const auto a = static_cast<std::uint8_t>((rgba >> 24u) & 0xffu);
  return static_cast<std::uint32_t>(srgb_byte_to_linear_byte(r)) |
         (static_cast<std::uint32_t>(srgb_byte_to_linear_byte(g)) << 8u) |
         (static_cast<std::uint32_t>(srgb_byte_to_linear_byte(b)) << 16u) |
         (static_cast<std::uint32_t>(a) << 24u);
}

} // namespace quader
