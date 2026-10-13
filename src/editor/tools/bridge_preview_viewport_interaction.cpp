////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/bridge_preview_viewport_interaction.hpp"

#include "editor/interactions/tool_drag_math.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

bool equal_position(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool equal_tension(double left, double right) {
  return left == right;
}

CommandResult native_result(const ModelingCommandResult &result) {
  return {.ok = result.ok, .changed = result.changed, .message = result.message};
}

/// Projects the tangent derivative at the pressed handle, avoiding a moving vanishing point.
QVec2 projected_tangent(const quader_editor_core::ViewportCameraState &camera,
                        QVec3 anchor, QVec3 tangent) {
  const QVec3 forward = camera.transform.z_axis * -1.0F;
  double horizontal = dot(tangent, camera.transform.x_axis);
  double vertical = -dot(tangent, camera.transform.y_axis);
  if (camera.projection == quader_editor_core::CameraProjection::Perspective) {
    const QVec3 relative = anchor - camera.transform.origin;
    const double depth = dot(relative, forward);
    if (depth <= 1.0e-8 || !std::isfinite(depth)) {
      return {};
    }
    const double depth_delta = dot(tangent, forward);
    horizontal -= dot(relative, camera.transform.x_axis) * depth_delta / depth;
    vertical += dot(relative, camera.transform.y_axis) * depth_delta / depth;
  }
  return {static_cast<float>(horizontal), static_cast<float>(vertical)};
}

} // namespace

ViewportNativeToolInteractionResult BridgePreviewViewportInteraction::handle_frame(
    ModelingSessionService &modeling, ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame,
    const std::function<CommandResult()> &apply_preview) {
  ViewportNativeToolInteractionResult output;
  cursor_ = ViewportPointerCursor::Default;
  const auto hover = [&](BridgePreviewHandlePart part) {
    output.mark_overlay_dirty = output.mark_overlay_dirty || part != hovered_part_;
    hovered_part_ = part;
  };
  const auto publish = [&](const CommandResult &result) {
    output.command_attempted = true;
    output.command_result = result;
    output.invalidate_hover = output.invalidate_hover || result.changed;
    output.mark_overlay_dirty = output.mark_overlay_dirty || result.changed;
  };
  const bool had_suppression = release_suppression_.active();
  if (release_suppression_.consume(QuaderMouseButton::Left, frame.left_pressed,
                                   frame.left_down, frame.left_released) ||
      (had_suppression && !frame.left_pressed)) {
    hover(BridgePreviewHandlePart::None);
    output.consumed = output.blocks_transform_gizmo = true;
    return output;
  }
  const auto handle = modeling.bridge_preview_handle_state();
  if (!handle) {
    hover(BridgePreviewHandlePart::None);
    clear_drag_state();
    return output;
  }
  const auto &input = frame.input;
  if (frame.input_result.cancel_requested) {
    const bool had_press = press_.has_value();
    publish(had_press ? cancel_drag(modeling)
                      : native_result(modeling.cancel_active_operation_preview()));
    if (output.command_result.ok) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    }
    hover(BridgePreviewHandlePart::None);
    output.consumed = output.blocks_transform_gizmo = true;
    output.sync_active_tool = !had_press && output.command_result.ok;
    return output;
  }
  const bool interrupted = input.pointer_interaction_canceled ||
      !input.viewport_focused || input.text_input_active ||
      input.navigation_active || frame.input_result.navigation_active ||
      frame.input_result.camera_changed || frame.input_result.view_changed;
  if (press_ && interrupted) {
    output.consumed = output.blocks_transform_gizmo = true;
    hover(BridgePreviewHandlePart::None);
    const bool focus_only = !input.viewport_focused &&
        !input.pointer_interaction_canceled && !input.text_input_active &&
        !input.navigation_active && !frame.input_result.navigation_active &&
        !frame.input_result.camera_changed && !frame.input_result.view_changed;
    if (!focus_only || !frame.left_down || frame.left_released) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    }
    return output;
  }
  if (interrupted || frame.transform_gizmo_consumed) {
    hover(BridgePreviewHandlePart::None);
    return output;
  }
  if (input.modal_tool_finalize_pressed) {
    publish(apply_preview ? apply_preview()
                          : command_invalid_result("Bridge preview history is unavailable."));
    if (output.command_result.ok) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    }
    hover(BridgePreviewHandlePart::None);
    output.consumed = output.blocks_transform_gizmo = true;
    output.sync_active_tool = output.command_result.ok;
    return output;
  }
  const auto layout = frame.handles_visible && !frame.suppress_hover_work &&
                              input.viewport_hovered
                          ? build_bridge_preview_handle_layout(
                                camera, *handle, frame.viewport_pixel_scale)
                          : BridgePreviewHandleLayout{};
  const auto hit = hit_bridge_preview_handle(
      layout, frame.left_pressed ? frame.left_click_local : input.mouse_local);
  hover(press_ ? press_->part : hit);
  if (hit != BridgePreviewHandlePart::None) {
    cursor_ = ViewportPointerCursor::OpenHand;
  }
  if (!press_ && frame.left_pressed && hit != BridgePreviewHandlePart::None) {
    const auto gesture = viewport_pointer_gesture_from_latest_edge(
        input.pointer_pressed_edges, QuaderMouseButton::Left,
        frame.left_click_local, input.mouse_screen, input.modifiers,
        frame.pointer_drag_threshold_pixels);
    if (!gesture.modifiers.alt) {
      const bool start = hit == BridgePreviewHandlePart::Start;
      const QVec3 anchor = handle->control_points[start ? 1u : 2u];
      const QVec3 axis = start ? handle->start_handle_axis : handle->end_handle_axis;
      const auto frozen_camera =
          ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size());
      const double world_per_pixel =
          quader_editor_core::viewport_world_units_per_pixel(frozen_camera, anchor);
      const double axis_length = length(axis);
      const QVec2 screen_derivative = projected_tangent(frozen_camera, anchor, axis);
      const double projected_length = std::hypot(screen_derivative.x,
                                                 screen_derivative.y);
      if (std::isfinite(world_per_pixel) && world_per_pixel > 0.0 &&
          std::isfinite(axis_length) && axis_length > 1.0e-8) {
        const auto settings = modeling.interaction_settings();
        // A tangent viewed end-on has no stable screen direction. Preserve a
        // bounded upward fallback rather than amplifying its tiny projection.
        constexpr double kNearAxisSine = 0.08715574275; // Five degrees.
        const bool fallback = !std::isfinite(projected_length) ||
                              projected_length <= axis_length * kNearAxisSine;
        press_ = Press{
            .gesture = gesture,
            .part = hit,
            .camera = frozen_camera,
            .axis = axis,
            .anchor = anchor,
            .source_anchor = handle->control_points[start ? 0u : 3u],
            .screen_axis = fallback ? QVec2{0.0F, -1.0F} : screen_derivative,
            .screen_anchor = gesture.press.local,
            .screen_tip = gesture.press.local +
                (fallback ? QVec2{0.0F, -1.0F}
                          : screen_derivative / static_cast<float>(world_per_pixel)),
            .initial_tension = start ? settings.bridge_start_tension
                                     : settings.bridge_end_tension,
            .accepted_start_tension = handle->start_tension,
            .accepted_end_tension = handle->end_tension,
            .world_per_pixel = world_per_pixel,
            .axis_length = axis_length,
            .preview_revision = handle->generation,
            .screen_fallback = fallback,
        };
        output.claim_interaction_pane = true;
      }
    }
  }
  if (!press_) {
    output.blocks_transform_gizmo = true;
    return output;
  }
  output.consumed = output.blocks_transform_gizmo = true;
  const bool start = press_->part == BridgePreviewHandlePart::Start;
  const bool stale = handle->generation != press_->preview_revision ||
      !equal_tension(handle->start_tension, press_->accepted_start_tension) ||
      !equal_tension(handle->end_tension, press_->accepted_end_tension) ||
      !equal_position(start ? handle->start_handle_axis : handle->end_handle_axis,
                      press_->axis) ||
      !equal_position(handle->control_points[start ? 0u : 3u], press_->source_anchor);
  if (stale || (!frame.left_down && !frame.left_released)) {
    clear_drag_state();
    release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    hover(BridgePreviewHandlePart::None);
    return output;
  }
  const ViewportPointerButtonEdge *release = frame.left_released
      ? viewport_pointer_matching_release(input.pointer_released_edges,
                                           input.pointer_pressed_edges,
                                           press_->gesture.press)
      : nullptr;
  const bool released = release != nullptr ||
      (frame.left_released && press_->gesture.press.sequence == 0u &&
       input.pointer_released_edges.empty());
  // A stale/unrelated release must not contribute its final coordinates.
  if (frame.left_released && !released && !frame.left_down) {
    return output;
  }
  const QVec2 pointer = release != nullptr ? release->local : input.mouse_local;
  const bool finite_pointer = std::isfinite(pointer.x) && std::isfinite(pointer.y);
  const double moved = viewport_pointer_distance_squared(
      press_->gesture.press.local, pointer);
  if (finite_pointer && moved > 0.0) {
    press_->started = true;
  }
  if (press_->started) {
    const auto delta = quader_editor_core::projected_axis_drag_distance({
        .screen_position = pointer,
        .press_position = press_->gesture.press.local,
        .screen_axis = press_->screen_axis,
        .uses_screen_handle = true,
        .screen_anchor = press_->screen_anchor,
        .screen_tip = press_->screen_tip,
        .reference_distance = press_->screen_fallback
            ? 2.0 * press_->world_per_pixel / press_->axis_length : 1.0,
    });
    if (finite_pointer && delta.ok) {
      const double tension = std::clamp(press_->initial_tension + delta.distance,
                                        0.0, 3.0);
      if (!press_->sampled_tension || *press_->sampled_tension != tension) {
        auto settings = modeling.interaction_settings();
        (start ? settings.bridge_start_tension : settings.bridge_end_tension) = tension;
        const CommandResult updated = native_result(modeling.set_interaction_settings(settings));
        press_->sampled_tension = tension;
        press_->sampled_tension_accepted = updated.ok;
        if (updated.ok) {
          press_->rejected_sample.reset();
          (start ? press_->accepted_start_tension : press_->accepted_end_tension) = tension;
          publish(updated);
        } else {
          press_->rejected_sample = updated;
        }
      } else if (press_->sampled_tension_accepted) {
        // Returning from an invalid pointer sample to the exact accepted value
        // clears its diagnostic without rebuilding the unchanged geometry.
        press_->rejected_sample.reset();
      }
    } else {
      press_->rejected_sample = command_invalid_result(
          "The bridge handle pointer sample is not finite.");
    }
    cursor_ = ViewportPointerCursor::ClosedHand;
  }
  if (released) {
    if (press_->rejected_sample) {
      publish(*press_->rejected_sample);
    }
    press_.reset();
    output.mark_overlay_dirty = true;
  }
  return output;
}

CommandResult BridgePreviewViewportInteraction::cancel_drag(
    ModelingSessionService &modeling) {
  if (!press_) {
    return command_unchanged_result();
  }
  const auto handle = modeling.bridge_preview_handle_state();
  if (!handle || handle->generation != press_->preview_revision ||
      !equal_tension(handle->start_tension, press_->accepted_start_tension) ||
      !equal_tension(handle->end_tension, press_->accepted_end_tension)) {
    clear_drag_state();
    return command_unchanged_result();
  }
  auto settings = modeling.interaction_settings();
  (press_->part == BridgePreviewHandlePart::Start
       ? settings.bridge_start_tension : settings.bridge_end_tension) =
      press_->initial_tension;
  const auto result = native_result(modeling.set_interaction_settings(settings));
  if (result.ok) {
    clear_drag_state();
  }
  return result;
}

void BridgePreviewViewportInteraction::clear_drag_state() {
  if (press_) {
    release_suppression_.begin(QuaderMouseButton::Left, true);
  }
  press_.reset();
  cursor_ = ViewportPointerCursor::Default;
  hovered_part_ = BridgePreviewHandlePart::None;
}

} // namespace quader
