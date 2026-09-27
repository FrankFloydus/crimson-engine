////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/extrude_viewport_interaction.hpp"

#include "editor/interactions/tool_drag_math.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_pixel_scale.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quader {
namespace {

bool equal_direction(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

double axis_drag_amount(
    const quader_editor_core::ViewportCameraState &camera, QVec3 anchor,
    QVec3 axis, QVec2 press, QVec2 pointer, double world_per_pixel) {
  // Freeze the projection and its near-axis fallback at press time. A moving
  // cap must never change pointer sensitivity or choose a different mapping.
  const QVec3 view = camera.projection == quader_editor_core::CameraProjection::Orthographic
                         ? camera.transform.z_axis
                         : normalize(camera.transform.origin - anchor);
  const double alignment = dot(axis, view);
  constexpr double kNearAxisCosineSquared = 0.9924038765; // Five degrees.
  const QVec3 right = camera.transform.x_axis;
  const QVec3 up = camera.transform.y_axis;
  const QVec3 forward = camera.transform.z_axis * -1.0F;
  double screen_axis_x = dot(axis, right);
  double screen_axis_y = -dot(axis, up);
  if (camera.projection == quader_editor_core::CameraProjection::Perspective) {
    const QVec3 relative_anchor = anchor - camera.transform.origin;
    const double depth = dot(relative_anchor, forward);
    if (!std::isfinite(depth) || depth <= 0.0001) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    // The perspective derivative includes depth motion for off-center faces.
    // Keep this Jacobian fixed: moving rays can cross the axis vanishing point.
    const double depth_direction = dot(axis, forward);
    screen_axis_x -= dot(relative_anchor, right) * depth_direction / depth;
    screen_axis_y += dot(relative_anchor, up) * depth_direction / depth;
  }
  const double projected_length_squared =
      screen_axis_x * screen_axis_x + screen_axis_y * screen_axis_y;
  if (!std::isfinite(projected_length_squared)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double delta_x = static_cast<double>(pointer.x) - press.x;
  const double delta_y = static_cast<double>(pointer.y) - press.y;
  if (alignment * alignment >= kNearAxisCosineSquared) {
    // Preserve a visible stem's direction without amplifying near-axis motion.
    // An exactly end-on handle has no screen direction, so upward remains positive.
    const double projected_delta = projected_length_squared > 1.0e-12
        ? (delta_x * screen_axis_x + delta_y * screen_axis_y) /
              std::sqrt(projected_length_squared)
        : -delta_y;
    return projected_delta * 2.0 * world_per_pixel;
  }
  if (projected_length_squared <= 1.0e-12) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return (delta_x * screen_axis_x + delta_y * screen_axis_y) *
         world_per_pixel / projected_length_squared;
}

} // namespace

QVec3 ExtrudeViewportInteraction::handle_endpoint(
    const ViewportCameraController &camera, const ExtrudeHandleState &handle,
    float raster_scale) {
  return build_extrude_handle_layout(camera, handle, raster_scale).button_center;
}

bool ExtrudeViewportInteraction::handle_hit(
    const ViewportCameraController &camera, const ExtrudeHandleState &handle,
    QVec2 pointer, float raster_scale) {
  return hit_extrude_handle(build_extrude_handle_layout(camera, handle, raster_scale),
                           pointer) != ExtrudeHandlePart::None;
}

ViewportNativeToolInteractionResult ExtrudeViewportInteraction::handle_frame(
    ExtrudeToolService &tool, ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame,
    ModelingSessionService *modeling) {
  ViewportNativeToolInteractionResult output;
  cursor_ = ViewportPointerCursor::Default;
  const auto &input = frame.input;
  const auto hover = [&](ExtrudeHandlePart part) {
    output.mark_overlay_dirty = output.mark_overlay_dirty || hovered_part_ != part;
    hovered_part_ = part;
  };
  const auto publish = [&](const CommandResult &result) {
    output.command_attempted = true;
    output.command_result = result;
    output.invalidate_hover = output.invalidate_hover || result.changed;
    output.mark_overlay_dirty = output.mark_overlay_dirty || result.changed;
  };
  if (release_suppression_.consume(QuaderMouseButton::Left, frame.left_pressed,
                                  frame.left_down, frame.left_released)) {
    hover(ExtrudeHandlePart::None);
    output.consumed = output.blocks_transform_gizmo = true;
    return output;
  }
  if (!tool.active()) {
    hover(ExtrudeHandlePart::None);
    clear_drag_state();
    return output;
  }
  const bool interrupted =
      input.pointer_interaction_canceled || !input.viewport_focused ||
      input.text_input_active || input.navigation_active ||
      frame.input_result.navigation_active || frame.input_result.camera_changed ||
      frame.input_result.view_changed;
  if (frame.input_result.cancel_requested) {
    hover(ExtrudeHandlePart::None);
    const bool had_press = press_.has_value() || tool.dragging();
    const bool cancel_tool = !had_press ||
        (tool.preview_active() && !tool.dragging());
    publish(cancel_tool ? tool.cancel() : tool.cancel_drag());
    if (output.command_result.ok) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    }
    output.consumed = output.blocks_transform_gizmo = true;
    output.sync_active_tool = cancel_tool && output.command_result.ok;
    return output;
  }
  if (press_ && interrupted) {
    hover(ExtrudeHandlePart::None);
    output.consumed = output.blocks_transform_gizmo = true;
    const bool focus_only = !input.viewport_focused &&
        !input.pointer_interaction_canceled && !input.text_input_active &&
        !input.navigation_active && !frame.input_result.navigation_active &&
        !frame.input_result.camera_changed && !frame.input_result.view_changed;
    if (focus_only && frame.left_down && !frame.left_released) {
      return output;
    }
    // Ending capture/navigation finishes the pointer gesture at its last valid
    // authored amount. The pending extrusion remains available for adjustment.
    publish(tool.release_drag());
    if (output.command_result.ok) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    }
    return output;
  }
  if (interrupted || frame.transform_gizmo_consumed) {
    hover(ExtrudeHandlePart::None);
    return output;
  }
  if (input.modal_tool_finalize_pressed && tool.preview_active() &&
      !tool.dragging()) {
    const CommandResult applied = tool.commit_preview();
    publish(applied);
    if (applied.ok) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    }
    output.consumed = output.blocks_transform_gizmo = true;
    output.sync_active_tool = applied.ok;
    return output;
  }
  const ExtrudeHandleState handle = tool.handle_state();
  const auto layout = !frame.suppress_hover_work && frame.handles_visible &&
                              input.viewport_hovered
                          ? build_extrude_handle_layout(camera, handle,
                                                        frame.viewport_pixel_scale)
                          : ExtrudeHandleLayout{};
  const auto hit = hit_extrude_handle(
      layout, frame.left_pressed ? frame.left_click_local : input.mouse_local);
  hover(press_ ? ExtrudeHandlePart::NewExtrusion : hit);
  if (hit != ExtrudeHandlePart::None) {
    cursor_ = ViewportPointerCursor::OpenHand;
  }
  if (!press_ && frame.left_pressed && input.viewport_hovered && handle.active) {
    const auto gesture = viewport_pointer_gesture_from_latest_edge(
        input.pointer_pressed_edges, QuaderMouseButton::Left,
        frame.left_click_local, input.mouse_screen, input.modifiers,
        frame.pointer_drag_threshold_pixels);
    // An explicit disk hit owns precision and snapping modifiers from the
    // beginning of the gesture. Pending sources never release clicks to selection.
    if (!gesture.modifiers.alt &&
        (hit != ExtrudeHandlePart::None ||
         (!gesture.modifiers.primary && !gesture.modifiers.shift))) {
      const ViewportRay ray = camera.ray_from_viewport_point(gesture.press.local);
      const bool selected_face = tool.selected_face_hit(
          {.ray = {.origin = ray.origin, .direction = ray.direction},
           .select_backfaces = frame.select_backfaces});
      if (hit != ExtrudeHandlePart::None || selected_face) {
        const bool adjusting = handle.adjustment_available;
        const QVec3 anchor = adjusting ? handle.adjustment_origin
                                       : handle.world_position;
        const QVec3 source_direction = adjusting ? handle.adjustment_direction
                                                 : handle.world_direction;
        const auto frozen_camera =
            ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size());
        press_ = Press{.gesture = gesture,
                       .anchor = anchor,
                       .normal = length(source_direction) > 1.0e-6F
                                     ? source_direction : handle.world_direction,
                       .camera = frozen_camera,
                       .world_per_pixel = quader_editor_core::viewport_world_units_per_pixel(
                           frozen_camera, anchor),
                       .initial_amount = adjusting ? handle.amount_world : 0.0,
                       .handle_revision = handle.revision,
                       .adjusting = adjusting,
                       .face_click = selected_face && hit == ExtrudeHandlePart::None,
                       .click_count = frame.left_click_count,
                       .cycle_depth = input.cycle_selection_depth_pressed,
                       .cycle_depth_modifiers = input.cycle_selection_depth_modifiers};
        output.claim_interaction_pane = true;
      }
    }
  }
  if (!press_) {
    if (tool.preview_active()) {
      output.consumed = output.blocks_transform_gizmo = true;
    }
    return output;
  }
  output.consumed = output.blocks_transform_gizmo = true;
  const ViewportPointerButtonEdge *release =
      frame.left_released
          ? viewport_pointer_edge_after(input.pointer_released_edges,
                                         press_->gesture.press)
          : nullptr;
  const bool released = release != nullptr ||
                        (frame.left_released && press_->gesture.press.sequence == 0u &&
                         input.pointer_released_edges.empty());
  if ((!frame.left_down && !frame.left_released) ||
      (press_->started && !tool.dragging()) ||
      (!press_->started && handle.revision != press_->handle_revision)) {
    publish(tool.release_drag());
    if (output.command_result.ok) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    }
    return output;
  }
  const QVec2 pointer = release != nullptr ? release->local : input.mouse_local;
  const ViewportModifiers sample_modifiers =
      release != nullptr ? release->modifiers.value_or(input.modifiers)
                         : input.modifiers;
  if (tool.mode() == modeling::FaceExtrudeMode::Region) {
    const int axis = input.tool_constraint_x_pressed ? 0
                     : input.tool_constraint_y_pressed ? 1
                     : input.tool_constraint_z_pressed ? 2 : -1;
    if (axis >= 0) {
      press_->constraint = press_->constraint == axis ? -1 : axis;
      press_->direction_overridden = true;
    }
  }
  QVec3 direction = press_->normal;
  if (press_->constraint >= 0) {
    direction = press_->constraint == 0 ? QVec3{1.0F, 0.0F, 0.0F}
                 : press_->constraint == 1 ? QVec3{0.0F, 1.0F, 0.0F}
                                           : QVec3{0.0F, 0.0F, 1.0F};
  }
  // Keep the original operation's implicit direction until an axis key changes
  // it; a plain adjustment returning to its starting amount is then a no-op.
  const std::optional<QVec3> region_direction =
      tool.mode() == modeling::FaceExtrudeMode::Region &&
              (!press_->adjusting || press_->direction_overridden)
          ? std::optional<QVec3>{direction} : std::nullopt;
  const bool finite_pointer = std::isfinite(pointer.x) && std::isfinite(pointer.y);
  // The disk has explicit drag intent, so its first motion needs no selection
  // threshold. A stationary click must not apply snapping or begin a preview.
  const bool drag_requested = press_->face_click
      ? viewport_pointer_drag_threshold_reached(press_->gesture, pointer)
      : finite_pointer &&
            viewport_pointer_distance_squared(press_->gesture.press.local, pointer) > 0.0;
  if (!press_->started && drag_requested) {
    const auto begun = press_->adjusting
        ? tool.begin_adjust_drag(region_direction) : tool.begin_drag(region_direction);
    publish(begun);
    if (!begun.ok) {
      clear_drag_state();
      release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
      return output;
    }
    press_->started = true;
  }
  if (press_->started) {
    const double raw_delta = axis_drag_amount(
        press_->camera, press_->anchor, direction,
        press_->gesture.press.local, pointer, press_->world_per_pixel);
    if (finite_pointer && std::isfinite(raw_delta)) {
      // Anchor precision only when its modifier changes. This scales new motion
      // without jumps and still recovers exactly after an invalid large sample.
      const double precision = sample_modifiers.shift ? 0.1 : 1.0;
      if (precision != press_->precision_scale) {
        press_->precision_raw_anchor = press_->last_raw_delta;
        press_->precision_amount_anchor = press_->effective_delta;
        press_->precision_scale = precision;
      }
      press_->effective_delta = press_->precision_amount_anchor +
          precision * (raw_delta - press_->precision_raw_anchor);
      press_->last_raw_delta = raw_delta;
    }
    double amount = finite_pointer && std::isfinite(raw_delta)
        ? press_->initial_amount + press_->effective_delta
        : std::numeric_limits<double>::quiet_NaN();
    const bool vertex_snap_down = release != nullptr && release->held_input
        ? release->held_input->transform_vertex_snap_down
        : input.transform_vertex_snap_down;
    const bool scene_snap = std::isfinite(amount) && modeling != nullptr &&
                            vertex_snap_down;
    if (scene_snap) {
      if (!press_->snap_query_amount || *press_->snap_query_amount != amount ||
          !press_->snap_query_direction ||
          !equal_direction(*press_->snap_query_direction, direction) ||
          !press_->snap_query_pointer ||
          press_->snap_query_pointer->x != pointer.x ||
          press_->snap_query_pointer->y != pointer.y) {
        press_->snap_query_amount = amount;
        press_->snap_query_direction = direction;
        press_->snap_query_pointer = pointer;
        press_->snapped_amount.reset();
        const auto ray = quader_editor_core::ray_from_viewport_screen_point(
            press_->camera, pointer);
        const double radius = frame.scene_vertex_snap_radius > 0.0
            ? frame.scene_vertex_snap_radius
            : 14.0 * viewport_sanitized_pixel_scale(frame.viewport_pixel_scale) *
                  press_->world_per_pixel;
        const auto target = modeling->pick_mesh_vertex_snap_target(
            {.origin = ray.origin, .direction = ray.direction}, radius, true);
        if (target) {
          const double initial_projection = tool.mode() == modeling::FaceExtrudeMode::Region
              ? press_->initial_amount * dot(press_->normal, direction)
              : press_->initial_amount;
          press_->snapped_amount = initial_projection +
              dot(target->world_position - press_->anchor, direction);
        }
      }
      amount = press_->snapped_amount.value_or(amount);
    } else {
      press_->snap_query_amount.reset();
      press_->snapped_amount.reset();
      press_->snap_query_direction.reset();
      press_->snap_query_pointer.reset();
    }
    // The release edge owns Ctrl just as it owns the final pointer position.
    // Snap the total amount, including the pending amount before this drag.
    if (sample_modifiers.primary && !press_->snapped_amount) {
      const double grid = viewport_grid_world_size_for_preset(input.grid_preset);
      if (grid > 0.0) {
        amount = std::round(amount / grid) * grid;
      }
    }
    if (!press_->published_amount || *press_->published_amount != amount ||
        !press_->published_direction ||
        !equal_direction(*press_->published_direction, direction)) {
      const auto updated = tool.update_drag_world_amount(amount, region_direction);
      press_->published_amount = amount;
      press_->published_direction = direction;
      if (updated.ok) {
        press_->rejected_sample.reset();
        publish(updated);
      } else if (tool.can_update_drag()) {
        // Invalid intermediate caps leave the last valid preview intact. Cache
        // the rejected sample and allow reversal without repeated regeneration.
        press_->rejected_sample = updated;
      } else {
        publish(updated);
        return output;
      }
    }
    cursor_ = ViewportPointerCursor::ClosedHand;
  }
  if (released) {
    if (press_->started) {
      const CommandResult released = tool.release_drag();
      publish(press_->rejected_sample && released.ok
                  ? *press_->rejected_sample : released);
      if (!released.ok) {
        return output;
      }
    } else if (press_->face_click && !tool.preview_active()) {
      output.selection_click_requested = true;
      output.selection_click_local = press_->gesture.press.local;
      output.selection_click_modifiers = press_->gesture.modifiers;
      output.selection_click_count = press_->click_count;
      output.selection_cycle_depth = press_->cycle_depth;
      output.selection_cycle_depth_modifiers = press_->cycle_depth_modifiers;
      output.blocks_transform_gizmo = false;
    }
    press_.reset();
  }
  return output;
}

void ExtrudeViewportInteraction::clear_drag_state() {
  if (press_) {
    release_suppression_.begin(QuaderMouseButton::Left, true);
  }
  press_.reset();
  cursor_ = ViewportPointerCursor::Default;
  hovered_part_ = ExtrudeHandlePart::None;
}

} // namespace quader
