////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/viewport_native_tool_interaction.hpp"
#include "editor/tools/extrude_handle_layout.hpp"
#include "editor/interactions/viewport_projection.hpp"

#include <optional>

namespace quader {

class ExtrudeToolService;
class ModelingSessionService;
struct ExtrudeHandleState;

/// Owns press provenance and camera projection for the persistent extrusion tool.
class ExtrudeViewportInteraction final {
public:
  [[nodiscard]] ViewportNativeToolInteractionResult handle_frame(
      ExtrudeToolService &tool, ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame,
      ModelingSessionService *modeling = nullptr);
  void clear_drag_state();
  [[nodiscard]] bool dragging() const { return press_.has_value(); }
  [[nodiscard]] bool suppressing_release() const {
    return release_suppression_.active();
  }
  [[nodiscard]] ViewportPointerCursor pointer_cursor() const { return cursor_; }
  [[nodiscard]] ExtrudeHandlePart hovered_part() const { return hovered_part_; }
  [[nodiscard]] static QVec3 handle_endpoint(
      const ViewportCameraController &camera, const ExtrudeHandleState &handle,
      float raster_scale = 1.0F);
  [[nodiscard]] static bool handle_hit(
      const ViewportCameraController &camera, const ExtrudeHandleState &handle,
      QVec2 pointer, float raster_scale = 1.0F);

private:
  /// Freezes the selected source and click intent until the drag threshold.
  struct Press final {
    ViewportPointerGesture gesture;
    QVec3 anchor;
    QVec3 normal;
    quader_editor_core::ViewportCameraState camera;
    double world_per_pixel = 0.0;
    double initial_amount = 0.0;
    double precision_scale = 1.0;
    double precision_raw_anchor = 0.0;
    double precision_amount_anchor = 0.0;
    double last_raw_delta = 0.0;
    double effective_delta = 0.0;
    std::uint64_t handle_revision = 0;
    bool adjusting = false;
    bool face_click = false;
    int click_count = 1;
    bool cycle_depth = false;
    ViewportModifiers cycle_depth_modifiers = {};
    bool started = false;
    int constraint = -1;
    bool direction_overridden = false;
    std::optional<double> published_amount;
    std::optional<QVec3> published_direction;
    std::optional<CommandResult> rejected_sample;
    std::optional<double> snap_query_amount;
    std::optional<double> snapped_amount;
    std::optional<QVec3> snap_query_direction;
    std::optional<QVec2> snap_query_pointer;
  };
  std::optional<Press> press_;
  ViewportPointerReleaseSuppression release_suppression_;
  ViewportPointerCursor cursor_ = ViewportPointerCursor::Default;
  ExtrudeHandlePart hovered_part_ = ExtrudeHandlePart::None;
};

} // namespace quader
