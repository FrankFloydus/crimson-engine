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
    : camera_(sanitized_camera(camera)) {
  // These values are camera constants. A selected scene can project tens of
  // thousands of overlay endpoints per frame, so computing trigonometry and
  // viewport divisions for every endpoint dominates navigation without
  // changing any overlay policy or generated geometry.
  aspect_ = camera_.viewport_size.x / camera_.viewport_size.y;
  tan_half_fov_ =
      std::tan((camera_.fov_degrees * kPi / 180.0f) * 0.5f);
  orthographic_half_height_ = camera_.orthographic_size * 0.5f;
  orthographic_half_width_ = orthographic_half_height_ * aspect_;
  const float clip_range = camera_.far_plane - camera_.near_plane;
  orthographic_depth_scale_ = -2.0f / clip_range;
  orthographic_depth_offset_ =
      -(camera_.far_plane + camera_.near_plane) / clip_range;
}

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
        projected.device = {
            point.x / (depth * tan_half_fov_ * aspect_),
            point.y / (depth * tan_half_fov_),
            1.0f - (2.0f * camera_.near_plane / depth),
            1.0f,
        };
    } else {
      projected.device = {
          point.x / orthographic_half_width_,
          point.y / orthographic_half_height_,
          orthographic_depth_scale_ * point.z + orthographic_depth_offset_,
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
  // Filament's DEVICE vertex domain accepts OpenGL NDC, then maps its Z to
  // reversed device depth as `z * -0.5 + 0.5`.  A smaller GL-domain Z is
  // therefore closer for the GreaterOrEqual overlay depth test.  Adding this
  // offset used to push depth-tested vertex quads into the shaded mesh, which
  // could completely hide a convex corner surrounded by rasterized faces.
  projected.device.z = std::max(-1.0f, projected.device.z - device_offset);
}

OverlayFloat4
OverlayCameraProjector::with_pixel_offset(OverlayFloat4 device,
                                          QVec2 pixel_offset) const {
  device.x += pixel_offset.x * 2.0f / camera_.viewport_size.x;
  device.y += pixel_offset.y * 2.0f / camera_.viewport_size.y;
  return device;
}

} // namespace quader
