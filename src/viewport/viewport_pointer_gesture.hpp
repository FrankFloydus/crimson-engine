////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_input_types.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace quader {

/**
 * Stores the common, toolkit-neutral facts frozen by a pointer press.
 *
 * Interaction owners retain their own domain payload beside this value. For
 * example, Curve conversion retains a Curve point identity and material
 * transfer retains a scene/selection identity. They must not independently
 * reinterpret which release belongs to the press or when movement becomes a
 * drag.
 */
struct ViewportPointerGesture final {
  ViewportPointerButtonEdge press;
  QVec2 press_screen = {};
  ViewportModifiers modifiers = {};
  QVec2 accumulated_delta = {};
  double deadline_seconds = 0.0;
  float drag_threshold_pixels = 4.0f;
};

/** Freezes the newest exact edge or a sequence-zero synthetic fallback. */
[[nodiscard]] inline ViewportPointerGesture
viewport_pointer_gesture_from_latest_edge(
    std::span<const ViewportPointerButtonEdge> edges,
    QuaderMouseButton button, QVec2 fallback_local, QVec2 press_screen,
    ViewportModifiers modifiers, float drag_threshold_pixels = 4.0f,
    double deadline_seconds = 0.0) {
  const auto found = std::find_if(
      edges.rbegin(), edges.rend(), [button](const auto &edge) {
        return edge.button == button;
      });
  return {
      .press = found != edges.rend()
                   ? *found
                   : ViewportPointerButtonEdge{
                         .button = button,
                         .local = fallback_local,
                     },
      .press_screen = press_screen,
      .modifiers = modifiers,
      .deadline_seconds = deadline_seconds,
      .drag_threshold_pixels = drag_threshold_pixels,
  };
}

[[nodiscard]] inline double viewport_pointer_distance_squared(QVec2 left,
                                                              QVec2 right) {
  const double dx = static_cast<double>(left.x) -
                    static_cast<double>(right.x);
  const double dy = static_cast<double>(left.y) -
                    static_cast<double>(right.y);
  return dx * dx + dy * dy;
}

[[nodiscard]] inline bool viewport_pointer_drag_threshold_reached(
    const ViewportPointerGesture &gesture, QVec2 current_local) {
  const double threshold =
      static_cast<double>(std::max(gesture.drag_threshold_pixels, 0.0f));
  return viewport_pointer_distance_squared(gesture.press.local,
                                           current_local) >=
         threshold * threshold;
}

[[nodiscard]] inline bool viewport_pointer_within_drag_threshold(
    const ViewportPointerGesture &gesture, QVec2 current_local) {
  const double threshold =
      static_cast<double>(std::max(gesture.drag_threshold_pixels, 0.0f));
  return viewport_pointer_distance_squared(gesture.press.local,
                                           current_local) <=
         threshold * threshold;
}

[[nodiscard]] inline bool viewport_pointer_deadline_reached(
    const ViewportPointerGesture &gesture, double monotonic_time_seconds) {
  return gesture.deadline_seconds > 0.0 &&
         monotonic_time_seconds >= gesture.deadline_seconds;
}

/**
 * Returns the newest matching edge that happened after a frozen press.
 *
 * Sequence zero is the synthetic-test compatibility value. In that case the
 * button identity still prevents an unrelated release from completing the
 * gesture.
 */
[[nodiscard]] inline const ViewportPointerButtonEdge *
viewport_pointer_latest_edge(
    std::span<const ViewportPointerButtonEdge> edges,
    QuaderMouseButton button) {
  const auto found = std::find_if(
      edges.rbegin(), edges.rend(), [button](const auto &edge) {
        return edge.button == button;
      });
  return found == edges.rend() ? nullptr : &*found;
}

[[nodiscard]] inline const ViewportPointerButtonEdge *
viewport_pointer_edge_after(
    std::span<const ViewportPointerButtonEdge> edges,
    const ViewportPointerButtonEdge &press) {
  const auto found = std::find_if(
      edges.rbegin(), edges.rend(), [&press](const auto &edge) {
        return edge.button == press.button &&
               (press.sequence == 0u || edge.sequence == 0u ||
                edge.sequence > press.sequence);
      });
  return found == edges.rend() ? nullptr : &*found;
}

/**
 * Retains cancellation ownership until the originating button is physically
 * up. This prevents a synthetic or delayed release from becoming a new click
 * in selection, a gizmo, or another tool.
 */
class ViewportPointerReleaseSuppression final {
public:
  void begin(QuaderMouseButton button, bool button_down) {
    button_ = button_down ? button : QuaderMouseButton::None;
  }

  [[nodiscard]] bool consume(QuaderMouseButton button, bool pressed,
                             bool down, bool released) {
    if (button_ == QuaderMouseButton::None) {
      return false;
    }
    if (pressed && button == button_) {
      button_ = QuaderMouseButton::None;
      return false;
    }
    const bool same_button = button == button_;
    const bool consumed = same_button && (down || released);
    if (!down && same_button) {
      button_ = QuaderMouseButton::None;
    }
    return consumed;
  }

  void clear() { button_ = QuaderMouseButton::None; }

  [[nodiscard]] bool active() const {
    return button_ != QuaderMouseButton::None;
  }

  [[nodiscard]] QuaderMouseButton button() const { return button_; }

private:
  QuaderMouseButton button_ = QuaderMouseButton::None;
};

} // namespace quader
