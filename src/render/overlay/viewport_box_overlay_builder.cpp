////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_box_overlay_builder.hpp"

#include "render/overlay/viewport_overlay_dash.hpp"
#include "render/overlay/viewport_overlay_geometry.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

/**
 * Stores a projected box preview endpoint for dash segmentation.
 */
struct OverlayScreenPoint {
    QVec2 position;
    float depth = 0.0f;
    bool visible = false;
};

ViewportOverlayLine box_preview_line(QVec3 start, QVec3 end) {
  return {
      .layer = ViewportOverlayLayer::ToolPreviewLine,
      .start = start,
      .end = end,
      .color = kOverlayToolBoxPreviewLineColor,
      .width_pixels = kOverlayToolBoxPreviewLineWidthPixels,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  };
}

void append_line(ViewportOverlayFrame &overlay_frame, QVec3 start, QVec3 end) {
  overlay_frame.lines.push_back(box_preview_line(start, end));
}

OverlayScreenPoint
project_overlay_world_point(const ViewportOverlayCamera &camera,
                            QVec3 world_point) {
  const float width = std::max(camera.viewport_size.x, 1.0f);
  const float height = std::max(camera.viewport_size.y, 1.0f);
  const float aspect = width / height;
  const QVec3 forward =
      viewport_overlay_normalize_or(camera.forward, {0.0f, 0.0f, -1.0f});
  const QVec3 right =
      viewport_overlay_normalize_or(camera.right, {1.0f, 0.0f, 0.0f});
  const QVec3 up = viewport_overlay_normalize_or(camera.up, {0.0f, 1.0f, 0.0f});
  const QVec3 camera_delta = world_point - camera.eye;
  const float depth = dot(camera_delta, forward);
  if (depth <= camera.near_plane || depth >= camera.far_plane) {
    return {};
  }

  float ndc_x = 0.0f;
  float ndc_y = 0.0f;
  if (camera.projection == ViewportOverlayProjection::Orthographic) {
    const float half_height =
        std::max(camera.orthographic_size, 0.0001f) * 0.5f;
    ndc_x = dot(camera_delta, right) / (half_height * aspect);
    ndc_y = dot(camera_delta, up) / half_height;
  } else {
    constexpr float kPi = 3.14159265358979323846f;
    const float fov_radians =
        std::clamp(camera.fov_degrees, 1.0f, 170.0f) * kPi / 180.0f;
    const float tan_half_fov = std::tan(fov_radians * 0.5f);
    ndc_x = dot(camera_delta, right) / (depth * tan_half_fov * aspect);
    ndc_y = dot(camera_delta, up) / (depth * tan_half_fov);
  }

  return {
      .position =
          {
              (ndc_x + 1.0f) * 0.5f * width,
              (1.0f - ndc_y) * 0.5f * height,
          },
      .depth = depth,
      .visible = std::isfinite(ndc_x) && std::isfinite(ndc_y),
  };
}

} // namespace

void ViewportBoxOverlayBuilder::append(ViewportOverlayFrame &overlay_frame,
                                       const ViewportOverlayCamera &camera,
                                       const ModelingBoxPreview &preview) {
  if (!preview.valid) {
    return;
  }

  if (preview.footprint_count < 4U) {
    return;
  }
  for (std::size_t index = 0; index < preview.footprint_count; ++index) {
    const QVec3 world_start = preview.footprint[index];
    const QVec3 world_end =
        preview.footprint[(index + 1U) % preview.footprint_count];
    const OverlayScreenPoint screen_start =
        project_overlay_world_point(camera, world_start);
    const OverlayScreenPoint screen_end =
        project_overlay_world_point(camera, world_end);
    if (!screen_start.visible || !screen_end.visible) {
      continue;
    }
    const QVec3 world_delta = world_end - world_start;
    append_viewport_overlay_dashed_edge(
        screen_start.position, screen_end.position, true,
        [&](QVec2, QVec2, float start_t, float end_t) {
          append_line(overlay_frame, world_start + world_delta * start_t,
                      world_start + world_delta * end_t);
        });
  }
}

} // namespace quader
