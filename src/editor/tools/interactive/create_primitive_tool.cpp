////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/interactive/create_primitive_tool.hpp"

#include "core/logger.hpp"

#include <cmath>

namespace quader {
namespace {

void invalidate_cube(ViewportToolContext &context) {
  if (!context.runtime_binding.current()) return;
  if (context.ports.hover) context.ports.hover->invalidate();
  if (context.ports.command_result) context.ports.command_result->mark_overlay_dirty();
}

bool accepted_cube_result(const ModelingCommandResult &result) {
  if (!result.ok) LOG_ERROR("Primitive control failed: {}", result.message);
  return result.ok;
}

ViewportToolResult consumed_cube() {
  return {.handled = true, .consumed = true, .modal_state_changed = true};
}

} // namespace

ViewportToolResult CreatePrimitiveTool::handle_event(
    ViewportToolContext &context, const ViewportToolEvent &event) const {
  if (!context.modal_state || !context.box) {
    return TwoStageBoxTool::handle_event(context, event);
  }
  ViewportModalToolState &modal = *context.modal_state;
  PrimitiveCreationInteraction &interaction = modal.primitive_creation;
  if (event.kind == ViewportToolEventKind::Cancel) {
    interaction.clear();
    return TwoStageBoxTool::handle_event(context, event);
  }
  ViewportBoxToolContext &box = *context.box;
  if (event.kind == ViewportToolEventKind::Wheel) {
    if (!box.live_primitive || !box.set_segments) return {};
    const auto &pointer = event.pointer;
    interaction.set_subdivision_decrement(pointer.modifiers.primary);
    interaction.project(box.primitive_controls_visible ? box.live_primitive() : std::nullopt,
                        context.camera, box.raster_scale);
    if (!interaction.frame().visible || !pointer.viewport_focused ||
        pointer.navigation_active || pointer.text_input_active || pointer.gizmo_consumed ||
        pointer.pointer_interaction_canceled || pointer.left_down || pointer.left_clicked ||
        pointer.left_released || interaction.active() >= 0 ||
        modal.primary_pointer_gesture || modal.primary_release_suppression.active()) {
      interaction.clear_segment_wheel();
      return {};
    }
    bool consumed = false;
    for (auto &wheel : pointer.wheel_events) {
      if (wheel.consumed) continue;
      const int control = interaction.hit_test(wheel.local);
      if (control < 6 || !std::isfinite(wheel.steps) || wheel.steps == 0.0f) {
        interaction.clear_segment_wheel();
        continue;
      }
      // Direction comes from this physical packet, independently of Ctrl's
      // click presentation. Even boundary/fractional packets reserve zoom.
      wheel.consumed = consumed = true;
      if (const auto value = interaction.wheel_segment_value(control, wheel.steps)) {
        const auto result = box.set_segments(control - 6, *value);
        if (!context.runtime_binding.current()) return consumed_cube();
        if (!accepted_cube_result(result)) interaction.clear_segment_wheel();
        if (result.changed) invalidate_cube(context);
        interaction.project(box.live_primitive(), context.camera, box.raster_scale);
      }
    }
    return consumed ? consumed_cube() : ViewportToolResult{};
  }
  if (event.kind != ViewportToolEventKind::Frame || !box.live_primitive ||
      !box.begin_resize || !box.update_resize ||
      !box.end_resize || !box.cancel_resize ||
      !box.set_segments || !box.make_event_at) {
    return TwoStageBoxTool::handle_event(context, event);
  }
  const ViewportPointerToolEvent &pointer = event.pointer;
  interaction.set_subdivision_decrement(pointer.modifiers.primary);
  if (!pointer.viewport_focused || !pointer.hovered || pointer.navigation_active ||
      pointer.text_input_active || pointer.gizmo_consumed ||
      pointer.pointer_interaction_canceled) interaction.clear_segment_wheel();
  if (modal.primary_release_suppression.consume(
          QuaderMouseButton::Left, pointer.left_clicked, pointer.left_down,
          pointer.left_released)) {
    return consumed_cube();
  }
  std::optional<ViewportPrimitiveCreationState> cube = box.live_primitive();
  interaction.project(box.primitive_controls_visible ? cube : std::nullopt,
                      context.camera, box.raster_scale);
  const auto retire = [&]() {
    modal.primary_release_suppression.begin(QuaderMouseButton::Left,
                                            pointer.left_down);
    modal.primary_pointer_gesture.reset();
    modal.primitive_creation_drag_started = false;
    interaction.end();
    interaction.hover(-1);
    invalidate_cube(context);
  };
  if (interaction.active() >= 0) {
    const int active = interaction.active();
    const bool resizing = active < 6;
    const bool stale = !cube || cube->object_id != interaction.object_id() ||
                       cube->shape != interaction.shape() ||
                       cube->sphere_type != interaction.sphere_type() ||
                       (resizing && !cube->resizing);
    const bool interrupted = pointer.pointer_interaction_canceled ||
        !pointer.viewport_focused || pointer.navigation_active ||
        pointer.text_input_active || pointer.gizmo_consumed ||
        (resizing && pointer.camera_changed) ||
        !box.primitive_controls_visible ||
        (!pointer.left_down && !pointer.left_released);
    if (stale || interrupted || !modal.primary_pointer_gesture) {
      if (resizing && (!cube || (cube->object_id == interaction.object_id() &&
                                cube->sphere_type == interaction.sphere_type()))) {
        (void)accepted_cube_result(box.cancel_resize());
        if (!context.runtime_binding.current()) return consumed_cube();
      }
      retire();
      return consumed_cube();
    }
    const ViewportPointerGesture &gesture = *modal.primary_pointer_gesture;
    const ViewportPointerButtonEdge *release =
        viewport_pointer_edge_after(pointer.pointer_released_edges, gesture.press);
    const bool matching_release = pointer.left_released &&
        (release || (gesture.press.sequence == 0 && pointer.pointer_released_edges.empty()));
    const QVec2 current = matching_release && release
                             ? release->local : pointer.mouse_local;
    modal.primitive_creation_drag_started |=
        viewport_pointer_drag_threshold_reached(gesture, current);
    if (resizing && (pointer.left_down || matching_release) &&
        modal.primitive_creation_drag_started) {
      if (auto delta = interaction.displacement(context.camera, current)) {
        const ModelingBoxToolEvent snapping = box.make_event_at(current);
        if (snapping.snap_enabled && std::isfinite(snapping.grid_size) &&
            snapping.grid_size > 0.0) {
          const double step = snapping.grid_size /
              std::max(0.000001, interaction.axis_world_scale());
          *delta = std::round(*delta / step) * step;
        }
        const ModelingCommandResult update = box.update_resize(*delta);
        const bool ok = accepted_cube_result(update);
        if (!context.runtime_binding.current()) return consumed_cube();
        if (!ok) {
          retire();
          return consumed_cube();
        }
        if (update.changed) invalidate_cube(context);
      }
    }
    if (matching_release || pointer.finalize_pressed) {
      if (resizing) {
        (void)accepted_cube_result(box.end_resize());
      } else if (matching_release &&
                 !modal.primitive_creation_drag_started &&
                 interaction.hit_test(current) == active) {
        const auto &control = interaction.frame().controls[active];
        const int value = control.segment_count + (gesture.modifiers.primary ? -1 : 1);
        if (value >= control.minimum_segments && value <= control.maximum_segments) {
          (void)accepted_cube_result(box.set_segments(active - 6, value));
        }
      }
      if (!context.runtime_binding.current()) return consumed_cube();
      retire();
      if (pointer.finalize_pressed) {
        return TwoStageBoxTool::handle_event(context, event);
      }
    }
    return consumed_cube();
  }

  const bool eligible = cube && box.primitive_controls_visible && pointer.hovered &&
      pointer.viewport_focused && !pointer.navigation_active &&
      !pointer.gizmo_consumed && !pointer.text_input_active &&
      !pointer.pointer_interaction_canceled && !modal.primary_pointer_gesture;
  interaction.hover(eligible ? interaction.hit_test(pointer.mouse_local) : -1);
  if (eligible && pointer.left_clicked && !pointer.finalize_pressed) {
    const int hit = interaction.hit_test(pointer.left_click_local);
    if (hit >= 0) {
      modal.primitive_creation_drag_started = false;
      modal.primary_pointer_gesture = viewport_pointer_gesture_from_latest_edge(
          pointer.pointer_pressed_edges, QuaderMouseButton::Left,
          pointer.left_click_local, pointer.mouse_screen, pointer.modifiers,
          pointer.pointer_drag_threshold_pixels);
      if (hit < 6) {
        const auto &control = interaction.frame().controls[hit];
        const bool ok = accepted_cube_result(box.begin_resize(
            control.resize_control, control.resize_axis));
        if (!context.runtime_binding.current()) return consumed_cube();
        if (!ok) {
          retire();
          return consumed_cube();
        }
      }
      interaction.begin(hit, *cube, context.camera,
                        modal.primary_pointer_gesture->press.local,
                        modal.primary_pointer_gesture->modifiers.primary);
      invalidate_cube(context);
      // Fast clicks may deliver both edges in one host frame. Resolve the
      // matching release through the same captured-gesture path immediately.
      if (pointer.left_released) {
        ViewportToolEvent completion = event;
        completion.pointer.left_clicked = false;
        return handle_event(context, completion);
      }
      return consumed_cube();
    }
  }
  return TwoStageBoxTool::handle_event(context, event);
}

} // namespace quader
