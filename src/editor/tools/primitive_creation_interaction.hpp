////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/primitive_creation_controls.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <optional>

namespace quader {

/** Presentation cache and immutable drag math, never a second mesh owner. */
class PrimitiveCreationInteraction final {
public:
  const PrimitiveCreationControlsFrame &project(
      const std::optional<ViewportPrimitiveCreationState> &cube,
      const ViewportCameraController &camera, float raster_scale);
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
  [[nodiscard]] std::int32_t object_id() const { return object_id_; }
  [[nodiscard]] const PrimitiveCreationControlsFrame &frame() const { return frame_; }
  [[nodiscard]] std::uint64_t projection_count() const { return projection_count_; }
  [[nodiscard]] double axis_world_scale() const { return drag_axis_scale_; }
  [[nodiscard]] ViewportPrimitiveCreationShape shape() const { return shape_; }

private:
  void update_subdivision_controls();
  void update_feedback();
  PrimitiveCreationControlsFrame frame_;
  bool subdivision_decrement_ = false;
  bool active_subdivision_decrement_ = false;
  std::optional<ViewportPrimitiveCreationState> cached_cube_;
  ViewportCameraState cached_camera_;
  ViewportCameraPose cached_pose_;
  QVec2 cached_viewport_;
  float cached_scale_ = 0.0f;
  bool cache_valid_ = false;
  std::uint64_t projection_count_ = 0;
  std::int32_t object_id_ = {};
  ViewportPrimitiveCreationShape shape_ = ViewportPrimitiveCreationShape::Box;
  QVec3 drag_anchor_;
  QVec3 drag_axis_;
  QVec2 drag_press_;
  QVec2 drag_screen_tip_;
  QVec2 drag_screen_anchor_;
  std::optional<double> drag_axis_start_;
  double drag_axis_scale_ = 1.0;
  double drag_world_per_pixel_ = 1.0;
  double drag_reference_world_ = 1.0;
  bool drag_screen_fallback_ = false;
  int wheel_control_ = -1;
  double wheel_remainder_ = 0.0;
};

} // namespace quader
