////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_input_router.hpp"

#include "diagnostics/profile.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace quader {

ViewportNavigationPress
viewport_navigation_press(const ViewportNavigationInput &navigation) {
  if (!navigation.pointer_transitions.empty()) {
    ViewportNavigationInput open_navigation;
    ViewportNavigationInput all_navigation;
    const auto set_press = [](ViewportNavigationInput &target,
                              ViewportInputActionKind action,
                              const ViewportPointerButtonEdge &edge,
                              bool pressed) {
      switch (action) {
      case ViewportInputActionKind::Fly:
        target.fly_pressed = pressed;
        target.fly_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      case ViewportInputActionKind::Pan:
        target.pan_pressed = pressed;
        target.pan_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      case ViewportInputActionKind::OrbitSnap:
        target.orbit_snap_pressed = pressed;
        target.orbit_snap_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      case ViewportInputActionKind::Orbit:
        target.orbit_pressed = pressed;
        target.orbit_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      default:
        break;
      }
    };
    for (const ViewportNavigationPointerTransition &transition :
         navigation.pointer_transitions) {
      const bool pressed =
          transition.kind ==
          ViewportNavigationPointerTransitionKind::Pressed;
      set_press(open_navigation, transition.action,
                transition.pointer_edge, pressed);
      if (pressed) {
        set_press(all_navigation, transition.action,
                  transition.pointer_edge, true);
      }
    }
    ViewportNavigationPress open =
        viewport_navigation_press(open_navigation);
    if (open.action != ViewportInputActionKind::None) {
      return open;
    }
    return viewport_navigation_press(all_navigation);
  }

  const auto select = [&](bool require_open_press) {
    ViewportNavigationPress selected;
    const auto consider = [&](bool pressed, ViewportInputActionKind action,
                              std::optional<ViewportPointerButtonEdge> edge,
                              bool released,
                              std::optional<ViewportPointerButtonEdge>
                                  release_edge) {
      const std::uint64_t candidate_sequence =
          edge.has_value() ? edge->sequence : 0u;
      const bool ordered_repress =
          released && edge.has_value() && release_edge.has_value() &&
          edge->sequence != 0u && release_edge->sequence != 0u &&
          release_edge->sequence < edge->sequence;
      const bool open_press = !released || ordered_repress;
      if (!pressed || (require_open_press && !open_press)) {
        return;
      }
      if (selected.action == ViewportInputActionKind::None) {
        selected = {.action = action, .pointer_edge = edge};
        return;
      }

      const std::uint64_t selected_sequence =
          selected.pointer_edge.has_value()
              ? selected.pointer_edge->sequence
              : 0u;
      // Physical order wins whenever it is known. Iteration order preserves
      // the established Fly > Pan > OrbitSnap > Orbit priority for equal or
      // synthetic (zero-provenance) presses.
      if (candidate_sequence != 0u &&
          (selected_sequence == 0u ||
           candidate_sequence < selected_sequence)) {
        selected = {.action = action, .pointer_edge = edge};
      }
    };

    consider(navigation.fly_pressed, ViewportInputActionKind::Fly,
             navigation.fly_pressed_pointer_edge, navigation.fly_released,
             navigation.fly_released_pointer_edge);
    consider(navigation.pan_pressed, ViewportInputActionKind::Pan,
             navigation.pan_pressed_pointer_edge, navigation.pan_released,
             navigation.pan_released_pointer_edge);
    consider(navigation.orbit_snap_pressed,
             ViewportInputActionKind::OrbitSnap,
             navigation.orbit_snap_pressed_pointer_edge,
             navigation.orbit_snap_released,
             navigation.orbit_snap_released_pointer_edge);
    consider(navigation.orbit_pressed, ViewportInputActionKind::Orbit,
             navigation.orbit_pressed_pointer_edge,
             navigation.orbit_released,
             navigation.orbit_released_pointer_edge);
    return selected;
  };

  // A complete gesture and another still-held press can be retained in one
  // frame. Prefer the earliest open gesture, while comparing each press only
  // to its own action release so an unrelated later click cannot steal first
  // ownership. If every press completed, retain the earliest one so a quick
  // press-drag-release can still contribute its bounded motion.
  ViewportNavigationPress open = select(true);
  if (open.action != ViewportInputActionKind::None) {
    return open;
  }
  return select(false);
}

namespace {

bool navigation_action_down(const ViewportInputFrame &input,
                            ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    return input.navigation_bindings.orbit_down;
  case ViewportInputActionKind::OrbitSnap:
    return input.navigation_bindings.orbit_snap_down;
  case ViewportInputActionKind::Pan:
    return input.navigation_bindings.pan_down;
  case ViewportInputActionKind::Fly:
    return input.navigation_bindings.fly_down;
  default:
    return false;
  }
}

bool navigation_action_released(const ViewportInputFrame &input,
                                ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    return input.navigation_bindings.orbit_released;
  case ViewportInputActionKind::OrbitSnap:
    return input.navigation_bindings.orbit_snap_released;
  case ViewportInputActionKind::Pan:
    return input.navigation_bindings.pan_released;
  case ViewportInputActionKind::Fly:
    return input.navigation_bindings.fly_released;
  default:
    return false;
  }
}

std::optional<ViewportPointerButtonEdge>
navigation_action_release_pointer_edge(
    const ViewportNavigationInput &navigation,
    ViewportInputActionKind action,
    std::uint64_t after_sequence = 0u) {
  if (!navigation.pointer_transitions.empty()) {
    for (const ViewportNavigationPointerTransition &transition :
         navigation.pointer_transitions) {
      if (transition.kind ==
              ViewportNavigationPointerTransitionKind::Released &&
          transition.action == action &&
          (after_sequence == 0u ||
           (transition.pointer_edge.sequence != 0u &&
            transition.pointer_edge.sequence > after_sequence))) {
        return transition.pointer_edge;
      }
    }
    return std::nullopt;
  }
  switch (action) {
  case ViewportInputActionKind::Orbit:
    return navigation.orbit_released_pointer_edge;
  case ViewportInputActionKind::OrbitSnap:
    return navigation.orbit_snap_released_pointer_edge;
  case ViewportInputActionKind::Pan:
    return navigation.pan_released_pointer_edge;
  case ViewportInputActionKind::Fly:
    return navigation.fly_released_pointer_edge;
  default:
    return std::nullopt;
  }
}

bool pointer_edge_precedes(
    const std::optional<ViewportPointerButtonEdge> &earlier,
    const std::optional<ViewportPointerButtonEdge> &later) {
  return earlier.has_value() && later.has_value() &&
         earlier->sequence != 0u && later->sequence != 0u &&
         earlier->sequence < later->sequence;
}

bool same_navigation_press(const ViewportNavigationPress &lhs,
                           const ViewportNavigationPress &rhs) {
  if (lhs.action != rhs.action || !lhs.pointer_edge.has_value() ||
      !rhs.pointer_edge.has_value()) {
    return false;
  }
  return lhs.pointer_edge->sequence != 0u &&
         lhs.pointer_edge->sequence == rhs.pointer_edge->sequence;
}

ViewportNavigationPress navigation_press_after_release(
    const ViewportNavigationInput &navigation,
    const std::optional<ViewportPointerButtonEdge> &release_edge) {
  if (!release_edge.has_value() || release_edge->sequence == 0u) {
    return {};
  }

  if (!navigation.pointer_transitions.empty()) {
    ViewportNavigationInput open_navigation;
    const auto set_press = [](ViewportNavigationInput &target,
                              ViewportInputActionKind action,
                              const ViewportPointerButtonEdge &edge,
                              bool pressed) {
      switch (action) {
      case ViewportInputActionKind::Fly:
        target.fly_pressed = pressed;
        target.fly_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      case ViewportInputActionKind::Pan:
        target.pan_pressed = pressed;
        target.pan_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      case ViewportInputActionKind::OrbitSnap:
        target.orbit_snap_pressed = pressed;
        target.orbit_snap_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      case ViewportInputActionKind::Orbit:
        target.orbit_pressed = pressed;
        target.orbit_pressed_pointer_edge =
            pressed ? std::optional{edge} : std::nullopt;
        break;
      default:
        break;
      }
    };
    for (const ViewportNavigationPointerTransition &transition :
         navigation.pointer_transitions) {
      if (transition.pointer_edge.sequence <= release_edge->sequence) {
        continue;
      }
      set_press(
          open_navigation, transition.action, transition.pointer_edge,
          transition.kind ==
              ViewportNavigationPointerTransitionKind::Pressed);
    }
    return viewport_navigation_press(open_navigation);
  }

  ViewportNavigationInput filtered = navigation;
  const auto retain_open_press_after_release =
      [&](bool &pressed,
          std::optional<ViewportPointerButtonEdge> &press_edge,
          bool released,
          const std::optional<ViewportPointerButtonEdge> &own_release_edge) {
    const bool sequenced_after_active_release =
        press_edge.has_value() && press_edge->sequence != 0u &&
        press_edge->sequence > release_edge->sequence;
    const bool still_open =
        !released ||
        (press_edge.has_value() && own_release_edge.has_value() &&
         own_release_edge->sequence != 0u &&
         own_release_edge->sequence < press_edge->sequence);
    if (!sequenced_after_active_release || !still_open) {
      pressed = false;
      press_edge.reset();
    }
  };
  retain_open_press_after_release(
      filtered.fly_pressed, filtered.fly_pressed_pointer_edge,
      filtered.fly_released, filtered.fly_released_pointer_edge);
  retain_open_press_after_release(
      filtered.pan_pressed, filtered.pan_pressed_pointer_edge,
      filtered.pan_released, filtered.pan_released_pointer_edge);
  retain_open_press_after_release(
      filtered.orbit_snap_pressed,
      filtered.orbit_snap_pressed_pointer_edge,
      filtered.orbit_snap_released,
      filtered.orbit_snap_released_pointer_edge);
  retain_open_press_after_release(
      filtered.orbit_pressed, filtered.orbit_pressed_pointer_edge,
      filtered.orbit_released, filtered.orbit_released_pointer_edge);
  return viewport_navigation_press(filtered);
}

bool is_orbit_navigation_action(ViewportInputActionKind action) {
  return action == ViewportInputActionKind::Orbit ||
         action == ViewportInputActionKind::OrbitSnap;
}

ViewportDragMode
navigation_mode_for_action(ViewportInputActionKind action,
                           const ViewportCameraController &camera) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    return ViewportDragMode::Orbit;
  case ViewportInputActionKind::OrbitSnap:
    return ViewportDragMode::OrbitSnap;
  case ViewportInputActionKind::Pan:
    return ViewportDragMode::Pan;
  case ViewportInputActionKind::Fly:
    return camera.state().projection == ViewportProjection::Orthographic
               ? ViewportDragMode::Pan
               : ViewportDragMode::Fly;
  default:
    return ViewportDragMode::None;
  }
}

} // namespace

ViewportInputRouter::ViewportInputRouter() {
  sync_grid_preset_snapshot(viewport_default_grid_preset());
}

ViewportInputResult
ViewportInputRouter::handle_frame(const ViewportInputFrame &input,
                                  ViewportCameraController &camera) {
  QDR_PROFILE_FUNCTION();
  ViewportInputResult result;
  camera.set_viewport_size(input.viewport_size.x, input.viewport_size.y);

  const bool accepts_mouse = input.viewport_hovered || navigating();
  const bool has_fly_movement_input =
      std::abs(input.fly_local_direction.x) > 0.000001f ||
      std::abs(input.fly_local_direction.y) > 0.000001f ||
      std::abs(input.fly_local_direction.z) > 0.000001f;
  bool fly_started_this_frame = false;
  bool navigation_started_this_frame = false;
  const bool navigation_canceled =
      input.pointer_interaction_canceled ||
      input.navigation_bindings.cancel_navigation_pressed;
  const ViewportNavigationPress navigation_press =
      viewport_navigation_press(input.navigation_bindings);
  const ViewportInputActionKind pressed_action =
      accepts_mouse && !navigating() && !navigation_canceled
          ? navigation_press.action
          : ViewportInputActionKind::None;
  if (pressed_action != ViewportInputActionKind::None) {
    QDR_PROFILE_SCOPE("Viewport navigation begin");
    active_navigation_action_ = pressed_action;
    navigation_mode_ = navigation_mode_for_action(pressed_action, camera);
    orbit_snap_action_uses_alt_modifier_ =
        pressed_action == ViewportInputActionKind::OrbitSnap &&
        input.modifiers.alt;
    fly_cursor_capture_active_ = navigation_mode_ == ViewportDragMode::Fly;
    fly_hud_active_ = false;
    camera.begin_drag(input.mouse_screen, navigation_mode_,
                      input.navigation_anchor,
                      input.navigation_anchor_repair_only);
    navigation_started_this_frame = true;
    fly_started_this_frame = navigation_mode_ == ViewportDragMode::Fly;
    result.consumed = true;
  }

  // Focus/capture loss is authoritative. Qt can deliver accumulated motion
  // and a synthesized release in the same retained frame; neither may change
  // the camera after pointer ownership was canceled.
  if (navigating() && navigation_canceled) {
    navigation_mode_ = ViewportDragMode::None;
    active_navigation_action_ = ViewportInputActionKind::None;
    orbit_snap_action_uses_alt_modifier_ = false;
    fly_cursor_capture_active_ = false;
    fly_hud_active_ = false;
    camera.end_drag();
    result.consumed = true;
    result.navigation_active = false;
    result.fly_navigation_active = false;
    result.fly_cursor_capture_active = false;
    result.fly_hud_active = false;
    return result;
  }

  const auto apply_pointer_navigation_motion =
      [&](ViewportInputActionKind action, QVec2 navigation_delta,
          bool allow_mode_change) {
    const ViewportDragMode active_navigation_mode =
        is_orbit_navigation_action(action)
            ? (input.modifiers.alt ||
                       (action == ViewportInputActionKind::OrbitSnap &&
                        !orbit_snap_action_uses_alt_modifier_)
                   ? ViewportDragMode::OrbitSnap
                   : ViewportDragMode::Orbit)
            : navigation_mode_;
    const bool locked_orthographic_orbit =
        camera.orthographic_view_locked() &&
        (active_navigation_mode == ViewportDragMode::Orbit ||
         active_navigation_mode == ViewportDragMode::OrbitSnap);
    const bool navigation_mode_changed =
        allow_mode_change && active_navigation_mode != navigation_mode_;
    const bool has_navigation_delta =
        std::abs(navigation_delta.x) > 0.000001f ||
        std::abs(navigation_delta.y) > 0.000001f;
    if (!locked_orthographic_orbit &&
        (has_navigation_delta || navigation_mode_changed)) {
      if (active_navigation_mode == ViewportDragMode::Orbit ||
          active_navigation_mode == ViewportDragMode::OrbitSnap) {
        const float device_scale =
            std::isfinite(input.device_pixel_ratio) &&
                    input.device_pixel_ratio > 0.0f
                ? input.device_pixel_ratio
                : 1.0f;
        navigation_delta = navigation_delta * (1.0f / device_scale);
      }
      camera.update_drag_delta(navigation_delta, active_navigation_mode);
      result.camera_changed = true;
    }
    navigation_mode_ = active_navigation_mode;
  };

  if (navigation_mode_ == ViewportDragMode::Fly) {
    QDR_PROFILE_SCOPE("Viewport fly navigation");
    if (input.navigation_bindings.reset_fly_speed_pressed) {
      camera.reset_fly_speed();
      result.consumed = true;
      result.fly_speed_changed = true;
      result.fly_speed_reset = true;
      result.fly_speed = camera.fly_speed();
    }

    if (!input.navigation_bindings.wheel_events.empty()) {
      for (const ViewportNavigationWheelEvent &event :
           input.navigation_bindings.wheel_events) {
        if ((event.action != ViewportInputActionKind::Zoom &&
             event.action != ViewportInputActionKind::ZoomToCursor) ||
            std::abs(event.steps) <= 0.000001f) {
          continue;
        }
        camera.adjust_fly_speed(event.steps);
        result.consumed = true;
        result.fly_speed_changed = true;
        result.fly_speed = camera.fly_speed();
      }
    } else {
      // Lightweight synthetic callers can continue to provide the historical
      // aggregate scalar and action flags without constructing event records.
      const bool fly_speed_wheel_active =
          input.navigation_bindings.zoom_wheel ||
          input.navigation_bindings.zoom_to_cursor_wheel;
      if (input.mouse_wheel != 0.0f && fly_speed_wheel_active) {
        camera.adjust_fly_speed(input.mouse_wheel);
        result.consumed = true;
        result.fly_speed_changed = true;
        result.fly_speed = camera.fly_speed();
      }
    }

    const bool fly_down =
        navigation_action_down(input, active_navigation_action_);
    const std::uint64_t active_press_sequence =
        navigation_started_this_frame &&
                navigation_press.pointer_edge.has_value()
            ? navigation_press.pointer_edge->sequence
            : 0u;
    const std::optional<ViewportPointerButtonEdge> fly_release_edge =
        navigation_action_release_pointer_edge(
            input.navigation_bindings, active_navigation_action_,
            active_press_sequence);
    const bool raw_fly_released =
        !input.navigation_bindings.pointer_transitions.empty()
            ? fly_release_edge.has_value()
            : navigation_action_released(input,
                                         active_navigation_action_);
    const bool release_precedes_selected_press =
        raw_fly_released &&
        pointer_edge_precedes(fly_release_edge,
                              navigation_press.pointer_edge);
    const bool fly_released =
        raw_fly_released &&
        !(navigation_started_this_frame &&
          release_precedes_selected_press);
    const ViewportNavigationPress restart_press =
        !navigation_started_this_frame && raw_fly_released
            ? navigation_press_after_release(input.navigation_bindings,
                                               fly_release_edge)
            : ViewportNavigationPress{};
    const bool restart_after_release =
        restart_press.action != ViewportInputActionKind::None;
    if (fly_down && !fly_started_this_frame && !restart_after_release) {
      if (has_fly_movement_input) {
        fly_hud_active_ = true;
      }
      camera.update_drag_delta(input.mouse_delta);
      camera.fly_move(input.fly_local_direction, input.delta_seconds);
      result.consumed = true;
      result.camera_changed = true;
    }

    if (fly_released || !fly_down || restart_after_release) {
      navigation_mode_ = ViewportDragMode::None;
      active_navigation_action_ = ViewportInputActionKind::None;
      orbit_snap_action_uses_alt_modifier_ = false;
      fly_cursor_capture_active_ = false;
      fly_hud_active_ = false;
      camera.end_drag();
      result.consumed = true;
      if (restart_after_release) {
        active_navigation_action_ = restart_press.action;
        navigation_mode_ =
            navigation_mode_for_action(restart_press.action, camera);
        orbit_snap_action_uses_alt_modifier_ =
            restart_press.action == ViewportInputActionKind::OrbitSnap &&
            input.modifiers.alt;
        fly_cursor_capture_active_ =
            navigation_mode_ == ViewportDragMode::Fly;
        const bool anchor_matches_restart =
            same_navigation_press(navigation_press, restart_press);
        camera.begin_drag(input.mouse_screen, navigation_mode_,
                          anchor_matches_restart ? input.navigation_anchor
                                                 : std::nullopt,
                          anchor_matches_restart &&
                              input.navigation_anchor_repair_only);
        if (restart_press.pointer_edge.has_value()) {
          apply_pointer_navigation_motion(
              restart_press.action,
              input.mouse_local - restart_press.pointer_edge->local, true);
        }
      }
    }

    result.navigation_active = navigating();
    result.fly_navigation_active = fly_navigating();
    result.fly_cursor_capture_active = fly_cursor_capture_active();
    result.fly_hud_active = fly_hud_active();
    return result;
  }

  if (navigating()) {
    QDR_PROFILE_SCOPE("Viewport drag navigation");
    const bool drag_down =
        navigation_action_down(input, active_navigation_action_);
    const std::uint64_t active_press_sequence =
        navigation_started_this_frame &&
                navigation_press.pointer_edge.has_value()
            ? navigation_press.pointer_edge->sequence
            : 0u;
    const std::optional<ViewportPointerButtonEdge> raw_release_pointer_edge =
        navigation_action_release_pointer_edge(
            input.navigation_bindings, active_navigation_action_,
            active_press_sequence);
    const bool raw_action_released =
        !input.navigation_bindings.pointer_transitions.empty()
            ? raw_release_pointer_edge.has_value()
            : navigation_action_released(input,
                                         active_navigation_action_);
    const bool release_precedes_selected_press =
        raw_action_released &&
        pointer_edge_precedes(raw_release_pointer_edge,
                              navigation_press.pointer_edge);
    const bool action_released =
        raw_action_released &&
        !(navigation_started_this_frame &&
          release_precedes_selected_press);
    const ViewportNavigationPress restart_press =
        !navigation_started_this_frame && raw_action_released
            ? navigation_press_after_release(input.navigation_bindings,
                                               raw_release_pointer_edge)
            : ViewportNavigationPress{};
    const bool restart_after_release =
        restart_press.action != ViewportInputActionKind::None;
    const bool drag_released = action_released;
    if (drag_down || action_released) {
      QVec2 navigation_delta = input.mouse_delta;
      const std::optional<ViewportPointerButtonEdge> release_pointer_edge =
          action_released
              ? raw_release_pointer_edge
              : std::nullopt;
      if (navigation_started_this_frame &&
          navigation_press.pointer_edge.has_value() &&
          release_pointer_edge.has_value()) {
        navigation_delta = release_pointer_edge->local -
                           navigation_press.pointer_edge->local;
      } else if (navigation_started_this_frame) {
        // The frame accumulator can contain motion from before the press. An
        // exact pointer edge lets us retain only movement that occurred after
        // this gesture began; synthetic/keyboard presses stay neutral.
        navigation_delta = navigation_press.pointer_edge.has_value()
                               ? input.mouse_local -
                                     navigation_press.pointer_edge->local
                               : QVec2{};
      } else if (release_pointer_edge.has_value()) {
        // Motion after the release can land in the same retained Qt frame.
        // Trim it using the exact semantic endpoint instead of rotating to
        // whichever unrelated pointer event happened last.
        navigation_delta =
            navigation_delta -
            (input.mouse_local - release_pointer_edge->local);
      }
      apply_pointer_navigation_motion(active_navigation_action_,
                                      navigation_delta, drag_down);
      result.consumed = true;
    }
    if (drag_released || !drag_down) {
      navigation_mode_ = ViewportDragMode::None;
      active_navigation_action_ = ViewportInputActionKind::None;
      orbit_snap_action_uses_alt_modifier_ = false;
      fly_cursor_capture_active_ = false;
      fly_hud_active_ = false;
      camera.end_drag();
      result.consumed = true;
      if (restart_after_release) {
        active_navigation_action_ = restart_press.action;
        navigation_mode_ =
            navigation_mode_for_action(restart_press.action, camera);
        orbit_snap_action_uses_alt_modifier_ =
            restart_press.action == ViewportInputActionKind::OrbitSnap &&
            input.modifiers.alt;
        fly_cursor_capture_active_ =
            navigation_mode_ == ViewportDragMode::Fly;
        const bool anchor_matches_restart =
            same_navigation_press(navigation_press, restart_press);
        camera.begin_drag(input.mouse_screen, navigation_mode_,
                          anchor_matches_restart ? input.navigation_anchor
                                                 : std::nullopt,
                          anchor_matches_restart &&
                              input.navigation_anchor_repair_only);
        if (restart_press.pointer_edge.has_value()) {
          apply_pointer_navigation_motion(
              restart_press.action,
              input.mouse_local - restart_press.pointer_edge->local, true);
        }
      }
    }
  }

  if (!input.navigation_bindings.wheel_events.empty()) {
    QDR_PROFILE_SCOPE("Viewport wheel zoom");
    for (const ViewportNavigationWheelEvent &event :
         input.navigation_bindings.wheel_events) {
      if (std::abs(event.steps) <= 0.000001f) {
        continue;
      }
      if (event.action == ViewportInputActionKind::ZoomToCursor) {
        camera.scroll_to_cursor(event.steps, event.local,
                                event.navigation_anchor);
        result.zoomed_to_cursor = true;
      } else if (event.action == ViewportInputActionKind::Zoom) {
        camera.scroll(event.steps, event.navigation_anchor);
      } else {
        continue;
      }
      result.consumed = true;
      result.camera_changed = true;
    }
  } else {
    // Lightweight synthetic callers can continue to provide the historical
    // aggregate scalar and action flags without constructing event records.
    const bool wheel_zoom_active =
        input.navigation_bindings.zoom_wheel ||
        input.navigation_bindings.zoom_to_cursor_wheel;
    if (input.viewport_hovered && input.mouse_wheel != 0.0f &&
        wheel_zoom_active) {
      QDR_PROFILE_SCOPE("Viewport wheel zoom");
      if (input.navigation_bindings.zoom_to_cursor_wheel) {
        camera.scroll_to_cursor(input.mouse_wheel, input.mouse_local,
                                input.navigation_anchor);
        result.zoomed_to_cursor = true;
      } else {
        camera.scroll(input.mouse_wheel, input.navigation_anchor);
      }
      result.consumed = true;
      result.camera_changed = true;
    }
  }

  const bool accepts_shortcuts =
      input.viewport_focused && !input.text_input_active && !navigating();
  if (accepts_shortcuts) {
    QDR_PROFILE_SCOPE("Viewport shortcuts");
    if (input.keybinding_input_consumed) {
      result.consumed = true;
    }
    for (const ViewportShortcut &shortcut : input.resolved_shortcuts) {
      if (shortcut.kind == ViewportShortcutKind::None) {
        continue;
      }
      if (handle_shortcut(shortcut, camera, result,
                          input.active_tool_blocks_focus_shortcuts)) {
        result.consumed = true;
        result.shortcut_handled = true;
        result.shortcut = shortcut;
      }
    }
  }

  result.navigation_active = navigating();
  result.fly_navigation_active = fly_navigating();
  result.fly_cursor_capture_active = fly_cursor_capture_active();
  result.fly_hud_active = fly_hud_active();
  return result;
}

void ViewportInputRouter::reset() {
  navigation_mode_ = ViewportDragMode::None;
  active_navigation_action_ = ViewportInputActionKind::None;
  orbit_snap_action_uses_alt_modifier_ = false;
  fly_cursor_capture_active_ = false;
  fly_hud_active_ = false;
}

void ViewportInputRouter::set_active_tool(ViewportToolId tool) {
  state_.active_tool = tool;
}

void ViewportInputRouter::set_selection_mode(ViewportSelectionMode mode) {
  state_.selection_mode = mode;
}

void ViewportInputRouter::sync_grid_preset_snapshot(int preset) {
  state_.grid_preset = viewport_clamp_grid_preset(preset);
}

bool ViewportInputRouter::handle_shortcut(const ViewportShortcut &shortcut,
                                          ViewportCameraController &camera,
                                          ViewportInputResult &result,
                                          bool active_tool_blocks_focus_shortcuts) {
  switch (shortcut.kind) {
  case ViewportShortcutKind::ActivateTool:
    result.tool_changed = result.tool_changed ||
                          state_.active_tool != shortcut.tool;
    state_.active_tool = shortcut.tool;
    result.tool_activation_requested = true;
    return true;
  case ViewportShortcutKind::SetSelectionMode:
    result.selection_mode_changed =
        result.selection_mode_changed ||
        state_.selection_mode != shortcut.selection_mode;
    state_.selection_mode = shortcut.selection_mode;
    return true;
  case ViewportShortcutKind::FocusDocument:
    if (active_tool_blocks_focus_shortcuts) {
      return false;
    }
    result.focus_document_requested = true;
    result.camera_changed = true;
    return true;
  case ViewportShortcutKind::FocusOrigin:
    camera.focus_origin();
    result.camera_changed = true;
    return true;
  case ViewportShortcutKind::ToggleMaximized:
    if (active_tool_blocks_focus_shortcuts) {
      return false;
    }
    result.maximized_toggle_requested = true;
    return true;
  case ViewportShortcutKind::SetGridPreset:
    result.requested_grid_preset =
        viewport_clamp_grid_preset(shortcut.grid_preset);
    result.grid_preset_changed =
        state_.grid_preset != result.requested_grid_preset;
    return true;
  case ViewportShortcutKind::SetView:
    result.requested_view = shortcut.view;
    result.view_changed = true;
    return true;
  case ViewportShortcutKind::CancelActiveTool:
    result.cancel_requested = true;
    return true;
  case ViewportShortcutKind::DeletePopup:
  case ViewportShortcutKind::AddPopup:
  case ViewportShortcutKind::ShadingPopup:
    return !active_tool_blocks_focus_shortcuts;
  case ViewportShortcutKind::FlipMeshNormals:
    return !active_tool_blocks_focus_shortcuts &&
           state_.selection_mode == ViewportSelectionMode::Object;
  case ViewportShortcutKind::FlipHorizontal:
  case ViewportShortcutKind::FlipVertical:
    return state_.selection_mode == ViewportSelectionMode::Object;
  case ViewportShortcutKind::DeleteSelection:
  case ViewportShortcutKind::SelectAll:
  case ViewportShortcutKind::InvertSelection:
  case ViewportShortcutKind::Copy:
  case ViewportShortcutKind::Paste:
  case ViewportShortcutKind::Duplicate:
  case ViewportShortcutKind::Undo:
  case ViewportShortcutKind::Redo:
  case ViewportShortcutKind::RepeatLastAction:
  case ViewportShortcutKind::MergeActive:
  case ViewportShortcutKind::MergeCenter:
  case ViewportShortcutKind::MirrorAxis:
    return true;
  case ViewportShortcutKind::None:
    return false;
  }
  return false;
}

const char *viewport_selection_mode_id(ViewportSelectionMode mode) {
  switch (mode) {
  case ViewportSelectionMode::Vertex:
    return "vertex";
  case ViewportSelectionMode::Edge:
    return "edge";
  case ViewportSelectionMode::Face:
    return "face";
  case ViewportSelectionMode::Object:
    return "object";
  case ViewportSelectionMode::Group:
    return "group";
  }
  return "unknown";
}

float viewport_grid_world_size_for_preset(int preset) {
  return viewport_grid_world_size_from_contract(preset);
}

} // namespace quader
