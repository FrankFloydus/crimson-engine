////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"

#include "render/overlay/viewport_overlay_geometry.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace quader {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCircleTau = kPi * 2.0f;
constexpr float kGizmoCircleSize = 1.1f;
constexpr float kGizmoArrowSize = 0.35f;
constexpr float kGizmoArrowOffset = kGizmoCircleSize + 0.3f;
constexpr float kGizmoArrowTip = kGizmoArrowOffset + kGizmoArrowSize;
constexpr float kGizmoArrowConeRadius = 0.065f;
constexpr float kGizmoScaleOffset = kGizmoCircleSize + 0.3f;
constexpr float kGizmoScaleTip = kGizmoScaleOffset * 1.11f;
constexpr float kGizmoScaleHalfWidth = 0.07f;
constexpr int kConeSegments = 16;
constexpr int kTrackballDiskSegments = 64;

std::uint8_t channel(std::uint32_t color, int shift) {
    return static_cast<std::uint8_t>((color >> shift) & 0xffu);
}

std::uint8_t scaled_byte(std::uint8_t value, float scale) {
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(static_cast<float>(value) * scale), 0l, 255l));
}

std::uint32_t multiply_color(std::uint32_t color, float rgb_scale,
                             float alpha_scale = 1.0f) {
  return rgba8(scaled_byte(channel(color, 0), rgb_scale),
               scaled_byte(channel(color, 8), rgb_scale),
               scaled_byte(channel(color, 16), rgb_scale),
               scaled_byte(channel(color, 24), alpha_scale));
}

std::uint32_t color_for_axis(RenderTransformAxis axis) {
  switch (axis) {
  case RenderTransformAxis::X:
    return kOverlayToolAxisXColor;
  case RenderTransformAxis::Y:
    return kOverlayToolAxisYColor;
  case RenderTransformAxis::Z:
    return kOverlayToolAxisZColor;
  case RenderTransformAxis::Xy:
  case RenderTransformAxis::Xz:
  case RenderTransformAxis::Yz:
  case RenderTransformAxis::All:
  case RenderTransformAxis::None:
    return kOverlayToolAxisMixedColor;
  }
  return kOverlayToolAxisMixedColor;
}

std::uint32_t color_for_plane(RenderTransformAxis axis) {
  switch (axis) {
  case RenderTransformAxis::Xy:
    return kOverlayToolPlaneXYColor;
  case RenderTransformAxis::Xz:
    return kOverlayToolPlaneXZColor;
  case RenderTransformAxis::Yz:
    return kOverlayToolPlaneYZColor;
  default:
    return rgba8(255, 255, 255, 255);
  }
}

std::uint32_t highlight_color_for_axis(RenderTransformAxis axis) {
  switch (axis) {
  case RenderTransformAxis::X:
  case RenderTransformAxis::Yz:
    return kOverlayToolAxisXHighlightColor;
  case RenderTransformAxis::Y:
  case RenderTransformAxis::Xz:
    return kOverlayToolAxisYHighlightColor;
  case RenderTransformAxis::Z:
  case RenderTransformAxis::Xy:
    return kOverlayToolAxisZHighlightColor;
  case RenderTransformAxis::All:
  case RenderTransformAxis::None:
    return kOverlayToolAxisHighlightColor;
  }
  return kOverlayToolAxisHighlightColor;
}

bool axis_matches(RenderTransformAxis expected, RenderTransformAxis actual) {
  return expected != RenderTransformAxis::None &&
         expected == actual;
}

bool highlighted(
        const RenderTransformGizmoRecord& frame, RenderTransformAxis axis) {
  return axis_matches(frame.info.active_axis, axis) ||
         axis_matches(frame.info.hover_axis, axis);
}

std::uint32_t
authored_axis_color(const RenderTransformGizmoRecord &frame,
                    RenderTransformAxis axis,
                    float alpha = 1.0f) {
  return highlighted(frame, axis)
             ? highlight_color_for_axis(axis)
             : multiply_color(color_for_axis(axis), 1.0f, alpha);
}

float visual_width_scale(const RenderTransformGizmoRecord &frame) {
  return std::clamp(static_cast<float>(frame.info.gizmo_scale), 0.75f, 1.5f);
}

bool drag_axis_includes(RenderTransformAxis active_axis,
                        RenderTransformAxis axis) {
  using TransformAxis = RenderTransformAxis;
  if (active_axis == TransformAxis::None || active_axis == axis) {
    return true;
  }
  if (active_axis == TransformAxis::Xy) {
    return axis == TransformAxis::X || axis == TransformAxis::Y;
  }
  if (active_axis == TransformAxis::Xz) {
    return axis == TransformAxis::X || axis == TransformAxis::Z;
  }
  if (active_axis == TransformAxis::Yz) {
    return axis == TransformAxis::Y || axis == TransformAxis::Z;
  }
  return false;
}

bool drag_plane_includes(RenderTransformAxis active_axis,
                         RenderTransformAxis plane_axis) {
  return active_axis == RenderTransformAxis::None ||
         active_axis == plane_axis;
}

float signed_area(const std::array<QVec2, 4> &points) {
  float area = 0.0f;
  for (std::size_t index = 0; index < points.size(); ++index) {
    const QVec2 a = points[index];
    const QVec2 b = points[(index + 1u) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return area * 0.5f;
}

ViewportOverlayLine gizmo_line(QVec3 start, QVec3 end, std::uint32_t color,
                               ViewportOverlayLayer layer, float width_pixels) {
  return {
      .layer = layer,
      .start = start,
      .end = end,
      .color = color,
      .width_pixels = width_pixels,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  };
}

void append_line(ViewportOverlayFrame &overlay_frame, QVec3 start, QVec3 end,
                 std::uint32_t color, ViewportOverlayLayer layer,
                 float width_pixels, bool underpaint = false) {
  const float clamped_width = std::max(width_pixels, 1.0f);
  if (underpaint) {
    overlay_frame.lines.push_back(gizmo_line(
        start, end, kOverlayToolLineShadowColor, layer, clamped_width + 1.6f));
  }
  overlay_frame.lines.push_back(
      gizmo_line(start, end, color, layer, clamped_width));
}

void append_triangle(
    ViewportOverlayFrame &overlay_frame, QVec3 a, QVec3 b, QVec3 c,
    std::uint32_t color,
    ViewportOverlayLayer layer = ViewportOverlayLayer::ToolHandle) {
  overlay_frame.triangles.push_back({
      .layer = layer,
      .a = a,
      .b = b,
      .c = c,
      .color = color,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
}

void append_quad(
    ViewportOverlayFrame &overlay_frame, QVec3 a, QVec3 b, QVec3 c, QVec3 d,
    std::uint32_t color,
    ViewportOverlayLayer layer = ViewportOverlayLayer::ToolHandle) {
  append_triangle(overlay_frame, a, b, c, color, layer);
  append_triangle(overlay_frame, a, c, d, color, layer);
}

float axis_world_length(
    const RenderTransformGizmoAxis &axis) {
  return length(axis.world_tip - axis.world_start);
}

float first_axis_world_length(
    const RenderTransformGizmoRecord &frame) {
  if (frame.axes.empty()) {
    return 0.001f;
  }
  return std::max(axis_world_length(frame.axes.front()), 0.001f);
}

float rotation_ring_radius_world(
    const RenderTransformGizmoRecord &frame, RenderTransformAxis axis) {
  float radius = 0.0f;
  for (const RenderTransformGizmoRingSegment &segment :
       frame.ring_segments) {
    if (segment.axis != axis) {
      continue;
    }
    radius = std::max(radius, length(segment.world_start - frame.info.pivot));
    radius = std::max(radius, length(segment.world_end - frame.info.pivot));
  }
  return std::max(radius, 0.001f);
}

QVec3 projected_onto_plane(QVec3 value, QVec3 normal) {
  return value - normal * dot(value, normal);
}

QVec3 rotate_around_axis(QVec3 value, QVec3 axis, float angle_radians) {
  const float c = std::cos(angle_radians);
  const float s = std::sin(angle_radians);
  return value * c + cross(axis, value) * s +
         axis * (dot(axis, value) * (1.0f - c));
}

QVec3 normalized_rotate_indicator_start_direction(
    const RenderTransformGizmoRotateIndicator &indicator) {
  const QVec3 start =
      projected_onto_plane(indicator.start_direction, indicator.world_axis);
  const float start_length = length(start);
  if (start_length > 0.000001f) {
    return start / start_length;
  }
  return viewport_overlay_fallback_perpendicular(indicator.world_axis);
}

void append_plane(ViewportOverlayFrame &overlay_frame,
                  const RenderTransformGizmoPlane &plane,
                  std::uint32_t color) {
  const std::array<QVec2, 4> screen_points{{
      plane.screen0,
      plane.screen1,
      plane.screen2,
      plane.screen3,
  }};
  if (std::abs(signed_area(screen_points)) <= 2.0f) {
    return;
  }

  append_triangle(overlay_frame, plane.world0, plane.world1, plane.world2,
                  color, ViewportOverlayLayer::ToolPlane);
  append_triangle(overlay_frame, plane.world0, plane.world2, plane.world3,
                  color, ViewportOverlayLayer::ToolPlane);
  append_line(overlay_frame, plane.world0, plane.world1, color,
              ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
  append_line(overlay_frame, plane.world1, plane.world2, color,
              ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
  append_line(overlay_frame, plane.world2, plane.world3, color,
              ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
  append_line(overlay_frame, plane.world3, plane.world0, color,
              ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
}

void append_move_center_handle(
    ViewportOverlayFrame &overlay_frame,
    const RenderTransformGizmoRecord &gizmo_frame,
    const RenderTransformGizmoPlane &center, float width_scale) {
  const std::uint32_t color = highlighted(gizmo_frame, center.axis)
                                  ? kOverlayToolAxisHighlightColor
                                  : kOverlayTransformGizmoMoveCenterColor;
  const float border_width =
      kOverlayTransformGizmoMoveCenterBorderWidthPixels * width_scale;
  append_line(overlay_frame, center.world0, center.world1, color,
              ViewportOverlayLayer::ToolHandle, border_width);
  append_line(overlay_frame, center.world1, center.world2, color,
              ViewportOverlayLayer::ToolHandle, border_width);
  append_line(overlay_frame, center.world2, center.world3, color,
              ViewportOverlayLayer::ToolHandle, border_width);
  append_line(overlay_frame, center.world3, center.world0, color,
              ViewportOverlayLayer::ToolHandle, border_width);
  overlay_frame.points.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .position = gizmo_frame.info.pivot,
      .color = color,
      .size_pixels =
          kOverlayTransformGizmoMoveCenterDotSizePixels * width_scale,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
}

void append_cone_handle(ViewportOverlayFrame &overlay_frame, QVec3 base_center,
                        QVec3 tip, std::uint32_t color, float radius_world) {
  QVec3 axis = tip - base_center;
  const float cone_length = length(axis);
  if (cone_length <= 0.000001f || radius_world <= 0.000001f) {
    return;
  }
  axis = axis / cone_length;
  QVec3 u = viewport_overlay_fallback_perpendicular(axis);
  QVec3 v = cross(axis, u);
  if (viewport_overlay_vec3_tiny(v)) {
    v = viewport_overlay_fallback_perpendicular(u);
  } else {
    v = normalize(v);
  }
  u = normalize(cross(v, axis));

  std::array<QVec3, kConeSegments> base_points = {};
  for (int i = 0; i < kConeSegments; ++i) {
    const float angle =
        kCircleTau * static_cast<float>(i) / static_cast<float>(kConeSegments);
    base_points[static_cast<std::size_t>(i)] =
        base_center +
        (u * std::cos(angle) + v * std::sin(angle)) * radius_world;
  }

  for (int i = 0; i < kConeSegments; ++i) {
    const int next = (i + 1) % kConeSegments;
    const QVec3 a = base_points[static_cast<std::size_t>(i)];
    const QVec3 b = base_points[static_cast<std::size_t>(next)];
    append_triangle(overlay_frame, tip, a, b, color);
    append_triangle(overlay_frame, base_center, b, a, color);
  }
}

void append_arrow_handle(ViewportOverlayFrame &overlay_frame, QVec3 start,
                         QVec3 tip, std::uint32_t color,
                         float line_width_pixels) {
  QVec3 direction = tip - start;
  const float axis_length = length(direction);
  if (axis_length <= 0.000001f) {
    return;
  }
  direction = direction / axis_length;
  const float unit_world = axis_length / kGizmoArrowTip;
  const QVec3 cone_base = start + direction * (unit_world * kGizmoArrowOffset);
  append_line(overlay_frame, start, cone_base, color,
              ViewportOverlayLayer::ToolHandle, line_width_pixels);
  append_cone_handle(overlay_frame, cone_base, tip, color,
                     unit_world * kGizmoArrowConeRadius);
}

void append_square_prism(ViewportOverlayFrame &overlay_frame, QVec3 start,
                         QVec3 end, float half_width_world,
                         std::uint32_t color) {
  QVec3 axis = end - start;
  const float length_world = length(axis);
  if (length_world <= 0.000001f || half_width_world <= 0.000001f) {
    return;
  }
  axis = axis / length_world;
  QVec3 side_a = viewport_overlay_fallback_perpendicular(axis);
  QVec3 side_b =
      viewport_overlay_normalize_or(cross(axis, side_a), {0.0f, 1.0f, 0.0f});
  side_a =
      viewport_overlay_normalize_or(cross(side_b, axis), {1.0f, 0.0f, 0.0f});

  const std::array<QVec3, 4> start_corners{{
      start + side_a * half_width_world + side_b * half_width_world,
      start - side_a * half_width_world + side_b * half_width_world,
      start - side_a * half_width_world - side_b * half_width_world,
      start + side_a * half_width_world - side_b * half_width_world,
  }};
  const std::array<QVec3, 4> end_corners{{
      end + side_a * half_width_world + side_b * half_width_world,
      end - side_a * half_width_world + side_b * half_width_world,
      end - side_a * half_width_world - side_b * half_width_world,
      end + side_a * half_width_world - side_b * half_width_world,
  }};

  for (int index = 0; index < 4; ++index) {
    const int next = (index + 1) % 4;
    append_quad(overlay_frame, start_corners[static_cast<std::size_t>(index)],
                start_corners[static_cast<std::size_t>(next)],
                end_corners[static_cast<std::size_t>(next)],
                end_corners[static_cast<std::size_t>(index)], color);
  }
  append_quad(overlay_frame, start_corners[0], start_corners[1],
              start_corners[2], start_corners[3], color);
  append_quad(overlay_frame, end_corners[3], end_corners[2], end_corners[1],
              end_corners[0], color);
}

void append_scale_handle(ViewportOverlayFrame &overlay_frame, QVec3 start,
                         QVec3 tip, std::uint32_t color,
                         float line_width_pixels) {
  QVec3 direction = tip - start;
  const float axis_length = length(direction);
  if (axis_length <= 0.000001f) {
    return;
  }
  direction = direction / axis_length;
  const float unit_world = axis_length / kGizmoScaleTip;
  const QVec3 block_start =
      start + direction * (unit_world * kGizmoScaleOffset);
  append_line(overlay_frame, start, block_start, color,
              ViewportOverlayLayer::ToolHandle, line_width_pixels);
  append_square_prism(overlay_frame, block_start, tip,
                      unit_world * kGizmoScaleHalfWidth, color);
}

void append_cube_face_outline(ViewportOverlayFrame &overlay_frame,
                              const std::array<QVec3, 8> &corners,
                              const std::array<int, 4> &face,
                              std::uint32_t color) {
  append_line(overlay_frame, corners[static_cast<std::size_t>(face[0])],
              corners[static_cast<std::size_t>(face[1])], color,
              ViewportOverlayLayer::ToolHandle, 1.0f, false);
  append_line(overlay_frame, corners[static_cast<std::size_t>(face[1])],
              corners[static_cast<std::size_t>(face[2])], color,
              ViewportOverlayLayer::ToolHandle, 1.0f, false);
  append_line(overlay_frame, corners[static_cast<std::size_t>(face[2])],
              corners[static_cast<std::size_t>(face[3])], color,
              ViewportOverlayLayer::ToolHandle, 1.0f, false);
  append_line(overlay_frame, corners[static_cast<std::size_t>(face[3])],
              corners[static_cast<std::size_t>(face[0])], color,
              ViewportOverlayLayer::ToolHandle, 1.0f, false);
}

void append_cube_handle(ViewportOverlayFrame &overlay_frame,
                        const ViewportOverlayCamera &camera, QVec3 center,
                        float world_size, std::uint32_t color, bool hovered) {
  if (world_size <= 0.000001f) {
    return;
  }

  const float half = world_size * 0.5f;
  const std::array<QVec3, 8> corners{{
      center + QVec3{-half, -half, -half},
      center + QVec3{half, -half, -half},
      center + QVec3{half, half, -half},
      center + QVec3{-half, half, -half},
      center + QVec3{-half, -half, half},
      center + QVec3{half, -half, half},
      center + QVec3{half, half, half},
      center + QVec3{-half, half, half},
  }};
  constexpr std::array<std::array<int, 4>, 6> kFaces{{
      {{1, 5, 6, 2}},
      {{4, 0, 3, 7}},
      {{3, 2, 6, 7}},
      {{4, 5, 1, 0}},
      {{5, 4, 7, 6}},
      {{0, 1, 2, 3}},
  }};
  const std::array<QVec3, 6> normals{{
      {1.0f, 0.0f, 0.0f},
      {-1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f},
      {0.0f, -1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, -1.0f},
  }};

  const QVec3 view_direction =
      viewport_overlay_normalize_or(camera.eye - center, {0.0f, 0.0f, 1.0f});
  const QVec3 light_direction = viewport_overlay_normalize_or(
      {-0.45f, -0.65f, 0.62f}, {0.0f, 0.0f, 1.0f});

  for (std::size_t face_index = 0; face_index < kFaces.size(); ++face_index) {
    const float facing = dot(normals[face_index], view_direction);
    if (facing <= 0.000001f) {
      continue;
    }
    const float lit = std::max(dot(normals[face_index], light_direction), 0.0f);
    const float shade = std::clamp(
        0.46f + lit * 0.36f + std::min(facing, 1.0f) * 0.18f, 0.42f, 1.12f);
    const std::uint32_t fill_color =
        multiply_color(color, hovered ? shade * 1.15f : shade);
    const std::array<int, 4> &face = kFaces[face_index];
    append_triangle(overlay_frame, corners[static_cast<std::size_t>(face[0])],
                    corners[static_cast<std::size_t>(face[1])],
                    corners[static_cast<std::size_t>(face[2])], fill_color);
    append_triangle(overlay_frame, corners[static_cast<std::size_t>(face[0])],
                    corners[static_cast<std::size_t>(face[2])],
                    corners[static_cast<std::size_t>(face[3])], fill_color);
    if (hovered) {
      append_cube_face_outline(overlay_frame, corners, face,
                               rgba8(255, 246, 128, 180));
    }
  }
}

void append_rotate_indicator(
    ViewportOverlayFrame &overlay_frame,
    const RenderTransformGizmoRecord &gizmo_frame) {
  const RenderTransformGizmoRotateIndicator &indicator =
      gizmo_frame.info.rotate_indicator;
  if (!indicator.active ||
      indicator.axis == RenderTransformAxis::None ||
      !std::isfinite(indicator.angle_degrees)) {
    return;
  }

  const float axis_length = length(indicator.world_axis);
  if (axis_length <= 0.000001f) {
    return;
  }
  const QVec3 plane_normal = indicator.world_axis / axis_length;
  const QVec3 start_direction =
      normalized_rotate_indicator_start_direction(indicator);
  const float angle_radians = static_cast<float>(
      indicator.angle_degrees * static_cast<double>(kPi) / 180.0);
  const QVec3 current_direction = viewport_overlay_normalize_or(
      rotate_around_axis(start_direction, plane_normal, angle_radians),
      start_direction);
  const float ring_radius =
      rotation_ring_radius_world(gizmo_frame, indicator.axis);
  const float pie_radius = ring_radius * 0.98f;
  const int arc_steps = std::clamp(
      static_cast<int>(std::abs(angle_radians) / (kPi / 24.0f)) + 2, 3, 72);

  QVec3 previous_point = gizmo_frame.info.pivot + start_direction * pie_radius;
  if (std::abs(angle_radians) > 0.005f) {
    for (int step = 1; step <= arc_steps; ++step) {
      const float t = static_cast<float>(step) / static_cast<float>(arc_steps);
      const QVec3 direction =
          rotate_around_axis(start_direction, plane_normal, angle_radians * t);
      const QVec3 point = gizmo_frame.info.pivot + direction * pie_radius;
      append_triangle(overlay_frame, gizmo_frame.info.pivot, previous_point,
                      point, kOverlayTransformGizmoRotatePieFillColor,
                      ViewportOverlayLayer::ToolPreviewFill);
      previous_point = point;
    }
  }

  const QVec3 start_point =
      gizmo_frame.info.pivot + start_direction * pie_radius;
  const QVec3 current_point =
      gizmo_frame.info.pivot + current_direction * (ring_radius * 1.04f);
  const QVec3 mouse_point =
      indicator.has_mouse_position ? indicator.mouse_position : current_point;
  append_line(overlay_frame, gizmo_frame.info.pivot, start_point,
              kOverlayTransformGizmoRotatePieLineColor,
              ViewportOverlayLayer::ToolPreviewLine, 1.25f, false);
  append_line(overlay_frame, gizmo_frame.info.pivot, mouse_point,
              kOverlayTransformGizmoRotatePieLineColor,
              ViewportOverlayLayer::ToolPreviewLine, 1.55f, false);
}

void append_trackball_surface(
    ViewportOverlayFrame &overlay_frame,
    const RenderTransformGizmoRecord &gizmo_frame,
    const ViewportOverlayCamera &camera) {
  if (!highlighted(gizmo_frame, RenderTransformAxis::All)) {
    return;
  }

  const float forward_length = length(camera.forward);
  if (!std::isfinite(forward_length) || forward_length <= 0.000001f ||
      !std::isfinite(length_squared(gizmo_frame.info.pivot))) {
    return;
  }
  const QVec3 normal = camera.forward / forward_length;
  const QVec3 projected_right = projected_onto_plane(camera.right, normal);
  const float right_length = length(projected_right);
  if (!std::isfinite(right_length) || right_length <= 0.000001f) {
    return;
  }
  const QVec3 right = projected_right / right_length;
  const QVec3 up = cross(right, normal);

  float radius = 0.0f;
  for (const RenderTransformGizmoRingSegment &segment :
       gizmo_frame.ring_segments) {
    const float start_radius =
        length(segment.world_start - gizmo_frame.info.pivot);
    const float end_radius =
        length(segment.world_end - gizmo_frame.info.pivot);
    if (!std::isfinite(start_radius) || !std::isfinite(end_radius)) {
      return;
    }
    radius = std::max({radius, start_radius, end_radius});
  }
  if (radius <= 0.000001f) {
    return;
  }

  const std::uint32_t color =
      gizmo_frame.info.active_axis == RenderTransformAxis::All
          ? kOverlayTransformGizmoTrackballActiveFillColor
          : kOverlayTransformGizmoTrackballHoverFillColor;
  const QVec3 first = gizmo_frame.info.pivot + right * radius;
  QVec3 previous = first;
  for (int step = 1; step <= kTrackballDiskSegments; ++step) {
    const float angle = kCircleTau * static_cast<float>(step) /
                        static_cast<float>(kTrackballDiskSegments);
    const QVec3 point =
        step == kTrackballDiskSegments
            ? first
            : gizmo_frame.info.pivot +
                  (right * std::cos(angle) + up * std::sin(angle)) * radius;
    append_triangle(overlay_frame, gizmo_frame.info.pivot, previous, point,
                    color, ViewportOverlayLayer::ToolPreviewFill);
    previous = point;
  }
}

void append_rotate_gizmo(
    ViewportOverlayFrame &overlay_frame,
    const RenderTransformGizmoRecord &gizmo_frame,
    const ViewportOverlayCamera &camera, float width_scale) {
  append_trackball_surface(overlay_frame, gizmo_frame, camera);
  const float trackball_width = 2.25f * width_scale;
  for (const RenderTransformGizmoTrackballSegment
           &segment : gizmo_frame.trackball_segments) {
    append_line(overlay_frame, segment.world_start, segment.world_end,
                kOverlayToolTrackballColor, ViewportOverlayLayer::ToolHandle,
                trackball_width);
  }

  for (const RenderTransformGizmoRingSegment &segment :
       gizmo_frame.ring_segments) {
    if (!segment.front_facing) {
      continue;
    }
    append_line(overlay_frame, segment.world_start, segment.world_end,
                authored_axis_color(gizmo_frame, segment.axis),
                ViewportOverlayLayer::ToolHandle, 3.0f * width_scale);
  }

  append_rotate_indicator(overlay_frame, gizmo_frame);
}

void append_move_or_scale_gizmo(
    ViewportOverlayFrame &overlay_frame,
    const RenderTransformGizmoRecord &gizmo_frame,
    const ViewportOverlayCamera &camera, float width_scale) {
  for (const RenderTransformGizmoPlane &plane :
       gizmo_frame.planes) {
    if (plane.axis == RenderTransformAxis::All) {
      continue;
    }
    if (!drag_plane_includes(gizmo_frame.info.active_axis, plane.axis)) {
      continue;
    }
    const std::uint32_t color = highlighted(gizmo_frame, plane.axis)
                                    ? highlight_color_for_axis(plane.axis)
                                    : color_for_plane(plane.axis);
    append_plane(overlay_frame, plane, color);
  }

  for (const RenderTransformGizmoAxis &axis :
       gizmo_frame.axes) {
    if (!drag_axis_includes(gizmo_frame.info.active_axis, axis.axis)) {
      continue;
    }
    const bool is_highlighted = highlighted(gizmo_frame, axis.axis);
    const std::uint32_t color =
        is_highlighted
            ? highlight_color_for_axis(axis.axis)
            : multiply_color(color_for_axis(axis.axis), 1.0f, axis.alpha);
    const float axis_line_width =
        kOverlayTransformGizmoAxisLineWidthPixels * width_scale;

    if (gizmo_frame.info.tool ==
        RenderTransformTool::Scale) {
      append_scale_handle(overlay_frame, axis.world_start, axis.world_tip,
                          color, axis_line_width);
    } else {
      append_arrow_handle(overlay_frame, axis.world_start, axis.world_tip,
                          color, axis_line_width);
    }
  }

  if (gizmo_frame.info.tool == RenderTransformTool::Move) {
    const auto center = std::find_if(
        gizmo_frame.planes.begin(), gizmo_frame.planes.end(),
        [](const RenderTransformGizmoPlane &plane) {
          return plane.axis == RenderTransformAxis::All;
        });
    if (center != gizmo_frame.planes.end() &&
        drag_plane_includes(gizmo_frame.info.active_axis, center->axis)) {
      append_move_center_handle(overlay_frame, gizmo_frame, *center,
                                width_scale);
    }
  }

  const bool center_hovered =
      gizmo_frame.info.hover_axis == RenderTransformAxis::All ||
      gizmo_frame.info.active_axis == RenderTransformAxis::All;
  if (gizmo_frame.info.tool ==
      RenderTransformTool::Scale) {
    if (center_hovered) {
      const float world_size =
          std::max(0.0001f, first_axis_world_length(gizmo_frame) *
                                (kOverlayTransformGizmoScaleCenterSizePixels /
                                 kOverlayTransformGizmoSizePixels));
      append_cube_handle(overlay_frame, camera, gizmo_frame.info.pivot,
                         world_size, kOverlayToolAxisHighlightColor, true);
    }
  }
}

} // namespace

void ViewportTransformGizmoOverlayBuilder::append(
    ViewportOverlayFrame &overlay_frame,
    const RenderTransformGizmoRecord &gizmo_frame,
    const ViewportOverlayCamera &camera) {
  if (!gizmo_frame.info.ok) {
    return;
  }

  const float width_scale = visual_width_scale(gizmo_frame);
  if (gizmo_frame.info.tool ==
      RenderTransformTool::Rotate) {
    append_rotate_gizmo(overlay_frame, gizmo_frame, camera, width_scale);
    return;
  }

  append_move_or_scale_gizmo(overlay_frame, gizmo_frame, camera, width_scale);
}

} // namespace quader
