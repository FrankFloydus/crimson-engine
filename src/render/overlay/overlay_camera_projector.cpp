////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_camera_projector.hpp"

#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <cmath>

namespace quader {

OverlayCameraProjector::OverlayCameraProjector(ViewportOverlayCamera camera)
    : camera_(sanitized_camera(camera)) {}

const ViewportOverlayCamera& OverlayCameraProjector::camera() const {
    return camera_;
}

ViewportOverlayCamera
OverlayCameraProjector::sanitized_camera(ViewportOverlayCamera camera) {
  camera.forward = normalize(camera.forward);
  if (length(camera.forward) <= 0.000001f) {
    camera.forward = {0.0f, 0.0f, -1.0f};
  }
  camera.right = normalize(camera.right);
  if (length(camera.right) <= 0.000001f ||
      std::abs(dot(camera.right, camera.forward)) > 0.999f) {
    const QVec3 reference = std::abs(camera.forward.y) < 0.999f
                                ? QVec3{0.0f, 1.0f, 0.0f}
                                : QVec3{1.0f, 0.0f, 0.0f};
    camera.right = normalize(cross(camera.forward, reference));
  }
  camera.up = normalize(cross(camera.right, camera.forward));
  if (length(camera.up) <= 0.000001f) {
    camera.up = {0.0f, 1.0f, 0.0f};
  }
  camera.viewport_size.x = std::max(camera.viewport_size.x, 1.0f);
  camera.viewport_size.y = std::max(camera.viewport_size.y, 1.0f);
  camera.orthographic_size = std::max(camera.orthographic_size, 0.0001f);
  camera.fov_degrees = std::clamp(camera.fov_degrees, 1.0f, 170.0f);
  camera.near_plane = std::max(camera.near_plane, 0.0001f);
  camera.far_plane = std::max(camera.far_plane, camera.near_plane + 0.001f);
  return camera;
}

OverlayViewPoint OverlayCameraProjector::view_point_for(QVec3 world) const {
  const QVec3 relative = world - camera_.eye;
  const float depth = dot(relative, camera_.forward);
  return {
      .x = dot(relative, camera_.right),
      .y = dot(relative, camera_.up),
      .z = -depth,
  };
}

float OverlayCameraProjector::near_plane_distance(
    const OverlayViewPoint &point) const {
  return -point.z - camera_.near_plane;
}

std::optional<OverlayClippedLine>
OverlayCameraProjector::clip_line_to_near_plane(QVec3 start, QVec3 end) const {
  OverlayViewPoint a = view_point_for(start);
  OverlayViewPoint b = view_point_for(end);

  const float a_near = near_plane_distance(a);
  const float b_near = near_plane_distance(b);
  if (a_near < 0.0f && b_near < 0.0f) {
    return std::nullopt;
  }
  if (a_near < 0.0f || b_near < 0.0f) {
    const float t = std::clamp(a_near / (a_near - b_near), 0.0f, 1.0f);
    const OverlayViewPoint clipped{
        .x = a.x + (b.x - a.x) * t,
        .y = a.y + (b.y - a.y) * t,
        .z = a.z + (b.z - a.z) * t,
    };
    if (a_near < 0.0f) {
      a = clipped;
    } else {
      b = clipped;
    }
  }
  return OverlayClippedLine{.start = a, .end = b};
}

OverlayProjectedPoint OverlayCameraProjector::project(OverlayViewPoint point) const {
    OverlayProjectedPoint projected;

    if (camera_.projection == ViewportOverlayProjection::Perspective) {
        const float depth = -point.z;
        if (depth <= 0.000001f) {
            return projected;
        }
        const float aspect = camera_.viewport_size.x / camera_.viewport_size.y;
        const float tan_half_fov =
            std::tan((camera_.fov_degrees * kPi / 180.0f) * 0.5f);
        projected.device = {
            point.x / (depth * tan_half_fov * aspect),
            point.y / (depth * tan_half_fov),
            1.0f - (2.0f * camera_.near_plane / depth),
            1.0f,
        };
    } else {
      const float aspect = camera_.viewport_size.x / camera_.viewport_size.y;
      const float half_height = camera_.orthographic_size * 0.5f;
      const float half_width = half_height * aspect;
      projected.device = {
          point.x / half_width,
          point.y / half_height,
          (-2.0f / (camera_.far_plane - camera_.near_plane)) * point.z -
              ((camera_.far_plane + camera_.near_plane) /
               (camera_.far_plane - camera_.near_plane)),
          1.0f,
      };
    }

    const QVec2 ndc{projected.device.x, projected.device.y};
    projected.pixel = {
        (ndc.x * 0.5f + 0.5f) * camera_.viewport_size.x,
        (ndc.y * 0.5f + 0.5f) * camera_.viewport_size.y,
    };
    projected.valid = true;
    return projected;
}

void OverlayCameraProjector::apply_device_depth_bias(
    OverlayProjectedPoint &projected, float depth_bias_units) const {
  if (!projected.valid || depth_bias_units <= 0.0f) {
    return;
  }
  const float device_offset = depth_bias_units * camera_.near_plane *
                              kOverlayDeviceDepthBiasPerNearPlane;
  projected.device.z += device_offset;
}

OverlayFloat4
OverlayCameraProjector::with_pixel_offset(OverlayFloat4 device,
                                          QVec2 pixel_offset) const {
  device.x += pixel_offset.x * 2.0f / camera_.viewport_size.x;
  device.y += pixel_offset.y * 2.0f / camera_.viewport_size.y;
  return device;
}

} // namespace quader
