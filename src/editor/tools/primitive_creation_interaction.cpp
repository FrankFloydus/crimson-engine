////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/primitive_creation_interaction.hpp"

namespace quader {
namespace {

ViewportBoundsResizeState bounds_state(const ViewportPrimitiveCreationState &state) {
  return {.local_min = state.local_min,
          .local_max = state.local_max,
          .world_origin = state.world_origin,
          .world_x = state.world_x,
          .world_y = state.world_y,
          .world_z = state.world_z,
          .revision = state.revision};
}

} // namespace

const PrimitiveCreationControlsFrame &PrimitiveCreationInteraction::project(
    const std::optional<ViewportPrimitiveCreationState> &cube,
    const ViewportCameraController &camera, float raster_scale) {
  cached_cube_ = cube;
  bounds_.project(cube ? std::optional{bounds_state(*cube)} : std::nullopt,
                   camera, raster_scale);
  update_frame();
  return frame_;
}

const PrimitiveCreationControlsFrame &PrimitiveCreationInteraction::project_presentation(
    const std::optional<ViewportPrimitiveCreationState> &cube,
    const ViewportCameraController &camera, float raster_scale,
    const PrimitiveCreationControlsFrame &interaction_frame) {
  cached_cube_ = cube;
  bounds_.project_presentation(
      cube ? std::optional{bounds_state(*cube)} : std::nullopt,
      camera, raster_scale, interaction_frame.bounds);
  update_frame();
  return frame_;
}

void PrimitiveCreationInteraction::update_frame() {
  const auto &bounds = bounds_.frame();
  if (frame_.revision == bounds.revision) return;
  frame_.bounds = bounds;
  frame_.hovered = bounds.hovered;
  frame_.active = bounds.active;
  frame_.visible = bounds.visible;
  frame_.revision = bounds.revision;
  frame_.controls = {};
  for (int index = 0; index < 6; ++index) {
    const auto &handle = bounds.handles[index];
    QVec3 axis{};
    if (handle.axis == 0) axis.x = 1.0f;
    else if (handle.axis == 1) axis.y = 1.0f;
    else axis.z = 1.0f;
    frame_.controls[index] = {
        .world_position = handle.world_position,
        .screen_position = handle.screen_position,
        .depth = handle.depth,
        .radius_pixels = handle.radius_pixels,
        .visible = handle.visible && bounds.visible,
        .enabled = handle.enabled,
        .resize_axis = axis,
        .resize_control = static_cast<ViewportPrimitiveResizeControl>(index),
    };
  }
  update_feedback();
}

void PrimitiveCreationInteraction::update_feedback() {
  frame_.highlight_point_count = 0;
  frame_.label_key = {};
  const int index = frame_.active >= 0 ? frame_.active : frame_.hovered;
  if (!frame_.visible || !cached_cube_ || index < 0 || index >= 6) return;
  const auto &handle = frame_.bounds.handles[index];
  if (!handle.visible) return;
  constexpr std::array<std::string_view, 3> kDimensions{
      "primitive.controls.width", "primitive.controls.height", "primitive.controls.depth"};
  frame_.label_key = kDimensions[handle.axis];
  frame_.label_value = length(handle.world_position - handle.opposite_world_position);
  frame_.label_is_count = false;
  frame_.label_position = handle.screen_position +
      QVec2{handle.radius_pixels + 8.0f * frame_.bounds.raster_scale, -handle.radius_pixels};
}

int PrimitiveCreationInteraction::hit_test(QVec2 point) const {
  return bounds_.hit_test(point);
}

void PrimitiveCreationInteraction::hover(int control) {
  bounds_.hover(control);
  update_frame();
}

void PrimitiveCreationInteraction::set_subdivision_decrement(bool) {}
void PrimitiveCreationInteraction::clear_segment_wheel() {}
std::optional<int> PrimitiveCreationInteraction::wheel_segment_value(int, double) {
  return std::nullopt;
}

void PrimitiveCreationInteraction::begin(
    int control, const ViewportPrimitiveCreationState &cube,
    const ViewportCameraController &camera, QVec2 press, bool) {
  cached_cube_ = cube;
  object_id_ = cube.object_id;
  shape_ = cube.shape;
  sphere_type_ = cube.sphere_type;
  bounds_.begin(control, bounds_state(cube), camera, press);
  update_frame();
}

std::optional<double> PrimitiveCreationInteraction::displacement(
    const ViewportCameraController &camera, QVec2 point) const {
  const auto world = bounds_.displacement(camera, point);
  if (!world || frame_.active < 0) return std::nullopt;
  const double sign = frame_.bounds.handles[frame_.active].positive ? 1.0 : -1.0;
  return *world * sign / bounds_.axis_world_scale();
}

ViewportPointerCursor PrimitiveCreationInteraction::pointer_cursor(bool drag_started) const {
  const int index = frame_.active >= 0 ? frame_.active : frame_.hovered;
  if (!frame_.visible || index < 0 || index >= 6 ||
      !frame_.controls[index].visible || !frame_.controls[index].enabled) {
    return ViewportPointerCursor::Default;
  }
  return frame_.active >= 0 && drag_started ? ViewportPointerCursor::ClosedHand
                                           : ViewportPointerCursor::OpenHand;
}

void PrimitiveCreationInteraction::end() {
  bounds_.end();
  update_frame();
}

void PrimitiveCreationInteraction::clear() {
  bounds_.clear();
  cached_cube_.reset();
  update_frame();
}

} // namespace quader
