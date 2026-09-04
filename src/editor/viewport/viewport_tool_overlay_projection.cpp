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
#include "editor/projection/selection_overlay_projection.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "render/overlay/viewport_box_overlay_builder.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
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
      .move_center_radius_pixels = source.info.move_center_radius_pixels,
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
                                QVec3 position, float raster_scale) {
  constexpr float kRadiusPixels = 7.0f;
  const float radius_pixels = kRadiusPixels *
                              viewport_sanitized_pixel_scale(raster_scale);
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
          {screen.position.x + radius_pixels, screen.position.y}),
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

void add_double(RenderSignatureBuilder &signature, double value) {
  signature.add_value(std::bit_cast<std::uint64_t>(value));
}

void add_camera(RenderSignatureBuilder &signature,
                const ViewportOverlayCamera *camera) {
  signature.add_bool(camera != nullptr);
  if (camera == nullptr) {
    return;
  }
  signature.add_vec3(camera->eye)
      .add_vec3(camera->forward)
      .add_vec3(camera->right)
      .add_vec3(camera->up)
      .add_vec2(camera->viewport_size)
      .add_value(static_cast<std::uint64_t>(camera->projection))
      .add_float(camera->orthographic_size)
      .add_float(camera->fov_degrees)
      .add_float(camera->near_plane)
      .add_float(camera->far_plane);
}

void add_box_preview(RenderSignatureBuilder &signature,
                     const ModelingBoxPreview *preview) {
  signature.add_bool(preview != nullptr);
  if (preview == nullptr) {
    return;
  }
  signature.add_bool(preview->valid)
      .add_value(static_cast<std::uint64_t>(preview->drag_state))
      .add_size(preview->footprint_count);
  for (const QVec3 point : preview->footprint) {
    signature.add_vec3(point);
  }
}

void add_gizmo_info(
    RenderSignatureBuilder &signature,
    const quader_editor_core::TransformGizmoFrame *gizmo) {
  signature.add_bool(gizmo != nullptr);
  if (gizmo == nullptr) {
    return;
  }
  const quader_editor_core::TransformGizmoFrameInfo &info = gizmo->info;
  signature.add_bool(info.ok)
      .add_value(static_cast<std::uint64_t>(info.tool))
      .add_value(static_cast<std::uint64_t>(info.hover_axis))
      .add_value(static_cast<std::uint64_t>(info.active_axis))
      .add_vec3(info.pivot)
      .add_vec2(info.screen_pivot);
  add_double(signature, info.gizmo_scale);
  add_double(signature, info.move_center_radius_pixels);
  signature.add_bool(info.rotate_indicator.active)
      .add_value(static_cast<std::uint64_t>(info.rotate_indicator.axis))
      .add_vec3(info.rotate_indicator.world_axis)
      .add_vec3(info.rotate_indicator.start_direction)
      .add_bool(info.rotate_indicator.has_mouse_position)
      .add_vec3(info.rotate_indicator.mouse_position);
  add_double(signature, info.rotate_indicator.angle_degrees);
  // Geometry is deterministically derived from the fixed owner revisions,
  // camera and info above. Sizes protect structural changes without scanning
  // the generated gizmo vectors before the coordinator's unchanged gate.
  signature.add_size(gizmo->axes.size())
      .add_size(gizmo->planes.size())
      .add_size(gizmo->ring_segments.size())
      .add_size(gizmo->trackball_segments.size());
}

void add_snap_points(
    RenderSignatureBuilder &signature,
    const std::vector<quader_editor_core::SelectionOverlayPoint> *points) {
  signature.add_bool(points != nullptr);
  if (points == nullptr) {
    return;
  }
  assert(points->size() <= 2u &&
         "Transform gizmo snap overlay must remain a fixed hover/target pair");
  signature.add_size(points->size());
  // TransformGizmoInteraction publishes either no snap marker or the fixed
  // hover/target pair. Read those two bounded records directly; never hash a
  // dynamically sized overlay stream on an unchanged frame.
  const auto add_point = [&signature](
                             const quader_editor_core::SelectionOverlayPoint
                                 &point) {
    signature.add_value(static_cast<std::uint64_t>(point.layer))
        .add_vec3(point.position)
        .add_value(point.color_abgr)
        .add_float(point.size_pixels)
        .add_value(static_cast<std::uint64_t>(point.object_id))
        .add_value(static_cast<std::uint64_t>(point.vertex_id));
  };
  if (!points->empty()) {
    add_point((*points)[0]);
  }
  if (points->size() > 1u) {
    add_point((*points)[1]);
  }
}

void add_selection_marquee(RenderSignatureBuilder &signature,
                           const ViewportSelectionMarquee *marquee) {
  signature.add_bool(marquee != nullptr);
  if (marquee == nullptr) {
    return;
  }
  signature.add_bool(marquee->active)
      .add_vec2(marquee->start)
      .add_vec2(marquee->end)
      .add_value(static_cast<std::uint64_t>(marquee->rule));
}

void add_pivot_handle(RenderSignatureBuilder &signature,
                      const PivotHandleState *handle) {
  signature.add_bool(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  signature.add_bool(handle->visible)
      .add_bool(handle->editing)
      .add_bool(handle->persistent)
      .add_value(handle->target)
      .add_vec3(handle->world_position);
}

void add_linear_array_handle(RenderSignatureBuilder &signature,
                             const LinearArrayHandleState *handle) {
  signature.add_bool(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  signature.add_bool(handle->visible)
      .add_bool(handle->dragging)
      .add_vec3(handle->source_world)
      .add_vec3(handle->endpoint_world)
      .add_value(static_cast<std::uint64_t>(handle->constraint))
      .add_value(handle->revision);
}

void add_radial_array_center(RenderSignatureBuilder &signature,
                             const RadialArrayCenterState *center) {
  signature.add_bool(center != nullptr);
  if (center == nullptr) {
    return;
  }
  signature.add_bool(center->visible)
      .add_bool(center->locked)
      .add_vec3(center->world_position)
      .add_vec3(center->world_normal)
      .add_value(center->revision);
}

std::uint64_t tool_overlay_publication_revision(
    const ViewportToolOverlaySourceInputs &inputs) {
  RenderSignatureBuilder signature;
  const ViewportToolOverlaySourceRevisions &revisions = inputs.revisions;
  signature.add_bool(inputs.interaction_pane)
      .add_bool(inputs.tool_overlay_visible)
      .add_bool(inputs.gizmos_visible)
      .add_bool(inputs.pivot_handle_editing)
      .add_bool(inputs.overlays_visible)
      .add_bool(inputs.radial_array_center_visible)
      .add_float(viewport_sanitized_pixel_scale(inputs.raster_scale));
  const bool transform_gizmo_visible =
      inputs.interaction_pane && inputs.gizmos_visible &&
      !inputs.pivot_handle_editing && inputs.transform_gizmo != nullptr;
  const bool marquee_visible =
      inputs.interaction_pane && inputs.overlays_visible &&
      inputs.selection_marquee != nullptr && inputs.selection_marquee->active;
  const bool pivot_visible =
      inputs.pivot_handle != nullptr && inputs.pivot_handle->visible;
  const bool camera_affects_frame =
      (inputs.interaction_pane && inputs.tool_overlay_visible) ||
      transform_gizmo_visible || marquee_visible || pivot_visible;
  signature.add_bool(camera_affects_frame);
  if (camera_affects_frame) {
    add_camera(signature, inputs.overlay_camera);
  }
  if (inputs.interaction_pane) {
    signature.add_value(revisions.active_tool);
    if (inputs.tool_overlay_visible) {
      add_box_preview(signature, inputs.box_preview);
    }
    if (transform_gizmo_visible) {
      signature.add_value(revisions.modeling_content)
          .add_value(revisions.modeling_selection)
          .add_value(revisions.modeling_transform_preview)
          .add_value(revisions.scene_transform)
          .add_value(revisions.scene_selection)
          .add_value(revisions.transform_space)
          .add_value(revisions.selection_pivot)
          .add_value(revisions.curve_preview);
      add_gizmo_info(signature, inputs.transform_gizmo);
    }
    if (inputs.overlays_visible) {
      add_snap_points(signature, inputs.transform_snap_points);
      add_selection_marquee(signature, inputs.selection_marquee);
    }
  }
  add_pivot_handle(signature, inputs.pivot_handle);
  add_linear_array_handle(signature, inputs.linear_array_handle);
  if (inputs.radial_array_center_visible) {
    add_radial_array_center(signature, inputs.radial_array_center);
  }
  return signature.value();
}

} // namespace

ViewportToolOverlaySource::ViewportToolOverlaySource(
    ViewportToolOverlaySourceInputs inputs)
    : inputs_(inputs),
      source_stamp_{
          .source_identity = inputs.source_identity,
          .publication_revision = tool_overlay_publication_revision(inputs),
      } {}

ViewportOverlayFrame
ViewportToolOverlaySource::build_dynamic_tool_overlay_frame() const {
  ViewportOverlayFrame frame;
  if (inputs_.overlay_camera == nullptr) {
    return frame;
  }
  if (inputs_.interaction_pane) {
    if (inputs_.tool_overlay_visible && inputs_.box_preview != nullptr) {
      ViewportToolOverlayProjection::append_box(
          frame, *inputs_.overlay_camera, *inputs_.box_preview,
          inputs_.raster_scale);
    }
    if (inputs_.gizmos_visible && !inputs_.pivot_handle_editing &&
        inputs_.transform_gizmo != nullptr) {
      ViewportToolOverlayProjection::append_transform_gizmo(
          frame, *inputs_.overlay_camera, *inputs_.transform_gizmo);
    }
    if (inputs_.overlays_visible) {
      if (inputs_.transform_snap_points != nullptr) {
        SelectionOverlayProjection::append_world_space_overlay_points(
            frame, *inputs_.transform_snap_points);
      }
      if (inputs_.selection_marquee != nullptr) {
        ViewportToolOverlayProjection::append_selection_marquee(
            frame, *inputs_.overlay_camera, *inputs_.selection_marquee,
            inputs_.raster_scale);
      }
    }
  }
  if (inputs_.viewport_camera != nullptr && inputs_.pivot_handle != nullptr) {
    ViewportToolOverlayProjection::append_pivot_handle(
        frame, *inputs_.viewport_camera, *inputs_.pivot_handle,
        inputs_.raster_scale);
  }
  if (inputs_.linear_array_handle != nullptr) {
    ViewportToolOverlayProjection::append_linear_array_handle(
        frame, *inputs_.linear_array_handle);
  }
  if (inputs_.radial_array_center_visible &&
      inputs_.radial_array_center != nullptr) {
    ViewportToolOverlayProjection::append_radial_array_center(
        frame, *inputs_.radial_array_center);
  }
  return frame;
}

void ViewportToolOverlayProjection::append_box(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const ModelingBoxPreview &preview, float raster_scale) {
  ViewportBoxOverlayBuilder::append(
      frame, camera,
      RenderBoxPreviewRecord{.valid = preview.ok && preview.footprint_count >= 2U,
                             .footprint = preview.footprint,
                             .footprint_count = preview.footprint_count},
      raster_scale);
}

void ViewportToolOverlayProjection::append_transform_gizmo(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const quader_editor_core::TransformGizmoFrame &gizmo) {
  ViewportTransformGizmoOverlayBuilder::append(frame, render_gizmo(gizmo),
                                               camera);
}

void ViewportToolOverlayProjection::append_selection_marquee(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const ViewportSelectionMarquee &marquee, float raster_scale) {
  ViewportSelectionMarqueeOverlayBuilder::append(
      frame, camera,
      RenderSelectionMarqueeRecord{
          .active = marquee.active,
          .crossing = marquee.rule == ModelingBoxSelectionRule::Crossing,
          .start = marquee.start,
          .end = marquee.end,
      },
      raster_scale);
}

void ViewportToolOverlayProjection::append_pivot_handle(
    ViewportOverlayFrame &frame, const ViewportCameraController &camera,
    const PivotHandleState &handle, float raster_scale) {
  if (!handle.visible) {
    return;
  }
  const float radius =
      pivot_handle_world_radius(camera, handle.world_position, raster_scale);
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
