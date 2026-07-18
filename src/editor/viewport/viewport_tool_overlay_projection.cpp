////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_tool_overlay_projection.hpp"

#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/pivot/pivot_types.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "render/overlay/viewport_box_overlay_builder.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace quader {
namespace {

[[nodiscard]] RenderTransformAxis
render_axis(quader_editor_core::TransformAxis axis) {
  using Source = quader_editor_core::TransformAxis;
  switch (axis) {
  case Source::X:
    return RenderTransformAxis::X;
  case Source::Y:
    return RenderTransformAxis::Y;
  case Source::Z:
    return RenderTransformAxis::Z;
  case Source::Xy:
    return RenderTransformAxis::Xy;
  case Source::Xz:
    return RenderTransformAxis::Xz;
  case Source::Yz:
    return RenderTransformAxis::Yz;
  case Source::All:
    return RenderTransformAxis::All;
  case Source::None:
    return RenderTransformAxis::None;
  }
  return RenderTransformAxis::None;
}

[[nodiscard]] RenderTransformTool
render_tool(quader_editor_core::ViewportInteractionTool tool) {
  using Source = quader_editor_core::ViewportInteractionTool;
  switch (tool) {
  case Source::Move:
    return RenderTransformTool::Move;
  case Source::Rotate:
    return RenderTransformTool::Rotate;
  case Source::Scale:
    return RenderTransformTool::Scale;
  default:
    return RenderTransformTool::Other;
  }
}

[[nodiscard]] RenderTransformGizmoRecord render_gizmo(
    const quader_editor_core::TransformGizmoFrame &source) {
  RenderTransformGizmoRecord result;
  result.info = {
      .ok = source.info.ok,
      .tool = render_tool(source.info.tool),
      .hover_axis = render_axis(source.info.hover_axis),
      .active_axis = render_axis(source.info.active_axis),
      .pivot = source.info.pivot,
      .screen_pivot = source.info.screen_pivot,
      .gizmo_scale = source.info.gizmo_scale,
      .rotate_indicator = {
          .active = source.info.rotate_indicator.active,
          .axis = render_axis(source.info.rotate_indicator.axis),
          .world_axis = source.info.rotate_indicator.world_axis,
          .start_direction = source.info.rotate_indicator.start_direction,
          .has_mouse_position =
              source.info.rotate_indicator.has_mouse_position,
          .mouse_position = source.info.rotate_indicator.mouse_position,
          .angle_degrees = source.info.rotate_indicator.angle_degrees,
      },
  };
  result.axes.reserve(source.axes.size());
  for (const auto &axis : source.axes) {
    result.axes.push_back({
        .axis = render_axis(axis.axis),
        .world_start = axis.world_start,
        .world_tip = axis.world_tip,
        .screen_start = axis.screen_start,
        .screen_tip = axis.screen_tip,
        .alpha = axis.alpha,
    });
  }
  result.planes.reserve(source.planes.size());
  for (const auto &plane : source.planes) {
    result.planes.push_back({
        .axis = render_axis(plane.axis),
        .world0 = plane.world0,
        .world1 = plane.world1,
        .world2 = plane.world2,
        .world3 = plane.world3,
        .screen0 = plane.screen0,
        .screen1 = plane.screen1,
        .screen2 = plane.screen2,
        .screen3 = plane.screen3,
    });
  }
  result.ring_segments.reserve(source.ring_segments.size());
  for (const auto &segment : source.ring_segments) {
    result.ring_segments.push_back({
        .axis = render_axis(segment.axis),
        .world_start = segment.world_start,
        .world_end = segment.world_end,
        .screen_start = segment.screen_start,
        .screen_end = segment.screen_end,
        .alpha = segment.alpha,
        .front_facing = segment.front_facing,
    });
  }
  result.trackball_segments.reserve(source.trackball_segments.size());
  for (const auto &segment : source.trackball_segments) {
    result.trackball_segments.push_back({
        .world_start = segment.world_start,
        .world_end = segment.world_end,
        .screen_start = segment.screen_start,
        .screen_end = segment.screen_end,
    });
  }
  return result;
}

std::optional<QVec3> intersect_ray_plane(const ViewportRay &ray,
                                         QVec3 plane_point,
                                         QVec3 plane_normal) {
  const float denominator = dot(plane_normal, ray.direction);
  if (std::abs(denominator) <= 0.000001f) {
    return std::nullopt;
  }
  const float t = dot(plane_point - ray.origin, plane_normal) / denominator;
  return std::isfinite(t) ? std::optional<QVec3>(ray.origin + ray.direction * t)
                          : std::nullopt;
}

void append_pivot_ring(ViewportOverlayFrame &frame, QVec3 center, QVec3 axis_a,
                       QVec3 axis_b, float radius) {
  constexpr int kSegments = 32;
  constexpr float kTau = 6.2831853071795864769f;
  for (int segment = 0; segment < kSegments; ++segment) {
    const float angle0 =
        kTau * static_cast<float>(segment) / static_cast<float>(kSegments);
    const float angle1 =
        kTau * static_cast<float>(segment + 1) / static_cast<float>(kSegments);
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::ToolHandle,
        .start = center + axis_a * (std::cos(angle0) * radius) +
                 axis_b * (std::sin(angle0) * radius),
        .end = center + axis_a * (std::cos(angle1) * radius) +
               axis_b * (std::sin(angle1) * radius),
        .color = kOverlayPivotHandleColor,
        .width_pixels = 2.0f,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Tool,
    });
  }
}

float pivot_handle_world_radius(const ViewportCameraController &camera,
                                QVec3 position) {
  constexpr float kRadiusPixels = 7.0f;
  const ViewportScreenPoint screen = camera.project_world_point(position);
  if (!screen.visible) {
    return 0.08f;
  }
  const QVec3 plane_normal =
      normalize_or_zero(camera.pose().target - camera.pose().eye);
  if (length_squared(plane_normal) <= 0.000001f) {
    return 0.08f;
  }
  const std::optional<QVec3> center = intersect_ray_plane(
      camera.ray_from_viewport_point(screen.position), position, plane_normal);
  const std::optional<QVec3> edge = intersect_ray_plane(
      camera.ray_from_viewport_point(
          {screen.position.x + kRadiusPixels, screen.position.y}),
      position, plane_normal);
  return center.has_value() && edge.has_value()
             ? std::clamp(length(*edge - *center), 0.025f, 100.0f)
             : 0.08f;
}

std::uint32_t linear_array_axis_color(LinearArrayAxisConstraint constraint) {
  switch (constraint) {
  case LinearArrayAxisConstraint::X:
    return kOverlayToolAxisXColor;
  case LinearArrayAxisConstraint::Y:
    return kOverlayToolAxisYColor;
  case LinearArrayAxisConstraint::Z:
    return kOverlayToolAxisZColor;
  case LinearArrayAxisConstraint::Free:
    return kOverlayToolAxisMixedColor;
  }
  return kOverlayToolAxisMixedColor;
}

} // namespace

void ViewportToolOverlayProjection::append_box(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const ModelingBoxPreview &preview) {
  ViewportBoxOverlayBuilder::append(
      frame, camera,
      RenderBoxPreviewRecord{.valid = preview.valid,
                             .footprint = preview.footprint,
                             .footprint_count = preview.footprint_count});
}

void ViewportToolOverlayProjection::append_transform_gizmo(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const quader_editor_core::TransformGizmoFrame &gizmo) {
  ViewportTransformGizmoOverlayBuilder::append(frame, render_gizmo(gizmo),
                                               camera);
}

void ViewportToolOverlayProjection::append_selection_marquee(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const ViewportSelectionMarquee &marquee) {
  ViewportSelectionMarqueeOverlayBuilder::append(
      frame, camera,
      RenderSelectionMarqueeRecord{
          .active = marquee.active,
          .crossing = marquee.rule == ModelingBoxSelectionRule::Crossing,
          .start = marquee.start,
          .end = marquee.end,
      });
}

void ViewportToolOverlayProjection::append_pivot_handle(
    ViewportOverlayFrame &frame, const ViewportCameraController &camera,
    const PivotHandleState &handle) {
  if (!handle.visible) {
    return;
  }
  const float radius = pivot_handle_world_radius(camera, handle.world_position);
  append_pivot_ring(frame, handle.world_position, {1.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f}, radius);
  append_pivot_ring(frame, handle.world_position, {1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f}, radius);
  append_pivot_ring(frame, handle.world_position, {0.0f, 1.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f}, radius);
}

void ViewportToolOverlayProjection::append_linear_array_handle(
    ViewportOverlayFrame &frame, const LinearArrayHandleState &handle) {
  if (!handle.visible) {
    return;
  }
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .start = handle.source_world,
      .end = handle.endpoint_world,
      .color = linear_array_axis_color(handle.constraint),
      .width_pixels = 2.0f,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
  frame.points.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .position = handle.source_world,
      .color = rgba8(238, 242, 247, 255),
      .size_pixels = 8.0f,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
      .outline_growth_pixels = 1.5f,
  });
  frame.points.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .position = handle.endpoint_world,
      .color = kOverlayToolHandleColor,
      .size_pixels = handle.dragging ? 15.0f : 13.0f,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
      .outline_growth_pixels = 2.0f,
  });
}

void ViewportToolOverlayProjection::append_radial_array_center(
    ViewportOverlayFrame &frame, const RadialArrayCenterState &center) {
  if (!center.visible) {
    return;
  }
  frame.points.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .position = center.world_position,
      .color = kOverlayToolHandleColor,
      .size_pixels = center.locked ? 12.0f : 9.0f,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
      .outline_growth_pixels = center.locked ? 2.0f : 1.5f,
  });
}

RenderOverlaySelectionMode ViewportToolOverlayProjection::selection_mode(
    ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Object:
    return RenderOverlaySelectionMode::Object;
  case ModelingSelectionMode::Group:
    return RenderOverlaySelectionMode::Group;
  case ModelingSelectionMode::Vertex:
  case ModelingSelectionMode::Edge:
  case ModelingSelectionMode::Face:
    return RenderOverlaySelectionMode::Component;
  }
  return RenderOverlaySelectionMode::Object;
}

} // namespace quader
