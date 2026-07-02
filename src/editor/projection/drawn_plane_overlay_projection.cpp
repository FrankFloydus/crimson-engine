////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/drawn_plane_overlay_projection.hpp"

#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <array>

namespace quader {
namespace {

/**
 * Stores the Drawn Plane Preview Frame data contract used by projection.
 */
struct DrawnPlanePreviewFrame {
  QVec3 line_start;
  QVec3 line_end;
  QVec3 normal;
  QVec3 side;
  float line_length = 0.0f;
};

bool build_drawn_plane_preview_frame(QVec3 point0, QVec3 point1, QVec3 support,
                                     DrawnPlanePreviewFrame &frame) {
  const QVec3 line = point1 - point0;
  const float line_length = length(line);
  if (line_length <= 0.0001f) {
    return false;
  }

  const QVec3 line_direction = line / line_length;
  const QVec3 support_vector = support - point0;
  QVec3 normal = cross(line, support_vector);
  if (length(normal) <= 0.000001f) {
    normal = cross(line_direction, {0.0f, 1.0f, 0.0f});
    if (length(normal) <= 0.000001f) {
      normal = cross(line_direction, {0.0f, 0.0f, 1.0f});
    }
  }
  normal = normalize(normal);
  if (length(normal) <= 0.000001f) {
    return false;
  }

  QVec3 side = cross(normal, line_direction);
  if (length(side) <= 0.000001f) {
    side = {0.0f, 1.0f, 0.0f};
  }
  side = normalize(side);
  frame = {
      .line_start = point0,
      .line_end = point1,
      .normal = normal,
      .side = side,
      .line_length = line_length,
  };
  return true;
}

float projected_length_squared(QVec3 value) {
  return value.x * value.x + value.y * value.y + value.z * value.z;
}

} // namespace

QVec3 DrawnPlaneOverlayProjection::support_point(
    std::span<const QVec3> visible_points, QVec3 face_normal) {
  if (visible_points.size() > 2U) {
    return visible_points[2];
  }
  if (visible_points.size() > 1U && projected_length_squared(face_normal) > 0.000001f) {
    return visible_points[0] + face_normal;
  }
  return {};
}

void DrawnPlaneOverlayProjection::append(ViewportOverlayFrame &frame,
                                         std::span<const QVec3> visible_points,
                                         QVec3 support, bool has_line) {
  std::array<QVec3, 3> points{};
  const std::size_t count =
      std::min<std::size_t>(visible_points.size(), points.size());
  for (std::size_t index = 0; index < count; ++index) {
    points[index] = visible_points[index];
  }
  if (count <= 0U) {
    return;
  }

  for (std::size_t index = 0; index < count; ++index) {
    frame.points.push_back({
        .layer = ViewportOverlayLayer::ToolHandle,
        .position = points[index],
        .color = kOverlayToolHandleColor,
        .size_pixels = kOverlayDrawnPlanePointSizePixels,
        .style_role = ViewportOverlayStyleRole::ToolHandle,
        .source = ViewportOverlaySource::Tool,
    });
  }

  if (!has_line || count < 2U) {
    return;
  }

  frame.lines.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .start = points[0],
      .end = points[1],
      .color = kOverlayToolHandleColor,
      .width_pixels = kOverlayDrawnPlaneLineWidthPixels,
      .style_role = ViewportOverlayStyleRole::ToolHandle,
      .source = ViewportOverlaySource::Tool,
  });

  DrawnPlanePreviewFrame plane_frame;
  if (!build_drawn_plane_preview_frame(points[0], points[1], support,
                                       plane_frame)) {
    return;
  }

  const QVec3 side = plane_frame.side * plane_frame.line_length;
  const std::array<QVec3, 4> corners{{
      plane_frame.line_start,
      plane_frame.line_end,
      plane_frame.line_end + side,
      plane_frame.line_start + side,
  }};

  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::ToolPlane,
      .a = corners[0],
      .b = corners[1],
      .c = corners[2],
      .color = kOverlayToolPlaneColor,
      .style_role = ViewportOverlayStyleRole::ToolPlaneFill,
      .source = ViewportOverlaySource::Tool,
  });
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::ToolPlane,
      .a = corners[0],
      .b = corners[2],
      .c = corners[3],
      .color = kOverlayToolPlaneColor,
      .style_role = ViewportOverlayStyleRole::ToolPlaneFill,
      .source = ViewportOverlaySource::Tool,
  });
  for (std::size_t index = 0; index < corners.size(); ++index) {
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::ToolPlaneLine,
        .start = corners[index],
        .end = corners[(index + 1U) % corners.size()],
        .color = kOverlayToolPlaneLineColor,
        .width_pixels = 1.0f,
        .style_role = ViewportOverlayStyleRole::ToolPlaneLine,
        .source = ViewportOverlaySource::Tool,
    });
  }
}

} // namespace quader
