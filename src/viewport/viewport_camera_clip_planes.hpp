////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>

namespace quader {

inline constexpr float kViewportCameraMinimumNearPlane = 0.001f;
inline constexpr float kViewportCameraMinimumClipPlaneGap = 0.001f;

/**
 * Stores sanitized viewport camera clip planes shared by viewport and render owners.
 */
struct ViewportCameraClipPlanes {
    float near_plane = kViewportCameraMinimumNearPlane;
    float far_plane = 1000.0f;

    bool operator==(const ViewportCameraClipPlanes &) const = default;
};

[[nodiscard]] inline ViewportCameraClipPlanes
sanitize_viewport_camera_clip_planes(float near_plane, float far_plane) {
  const float sanitized_near =
      std::max(near_plane, kViewportCameraMinimumNearPlane);
  return {
      .near_plane = sanitized_near,
      .far_plane = std::max(far_plane, sanitized_near +
                                           kViewportCameraMinimumClipPlaneGap),
  };
}

} // namespace quader
