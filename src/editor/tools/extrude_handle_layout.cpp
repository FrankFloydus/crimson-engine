////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/extrude_handle_layout.hpp"

#include "editor/interactions/transform_gizmo.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <cmath>

namespace quader {
namespace {

bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float squared_length(QVec2 value) {
  return value.x * value.x + value.y * value.y;
}

} // namespace

ExtrudeHandleLayout build_extrude_handle_layout(
    const ViewportCameraController &camera, const ExtrudeHandleState &handle,
    float raster_scale) {
  ExtrudeHandleLayout layout;
  layout.mode = handle.mode;
  layout.raster_scale = viewport_sanitized_pixel_scale(raster_scale);
  const float normal_length = length(handle.world_direction);
  if (!handle.active || handle.dragging || !finite(handle.world_position) ||
      !finite(handle.world_direction) || !std::isfinite(normal_length) ||
      normal_length < 1.0e-6F) {
    return layout;
  }
  const auto projected_pivot = camera.project_world_point(handle.world_position);
  if (!projected_pivot.visible) {
    return layout;
  }
  const auto runtime_camera =
      ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size());
  const auto world_per_pixel_at = [&](QVec3 position) {
    return static_cast<float>(quader_editor_core::viewport_world_units_per_pixel(
        runtime_camera, position));
  };
  const ViewportCameraBasis basis = ViewportCameraAdapter::basis(camera);
  layout.camera_right = basis.right;
  layout.camera_up = basis.up;
  layout.pivot = handle.world_position;
  layout.pivot_screen = projected_pivot.position;
  layout.world_units_per_pixel = world_per_pixel_at(layout.pivot);
  if (!std::isfinite(layout.world_units_per_pixel) ||
      layout.world_units_per_pixel <= 0.0F) {
    return layout;
  }

  // Scale the complete handle at its selection pivot. The disk then keeps that
  // world radius as the normal-axis offset brings it nearer to or farther from
  // the camera, preserving the same perspective as the stem.
  constexpr float kButtonRadiusPixels = 16.0F;
  constexpr float kStemRadiusMultiple = 12.0F;
  layout.button_world_radius = kButtonRadiusPixels * layout.raster_scale *
                               layout.world_units_per_pixel;
  layout.button_center = layout.pivot + handle.world_direction / normal_length *
      (layout.button_world_radius * kStemRadiusMultiple);
  if (!finite(layout.button_center) ||
      !std::isfinite(layout.button_world_radius)) {
    return layout;
  }
  const auto projected_button = camera.project_world_point(layout.button_center);
  const auto projected_rim = camera.project_world_point(
      layout.button_center + layout.camera_right * layout.button_world_radius);
  if (!projected_button.visible || !projected_rim.visible) {
    return layout;
  }
  layout.button_center_screen = projected_button.position;
  layout.button_radius_pixels = std::sqrt(squared_length(
      projected_rim.position - layout.button_center_screen));
  layout.valid = std::isfinite(layout.button_radius_pixels) &&
                 layout.button_radius_pixels > 0.0F;
  return layout;
}

ExtrudeHandlePart hit_extrude_handle(const ExtrudeHandleLayout &layout,
                                     QVec2 pointer) {
  if (!layout.valid || !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
    return ExtrudeHandlePart::None;
  }
  if (squared_length(pointer - layout.button_center_screen) <=
      layout.button_radius_pixels * layout.button_radius_pixels) {
    return ExtrudeHandlePart::NewExtrusion;
  }
  return ExtrudeHandlePart::None;
}

} // namespace quader
