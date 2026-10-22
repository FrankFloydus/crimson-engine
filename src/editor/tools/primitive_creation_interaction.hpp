////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/primitive_creation_controls.hpp"
#include "editor/tools/bounds_resize_interaction.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_types.hpp"

#include <optional>

namespace quader {

/** Presentation cache and immutable drag math, never a second mesh owner. */
class PrimitiveCreationInteraction final {
public:
  const PrimitiveCreationControlsFrame &project(
      const std::optional<ViewportPrimitiveCreationState> &cube,
      const ViewportCameraController &camera, float raster_scale);
  /// Retains pane-local projection while presenting the input owner's feedback.
  const PrimitiveCreationControlsFrame &project_presentation(
      const std::optional<ViewportPrimitiveCreationState> &cube,
      const ViewportCameraController &camera, float raster_scale,
      const PrimitiveCreationControlsFrame &interaction_frame);
  [[nodiscard]] int hit_test(QVec2 point) const;
  void hover(int control);
  void set_subdivision_decrement(bool decrement);
  /// Accumulates ordered physical steps for one segment control, with limits.
  [[nodiscard]] std::optional<int> wheel_segment_value(int control, double steps);
  void clear_segment_wheel();
  void begin(int control, const ViewportPrimitiveCreationState &cube,
             const ViewportCameraController &camera, QVec2 press,
             bool subdivision_decrement = false);
  [[nodiscard]] std::optional<double>
  displacement(const ViewportCameraController &camera, QVec2 point) const;
  void end();
  void clear();
  [[nodiscard]] int active() const { return frame_.active; }
  /// Presents the existing control hit and the tool's confirmed drag threshold.
  [[nodiscard]] ViewportPointerCursor pointer_cursor(bool drag_started) const;
  [[nodiscard]] std::int32_t object_id() const { return object_id_; }
  [[nodiscard]] const PrimitiveCreationControlsFrame &frame() const { return frame_; }
  [[nodiscard]] std::uint64_t projection_count() const { return bounds_.projection_count(); }
  [[nodiscard]] double axis_world_scale() const { return bounds_.axis_world_scale(); }
  [[nodiscard]] ViewportPrimitiveCreationShape shape() const { return shape_; }
  [[nodiscard]] ViewportSphereType sphere_type() const { return sphere_type_; }
  /// The visible controls may precede the first captured resize gesture.
  [[nodiscard]] ViewportPrimitiveCreationShape presented_shape() const {
    return cached_cube_ ? cached_cube_->shape : shape_;
  }
  [[nodiscard]] ViewportSphereType presented_sphere_type() const {
    return cached_cube_ ? cached_cube_->sphere_type : sphere_type_;
  }

private:
  void update_frame();
  void update_feedback();
  BoundsResizeInteraction bounds_;
  PrimitiveCreationControlsFrame frame_;
  std::optional<ViewportPrimitiveCreationState> cached_cube_;
  std::int32_t object_id_ = {};
  ViewportPrimitiveCreationShape shape_ = ViewportPrimitiveCreationShape::Box;
  ViewportSphereType sphere_type_ = ViewportSphereType::Uv;
};

} // namespace quader
