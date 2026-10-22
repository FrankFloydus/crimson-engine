////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_bounds_resize_overlay_builder.hpp"

#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/viewport_circular_handle.hpp"
#include "render/overlay/viewport_overlay_dash.hpp"
#include "viewport/bounds_resize_controls.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace quader {
namespace {

constexpr std::array<std::uint32_t, 3> kAxisColors{
    kOverlayToolAxisXColor, kOverlayToolAxisYColor, kOverlayToolAxisZColor};
constexpr std::array<std::uint32_t, 3> kAxisHighlights{
    kOverlayToolAxisXHighlightColor, kOverlayToolAxisYHighlightColor,
    kOverlayToolAxisZHighlightColor};

void append_line(ViewportOverlayFrame &frame, QVec3 start, QVec3 end,
                 std::uint32_t color) {
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::ToolPreviewLine,
      .start = start,
      .end = end,
      .color = color,
      .width_pixels = 1.0f,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
}

QVec3 world_point(const ViewportOverlayCamera &camera, OverlayViewPoint point) {
  return camera.eye + camera.right * point.x + camera.up * point.y +
         camera.forward * -point.z;
}

void append_dashed(ViewportOverlayFrame &frame,
                   const OverlayCameraProjector &projector,
                   QVec3 start, QVec3 end, std::uint32_t color,
                   float raster_scale) {
  const auto clipped = projector.clip_line_to_near_plane(start, end);
  if (!clipped) return;
  const auto projected_start = projector.project(clipped->start);
  const auto projected_end = projector.project(clipped->end);
  if (!projected_start.valid || !projected_end.valid) return;
  const auto &camera = projector.camera();
  const QVec3 a = world_point(camera, clipped->start);
  const QVec3 b = world_point(camera, clipped->end);
  const auto fraction = [&](float screen_fraction) {
    if (camera.projection == ViewportOverlayProjection::Orthographic) {
      return screen_fraction;
    }
    const float start_depth = -clipped->start.z;
    const float end_depth = -clipped->end.z;
    // Invert perspective interpolation so a screen-space dash keeps its
    // requested length even when its endpoints have different depths.
    return screen_fraction * start_depth /
        ((1.0f - screen_fraction) * end_depth + screen_fraction * start_depth);
  };
  append_viewport_overlay_dashed_edge(
      projected_start.pixel, projected_end.pixel, true,
      [&](QVec2, QVec2, float start_t, float end_t) {
        append_line(frame, a + (b - a) * fraction(start_t),
                     a + (b - a) * fraction(end_t), color);
      }, raster_scale);
}

QVec3 handle_center(const ViewportOverlayCamera &camera,
                    const BoundsResizeHandle &handle) {
  const float world_per_pixel =
      (camera.projection == ViewportOverlayProjection::Orthographic
           ? camera.orthographic_size
           : 2.0f * handle.depth *
                 std::tan(camera.fov_degrees * std::numbers::pi_v<float> / 360.0f)) /
      camera.viewport_size.y;
  return camera.eye + camera.forward * handle.depth +
         camera.right * ((handle.screen_position.x - camera.viewport_size.x * 0.5f) *
                         world_per_pixel) +
         camera.up * ((camera.viewport_size.y * 0.5f - handle.screen_position.y) *
                      world_per_pixel);
}

bool drawable(const BoundsResizeHandle &handle) {
  return handle.visible && handle.axis >= 0 && handle.axis < 3 &&
         std::isfinite(handle.screen_position.x) &&
         std::isfinite(handle.screen_position.y) &&
         std::isfinite(handle.depth) && handle.depth > 0.0f &&
         std::isfinite(handle.radius_pixels) && handle.radius_pixels > 0.0f;
}

void append_handle(ViewportOverlayFrame &frame,
                    const ViewportOverlayCamera &camera,
                    const BoundsResizeFrame &bounds, int index) {
  const auto &handle = bounds.handles[index];
  if (!drawable(handle)) return;
  const float world_per_pixel =
      (camera.projection == ViewportOverlayProjection::Orthographic
           ? camera.orthographic_size
           : 2.0f * handle.depth *
                 std::tan(camera.fov_degrees * std::numbers::pi_v<float> / 360.0f)) /
      camera.viewport_size.y;
  const bool highlighted = bounds.active == index || bounds.hovered == index;
  append_viewport_circular_handle(
      frame, handle_center(camera, handle),
      camera.right * (world_per_pixel * handle.radius_pixels),
      camera.up * (-world_per_pixel * handle.radius_pixels),
      {.fill_color = highlighted && handle.enabled ? kAxisHighlights[handle.axis]
                                                  : kAxisColors[handle.axis],
       .outline_color = kOverlayPrimitiveCubeFaceOutlineColor});
}

} // namespace

void ViewportBoundsResizeOverlayBuilder::append(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const BoundsResizeFrame &bounds, const ViewportSettings &) {
  if (!bounds.visible) return;
  const OverlayCameraProjector projector(camera);
  const auto &sanitized = projector.camera();
  for (int corner = 0; corner < 8; ++corner) {
    for (int axis = 0; axis < 3; ++axis) {
      if ((corner & (1 << axis)) == 0) {
        append_line(frame, bounds.corners[corner], bounds.corners[corner | (1 << axis)],
                     kOverlayToolAxisMixedColor);
      }
    }
  }
  for (int axis = 0; axis < 3; ++axis) {
    append_dashed(frame, projector, bounds.guide_starts[axis], bounds.guide_ends[axis],
                   kAxisColors[axis], bounds.raster_scale);
  }
  for (int index = 0; index < 6; ++index) {
    if (drawable(bounds.handles[index])) {
      append_dashed(frame, projector, bounds.handles[index].world_position,
                     handle_center(sanitized, bounds.handles[index]),
                     kAxisColors[bounds.handles[index].axis], bounds.raster_scale);
    }
    if (index != bounds.hovered && index != bounds.active) {
      append_handle(frame, sanitized, bounds, index);
    }
  }
  if (bounds.hovered >= 0 && bounds.hovered < 6 && bounds.hovered != bounds.active) {
    append_handle(frame, sanitized, bounds, bounds.hovered);
  }
  if (bounds.active >= 0 && bounds.active < 6) {
    append_handle(frame, sanitized, bounds, bounds.active);
  }
}

} // namespace quader
