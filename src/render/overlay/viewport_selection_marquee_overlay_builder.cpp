////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"

#include "render/overlay/viewport_overlay_dash.hpp"
#include "render/overlay/viewport_overlay_geometry.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace quader {
namespace {

QVec3 marquee_world_point(const ViewportOverlayCamera &camera, QVec2 screen) {
  const QVec3 forward =
      viewport_overlay_normalize_or(camera.forward, {0.0f, 0.0f, -1.0f});
  QVec3 right = viewport_overlay_normalize_or(camera.right, {1.0f, 0.0f, 0.0f});
  if (std::abs(dot(right, forward)) > 0.999f) {
    right = viewport_overlay_normalize_or(cross(forward, {0.0f, 1.0f, 0.0f}),
                                          {1.0f, 0.0f, 0.0f});
  }
  const QVec3 up =
      viewport_overlay_normalize_or(cross(right, forward), {0.0f, 1.0f, 0.0f});
  const float width = std::max(camera.viewport_size.x, 1.0f);
  const float height = std::max(camera.viewport_size.y, 1.0f);
  const float aspect = width / height;
  const float ndc_x = (screen.x / width) * 2.0f - 1.0f;
  const float ndc_y = 1.0f - (screen.y / height) * 2.0f;
  const float depth = std::max(camera.near_plane + 0.05f, 0.1f);

  if (camera.projection == ViewportOverlayProjection::Orthographic) {
    const float half_height =
        std::max(camera.orthographic_size, 0.0001f) * 0.5f;
    return camera.eye + forward * depth +
           right * (ndc_x * half_height * aspect) + up * (ndc_y * half_height);
  }

  constexpr float kPi = 3.14159265358979323846f;
  const float fov_radians =
      std::clamp(camera.fov_degrees, 1.0f, 170.0f) * kPi / 180.0f;
  const float tan_half_fov = std::tan(fov_radians * 0.5f);
  return camera.eye + forward * depth +
         right * (ndc_x * depth * tan_half_fov * aspect) +
         up * (ndc_y * depth * tan_half_fov);
}

void append_segment(ViewportOverlayFrame &frame,
                    const ViewportOverlayCamera &camera, QVec2 start, QVec2 end,
                    std::uint32_t color) {
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::ToolPreviewLine,
      .start = marquee_world_point(camera, start),
      .end = marquee_world_point(camera, end),
      .color = color,
      .width_pixels = kOverlaySelectionMarqueeLineWidthPixels,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
}

} // namespace

void ViewportSelectionMarqueeOverlayBuilder::append(
    ViewportOverlayFrame &overlay_frame, const ViewportOverlayCamera &camera,
    const RenderSelectionMarqueeRecord &marquee, float raster_scale) {
  if (!marquee.active) {
    return;
  }

  const bool dashed = marquee.crossing;
  const QVec2 min{
      std::min(marquee.start.x, marquee.end.x),
      std::min(marquee.start.y, marquee.end.y),
  };
  const QVec2 max{
      std::max(marquee.start.x, marquee.end.x),
      std::max(marquee.start.y, marquee.end.y),
  };
  const std::array<QVec2, 4> corners{{
      {min.x, min.y},
      {max.x, min.y},
      {max.x, max.y},
      {min.x, max.y},
  }};
  const std::array<QVec3, 4> world_corners{{
      marquee_world_point(camera, corners[0]),
      marquee_world_point(camera, corners[1]),
      marquee_world_point(camera, corners[2]),
      marquee_world_point(camera, corners[3]),
  }};
  overlay_frame.triangles.push_back({
      .layer = ViewportOverlayLayer::ToolPreviewFill,
      .a = world_corners[0],
      .b = world_corners[1],
      .c = world_corners[2],
      .color = kOverlaySelectionMarqueeFillColor,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
  overlay_frame.triangles.push_back({
      .layer = ViewportOverlayLayer::ToolPreviewFill,
      .a = world_corners[0],
      .b = world_corners[2],
      .c = world_corners[3],
      .color = kOverlaySelectionMarqueeFillColor,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
  for (std::size_t index = 0; index < corners.size(); ++index) {
    append_viewport_overlay_dashed_edge(
        corners[index], corners[(index + 1U) % corners.size()], dashed,
        [&](QVec2 start, QVec2 end, float, float) {
          append_segment(overlay_frame, camera, start, end,
                         kOverlaySelectionMarqueeLineColor);
        },
        raster_scale);
  }
}

} // namespace quader
