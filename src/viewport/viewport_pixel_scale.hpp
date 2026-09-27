////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/interface_scale_policy.hpp"

#include <algorithm>
#include <cmath>

namespace quader {

inline constexpr float kViewportPixelScaleMinimum = 0.5f;
inline constexpr float kViewportPixelScaleMaximum = 15.0f;

/** Converts the persisted component tolerance into its logical-pixel aperture. */
[[nodiscard]] inline double
viewport_component_pick_radius_pixels(double preference_radius) noexcept {
  constexpr double kPixelsPerPreferenceUnit = 40.0;
  return std::clamp(preference_radius * kPixelsPerPreferenceUnit, 2.0, 32.0);
}

/** Keeps screen-space presentation scale finite and bounded at owner seams. */
[[nodiscard]] inline float
viewport_sanitized_pixel_scale(float viewport_pixel_scale) noexcept {
  if (!std::isfinite(viewport_pixel_scale) || viewport_pixel_scale <= 0.0f) {
    return 1.0f;
  }
  return std::clamp(viewport_pixel_scale, kViewportPixelScaleMinimum,
                    kViewportPixelScaleMaximum);
}

/**
 * Recovers the user-authored UI factor from the combined physical-pixel
 * scale. World-space ray tolerances already account for monitor DPR through
 * camera projection, so matching interaction radii use this factor only.
 */
[[nodiscard]] inline float viewport_user_interface_scale(
    float viewport_pixel_scale, float device_pixel_ratio) noexcept {
  if (!std::isfinite(viewport_pixel_scale) || viewport_pixel_scale <= 0.0f) {
    return 1.0f;
  }
  const float usable_dpr =
      std::isfinite(device_pixel_ratio) && device_pixel_ratio > 0.0f
          ? device_pixel_ratio
          : 1.0f;
  const float user_scale = viewport_pixel_scale / usable_dpr;
  return std::clamp(user_scale, kInterfaceScaleMinimum,
                    kInterfaceScaleMaximum);
}

[[nodiscard]] inline double viewport_scaled_world_pick_radius(
    double authored_radius, float viewport_pixel_scale,
    float device_pixel_ratio) noexcept {
  if (!std::isfinite(authored_radius)) {
    return authored_radius;
  }
  return authored_radius * static_cast<double>(viewport_user_interface_scale(
                               viewport_pixel_scale, device_pixel_ratio));
}

} // namespace quader
