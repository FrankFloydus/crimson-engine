////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/bridge_preview_handle_layout.hpp"

#include "editor/interactions/transform_gizmo.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_pixel_scale.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <cmath>
#include <limits>

namespace quader {

BridgePreviewHandleLayout build_bridge_preview_handle_layout(
    const ViewportCameraController &camera,
    const BridgePreviewHandleState &handle, float raster_scale) {
  BridgePreviewHandleLayout layout;
  constexpr float kHandleRadiusPixels = 7.0F;
  layout.radius_pixels =
      kHandleRadiusPixels * viewport_sanitized_pixel_scale(raster_scale);
  const auto basis = ViewportCameraAdapter::basis(camera);
  layout.camera_right = basis.right;
  layout.camera_up = basis.up;
  const auto runtime_camera =
      ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size());
  layout.centers = {handle.control_points[1], handle.control_points[2]};
  const std::array axes{handle.start_handle_axis, handle.end_handle_axis};
  for (std::size_t index = 0u; index < layout.centers.size(); ++index) {
    const double axis_length = length(axes[index]);
    const auto projected = camera.project_world_point(layout.centers[index]);
    const double world_per_pixel =
        quader_editor_core::viewport_world_units_per_pixel(
            runtime_camera, layout.centers[index]);
    if (!projected.visible || !std::isfinite(axis_length) ||
        axis_length <= 1.0e-8 || !std::isfinite(world_per_pixel) ||
        world_per_pixel <= 0.0) {
      continue;
    }
    layout.screen_centers[index] = projected.position;
    layout.world_radii[index] =
        static_cast<float>(world_per_pixel) * layout.radius_pixels;
    layout.visible[index] = std::isfinite(layout.world_radii[index]) &&
                            std::isfinite(projected.position.x) &&
                            std::isfinite(projected.position.y);
  }
  return layout;
}

BridgePreviewHandlePart hit_bridge_preview_handle(
    const BridgePreviewHandleLayout &layout, QVec2 pointer) {
  if (!std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
    return BridgePreviewHandlePart::None;
  }
  double closest_distance = std::numeric_limits<double>::infinity();
  BridgePreviewHandlePart part = BridgePreviewHandlePart::None;
  for (std::size_t index = 0u; index < layout.visible.size(); ++index) {
    if (!layout.visible[index]) {
      continue;
    }
    const double distance = viewport_pointer_distance_squared(
        pointer, layout.screen_centers[index]);
    if (distance <= layout.radius_pixels * layout.radius_pixels &&
        distance < closest_distance) {
      closest_distance = distance;
      part = index == 0u ? BridgePreviewHandlePart::Start
                        : BridgePreviewHandlePart::End;
    }
  }
  return part;
}

} // namespace quader
