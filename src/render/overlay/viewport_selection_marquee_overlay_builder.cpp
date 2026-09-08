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
#include <span>
#include <vector>

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
                    std::uint32_t color,
                    float width = kOverlaySelectionMarqueeLineWidthPixels) {
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::ToolPreviewLine,
      .start = marquee_world_point(camera, start),
      .end = marquee_world_point(camera, end),
      .color = color,
      .width_pixels = width,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
}

/** One non-horizontal boundary, ordered from top to bottom in screen space. */
struct LassoFillEdge final {
  QVec2 top;
  QVec2 bottom;

  [[nodiscard]] double x_at(double y) const {
    const double fraction = (y - top.y) / (double(bottom.y) - top.y);
    return double(top.x) + (double(bottom.x) - top.x) * fraction;
  }
};

void append_fill_triangle(ViewportOverlayFrame &frame,
                          const ViewportOverlayCamera &camera, QVec2 a,
                          QVec2 b, QVec2 c) {
  const double area = (double(b.x) - a.x) * (double(c.y) - a.y) -
                      (double(b.y) - a.y) * (double(c.x) - a.x);
  if (std::abs(area) <= 1.0e-8) {
    return;
  }
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::ToolPreviewFill,
      .a = marquee_world_point(camera, a),
      .b = marquee_world_point(camera, b),
      .c = marquee_world_point(camera, c),
      .color = kOverlaySelectionLassoFillColor,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
}

void append_lasso_fill(ViewportOverlayFrame &frame,
                       const ViewportOverlayCamera &camera,
                       std::span<const QVec2> points) {
  if (points.size() < 3U) {
    return;
  }

  std::vector<LassoFillEdge> edges;
  std::vector<double> boundaries;
  edges.reserve(points.size());
  boundaries.reserve(points.size());
  for (std::size_t index = 0; index < points.size(); ++index) {
    QVec2 top = points[index];
    QVec2 bottom = points[(index + 1U) % points.size()];
    boundaries.push_back(top.y);
    if (top.y == bottom.y) {
      continue;
    }
    if (top.y > bottom.y) {
      std::swap(top, bottom);
    }
    edges.push_back({top, bottom});
  }

  // Split at crossings as well as vertices. Between consecutive boundaries
  // edge order is stable, so adjacent crossing pairs tessellate even-odd fill
  // without overlapping triangles or filling self-intersection holes.
  for (std::size_t first = 0; first < edges.size(); ++first) {
    for (std::size_t second = first + 1U; second < edges.size(); ++second) {
      const LassoFillEdge &a = edges[first];
      const LassoFillEdge &b = edges[second];
      const double top = std::max(a.top.y, b.top.y);
      const double bottom = std::min(a.bottom.y, b.bottom.y);
      if (top >= bottom) {
        continue;
      }
      const double difference_top = a.x_at(top) - b.x_at(top);
      const double difference_bottom = a.x_at(bottom) - b.x_at(bottom);
      if ((difference_top < 0.0 && difference_bottom > 0.0) ||
          (difference_top > 0.0 && difference_bottom < 0.0)) {
        boundaries.push_back(
            top + (bottom - top) * difference_top /
                      (difference_top - difference_bottom));
      }
    }
  }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                   boundaries.end());

  std::vector<const LassoFillEdge *> crossings;
  crossings.reserve(edges.size());
  for (std::size_t boundary = 1U; boundary < boundaries.size(); ++boundary) {
    const double top = boundaries[boundary - 1U];
    const double bottom = boundaries[boundary];
    const double middle = (top + bottom) * 0.5;
    if (bottom - top <= 1.0e-8) {
      continue;
    }
    crossings.clear();
    for (const LassoFillEdge &edge : edges) {
      if (middle >= edge.top.y && middle < edge.bottom.y) {
        crossings.push_back(&edge);
      }
    }
    std::sort(crossings.begin(), crossings.end(),
              [middle](const LassoFillEdge *a, const LassoFillEdge *b) {
                return a->x_at(middle) < b->x_at(middle);
              });
    for (std::size_t index = 1U; index < crossings.size(); index += 2U) {
      const LassoFillEdge &left = *crossings[index - 1U];
      const LassoFillEdge &right = *crossings[index];
      const QVec2 a{float(left.x_at(top)), float(top)};
      const QVec2 b{float(right.x_at(top)), float(top)};
      const QVec2 c{float(right.x_at(bottom)), float(bottom)};
      const QVec2 d{float(left.x_at(bottom)), float(bottom)};
      append_fill_triangle(frame, camera, a, b, c);
      append_fill_triangle(frame, camera, a, c, d);
    }
  }
}

void append_lasso(ViewportOverlayFrame &frame,
                   const ViewportOverlayCamera &camera,
                   std::span<const QVec2> points, float raster_scale) {
  if (points.size() < 2U ||
      std::any_of(points.begin(), points.end(), [](QVec2 point) {
        return !std::isfinite(point.x) || !std::isfinite(point.y);
      })) {
    return;
  }
  append_lasso_fill(frame, camera, points);

  const double dash_length = kOverlaySelectionLassoDashPeriodPixels *
                             kOverlaySelectionLassoDashFactor *
                             viewport_overlay_sanitized_raster_scale(raster_scale);
  double path_length = 0.0;
  const std::size_t segment_count = points.size() == 2U ? 1U : points.size();
  for (std::size_t index = 0; index < segment_count; ++index) {
    const QVec2 start = points[index];
    const QVec2 delta = points[(index + 1U) % points.size()] - start;
    const double length = std::hypot(double(delta.x), double(delta.y));
    if (length <= 1.0e-8) {
      continue;
    }
    double offset = 0.0;
    while (offset < length) {
      const double distance = path_length + offset;
      const double dash_index = std::floor(distance / dash_length + 1.0e-8);
      const double end = std::min(length, (dash_index + 1.0) * dash_length - path_length);
      if (end <= offset) {
        break;
      }
      const std::uint32_t color =
          std::fmod(dash_index, 2.0) < 1.0
              ? kOverlaySelectionLassoDarkLineColor
              : kOverlaySelectionLassoLightLineColor;
      append_segment(frame, camera, start + delta * float(offset / length),
                      start + delta * float(end / length), color,
                      kOverlaySelectionLassoLineWidthPixels);
      offset = end;
    }
    path_length += length;
  }
}

} // namespace

void ViewportSelectionMarqueeOverlayBuilder::append(
    ViewportOverlayFrame &overlay_frame, const ViewportOverlayCamera &camera,
    const RenderSelectionMarqueeRecord &marquee, float raster_scale) {
  if (!marquee.active) {
    return;
  }
  if (!marquee.lasso_points.empty()) {
    append_lasso(overlay_frame, camera, marquee.lasso_points, raster_scale);
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
