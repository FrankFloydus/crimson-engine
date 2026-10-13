////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/interactions/viewport_projection.hpp"
#include "editor/tools/bridge_preview_handle_layout.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"

#include <optional>
#include <functional>

namespace quader {

class ModelingSessionService;

/// Owns pointer provenance and frozen projection, leaving curve and settings with modeling.
class BridgePreviewViewportInteraction final {
public:
  [[nodiscard]] ViewportNativeToolInteractionResult handle_frame(
      ModelingSessionService &modeling, ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame,
      const std::function<CommandResult()> &apply_preview = {});
  [[nodiscard]] CommandResult cancel_drag(ModelingSessionService &modeling);
  void clear_drag_state();
  [[nodiscard]] bool dragging() const { return press_.has_value(); }
  [[nodiscard]] bool suppressing_release() const {
    return release_suppression_.active();
  }
  [[nodiscard]] ViewportPointerCursor pointer_cursor() const { return cursor_; }
  [[nodiscard]] BridgePreviewHandlePart hovered_part() const {
    return hovered_part_;
  }

private:
  /// Freezes a handle's world tangent, camera and initial accepted tension.
  struct Press final {
    ViewportPointerGesture gesture;
    BridgePreviewHandlePart part = BridgePreviewHandlePart::None;
    quader_editor_core::ViewportCameraState camera;
    QVec3 axis = {};
    QVec3 anchor = {};
    QVec3 source_anchor = {};
    QVec2 screen_axis = {};
    QVec2 screen_anchor = {};
    QVec2 screen_tip = {};
    double initial_tension = 1.0;
    double accepted_start_tension = 1.0;
    double accepted_end_tension = 1.0;
    double world_per_pixel = 0.0;
    double axis_length = 0.0;
    std::uint64_t preview_revision = 0u;
    bool screen_fallback = false;
    bool started = false;
    bool sampled_tension_accepted = false;
    std::optional<double> sampled_tension;
    std::optional<CommandResult> rejected_sample;
  };

  std::optional<Press> press_;
  ViewportPointerReleaseSuppression release_suppression_;
  ViewportPointerCursor cursor_ = ViewportPointerCursor::Default;
  BridgePreviewHandlePart hovered_part_ = BridgePreviewHandlePart::None;
};

} // namespace quader
