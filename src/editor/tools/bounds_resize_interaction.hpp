////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/bounds_resize_controls.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <optional>

namespace quader {

/// Retains pane-local draw/pick geometry and immutable press-time drag math.
class BoundsResizeInteraction final {
public:
  const BoundsResizeFrame &project(
      const std::optional<ViewportBoundsResizeState> &bounds,
      const ViewportCameraController &camera, float raster_scale);
  const BoundsResizeFrame &project_presentation(
      const std::optional<ViewportBoundsResizeState> &bounds,
      const ViewportCameraController &camera, float raster_scale,
      const BoundsResizeFrame &interaction_frame);
  [[nodiscard]] int hit_test(QVec2 point) const;
  void hover(int handle);
  void begin(int handle, const ViewportBoundsResizeState &bounds,
             const ViewportCameraController &camera, QVec2 press);
  /// Positive displacement moves the captured side outwards, in world units.
  [[nodiscard]] std::optional<double>
  displacement(const ViewportCameraController &camera, QVec2 point) const;
  void end();
  void clear();
  [[nodiscard]] int active() const { return frame_.active; }
  [[nodiscard]] const BoundsResizeFrame &frame() const { return frame_; }
  [[nodiscard]] std::uint64_t projection_count() const { return projection_count_; }
  [[nodiscard]] double axis_world_scale() const { return drag_axis_scale_; }

private:
  BoundsResizeFrame frame_;
  std::optional<ViewportBoundsResizeState> cached_bounds_;
  ViewportCameraState cached_camera_;
  ViewportCameraPose cached_pose_;
  QVec2 cached_viewport_;
  bool cache_valid_ = false;
  std::uint64_t projection_count_ = 0;
  QVec3 drag_anchor_;
  QVec3 drag_axis_;
  QVec2 drag_press_;
  QVec2 drag_screen_anchor_;
  QVec2 drag_screen_tip_;
  QVec2 drag_fallback_axis_;
  std::optional<double> drag_axis_start_;
  double drag_axis_scale_ = 1.0;
  double drag_world_per_pixel_ = 1.0;
  double drag_reference_world_ = 1.0;
  bool drag_screen_fallback_ = false;
};

} // namespace quader
