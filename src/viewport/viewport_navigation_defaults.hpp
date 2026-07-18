////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <cmath>

namespace quader {

inline constexpr float kDefaultViewportFlyNavigationSpeed = 25.0f;
inline constexpr float kMinimumViewportFlyNavigationSpeed = 1.0f;
inline constexpr float kMaximumViewportFlyNavigationSpeed = 1000.0f;

[[nodiscard]] inline float
viewport_sanitize_fly_navigation_speed(float speed) {
  return std::isfinite(speed)
             ? std::clamp(speed, kMinimumViewportFlyNavigationSpeed,
                          kMaximumViewportFlyNavigationSpeed)
             : kDefaultViewportFlyNavigationSpeed;
}

} // namespace quader

