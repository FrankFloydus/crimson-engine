////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"
#include "api/quader_viewport_layout_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace quader {

/// Identifies the event that requested a retained viewport pane focus change.
enum class ViewportPaneFocusTrigger {
  Hover,
  PointerPress,
  Wheel,
  FocusIn,
  Programmatic,
};

/// Supplies pane ownership and keyboard state for a focus-policy decision.
struct ViewportPaneFocusRequest {
  ViewportPaneFocusTrigger trigger = ViewportPaneFocusTrigger::Hover;
  bool interaction_locked = false;
  bool requested_pane_is_interaction_owner = false;
  bool viewport_keyboard_focus = false;
};

/// Separates workspace-pane focus from native child-window keyboard focus.
struct ViewportPaneFocusDecision {
  bool focus_workspace_pane = false;
  bool focus_native_surface = false;
};

/// Describes one active pane using only semantic input activity.
struct ViewportPaneInputCandidate {
  ViewportPaneId pane_id = ViewportPaneId::Perspective;
  bool has_input_activity = false;
  bool hovered = false;
  bool focused = false;
};

/// Captures the deterministic pane-precedence inputs for one host frame.
struct ViewportPaneInputResolutionRequest {
  std::span<const ViewportPaneInputCandidate> active_panes;
  bool interaction_locked = false;
  std::optional<ViewportPaneId> interaction_owner;
  ViewportPaneId focused_pane = ViewportPaneId::Perspective;
};

/** Resolves locked owner -> activity -> hover -> focus -> workspace focus. */
[[nodiscard]] inline std::optional<ViewportPaneId>
resolve_viewport_input_pane(
    const ViewportPaneInputResolutionRequest &request) {
  const auto contains = [&](ViewportPaneId pane_id) {
    return std::any_of(request.active_panes.begin(), request.active_panes.end(),
                       [&](const ViewportPaneInputCandidate &candidate) {
                         return candidate.pane_id == pane_id;
                       });
  };
  if (request.interaction_locked && request.interaction_owner.has_value() &&
      contains(*request.interaction_owner)) {
    return request.interaction_owner;
  }
  const auto first_matching = [&](auto predicate)
      -> std::optional<ViewportPaneId> {
    const auto found =
        std::find_if(request.active_panes.begin(), request.active_panes.end(),
                     predicate);
    return found != request.active_panes.end()
               ? std::optional<ViewportPaneId>(found->pane_id)
               : std::nullopt;
  };
  if (auto pane = first_matching([](const ViewportPaneInputCandidate &item) {
        return item.has_input_activity;
      })) {
    return pane;
  }
  if (auto pane = first_matching([](const ViewportPaneInputCandidate &item) {
        return item.hovered;
      })) {
    return pane;
  }
  if (auto pane = first_matching([](const ViewportPaneInputCandidate &item) {
        return item.focused;
      })) {
    return pane;
  }
  return contains(request.focused_pane)
             ? std::optional<ViewportPaneId>(request.focused_pane)
             : std::nullopt;
}

[[nodiscard]] constexpr ViewportPaneFocusDecision
viewport_pane_focus_decision(const ViewportPaneFocusRequest &request) {
  if (request.interaction_locked &&
      !request.requested_pane_is_interaction_owner) {
    return {};
  }

  const bool focus_native_surface =
      request.trigger == ViewportPaneFocusTrigger::PointerPress ||
      request.trigger == ViewportPaneFocusTrigger::Wheel ||
      request.trigger == ViewportPaneFocusTrigger::Programmatic ||
      (request.trigger == ViewportPaneFocusTrigger::Hover &&
       request.viewport_keyboard_focus);
  return {
      .focus_workspace_pane = true,
      .focus_native_surface = focus_native_surface,
  };
}

/// Supplies modal-tool state used to decide whether an outside pointer event cancels it.
struct ViewportExternalPointerCancelRequest {
  bool external_pointer_event = false;
  bool modal_tool_owns_viewport = false;
  bool persistent_native_tool_active = false;
  bool navigation_active = false;
  bool left_button_release_only = false;
  bool insert_edge_loop_dragging = false;
  bool cut_dragging = false;
  bool mirror_dragging = false;
};

[[nodiscard]] constexpr bool should_cancel_modal_tool_for_external_pointer(
    const ViewportExternalPointerCancelRequest &request) {
  if (!request.external_pointer_event ||
      !request.modal_tool_owns_viewport ||
      request.persistent_native_tool_active ||
      request.navigation_active) {
    return false;
  }
  if (!request.left_button_release_only) {
    return true;
  }
  return !request.insert_edge_loop_dragging && !request.cut_dragging &&
         !request.mirror_dragging;
}

/// Supplies router and editor state for a tool-activation dispatch decision.
struct ViewportToolActivationDispatchRequest {
  bool activation_requested = false;
  bool tool_changed = false;
  bool editor_tool_matches_requested = false;
};

/// Restarts a tool only when its shortcut requests a real editor transition.
[[nodiscard]] constexpr bool should_dispatch_viewport_tool_activation(
    const ViewportToolActivationDispatchRequest &request) {
  return request.activation_requested &&
         (request.tool_changed || !request.editor_tool_matches_requested);
}

/// Identifies the semantic edge represented by one canonical input chord.
enum class ViewportInputEdgeKind {
  Pressed,
  Wheel,
};

/// Returns whether Qt's generic/directional wheel chords describe one event.
[[nodiscard]] inline bool viewport_wheel_chords_share_event(
    const QuaderInputChord &generic, const QuaderInputChord &directional) {
  if (generic.gesture != QuaderInputGesture::Wheel ||
      (directional.gesture != QuaderInputGesture::WheelUp &&
       directional.gesture != QuaderInputGesture::WheelDown)) {
    return false;
  }
  QuaderInputChord normalized_directional = directional;
  normalized_directional.gesture = QuaderInputGesture::Wheel;
  return generic == normalized_directional;
}

/**
 * Dispatches discrete viewport edges deterministically. Keyboard presses keep
 * arrival order. A directional vertical-wheel binding gets first refusal over
 * its generic Wheel fallback so one physical event cannot trigger both. The
 * completion callback runs once after each physical wheel event and fallback.
 */
template <typename DispatchEdge, typename CompleteWheelEvent>
void dispatch_ordered_viewport_input_edges(
    std::span<const QuaderInputChord> pressed,
    std::span<const QuaderWheelInputEdge> wheel,
    DispatchEdge &&dispatch_edge,
    CompleteWheelEvent &&complete_wheel_event) {
  for (const QuaderInputChord &chord : pressed) {
    (void)dispatch_edge(chord, ViewportInputEdgeKind::Pressed, 0.0);
  }

  for (std::size_t index = 0; index < wheel.size();) {
    if (index + 1u < wheel.size() &&
        viewport_wheel_chords_share_event(wheel[index].chord,
                                          wheel[index + 1u].chord)) {
      if (!dispatch_edge(wheel[index + 1u].chord,
                         ViewportInputEdgeKind::Wheel,
                         wheel[index + 1u].step_delta)) {
        (void)dispatch_edge(wheel[index].chord, ViewportInputEdgeKind::Wheel,
                            wheel[index].step_delta);
      }
      complete_wheel_event(wheel.subspan(index, 2u));
      index += 2u;
      continue;
    }
    (void)dispatch_edge(wheel[index].chord, ViewportInputEdgeKind::Wheel,
                        wheel[index].step_delta);
    complete_wheel_event(wheel.subspan(index, 1u));
    ++index;
  }
}

template <typename DispatchEdge>
void dispatch_ordered_viewport_input_edges(
    std::span<const QuaderInputChord> pressed,
    std::span<const QuaderWheelInputEdge> wheel,
    DispatchEdge &&dispatch_edge) {
  dispatch_ordered_viewport_input_edges(
      pressed, wheel, std::forward<DispatchEdge>(dispatch_edge),
      [](std::span<const QuaderWheelInputEdge>) {});
}

/**
 * Converts signed high-resolution wheel input into discrete semantic steps.
 * Fractional motion is retained across frames and opposite motion cancels it.
 */
[[nodiscard]] inline int
accumulate_discrete_wheel_steps(double delta, double &remainder) {
  constexpr double kWheelAccumulatorEpsilon = 0.000000001;
  if (!std::isfinite(delta) || !std::isfinite(remainder)) {
    remainder = 0.0;
    return 0;
  }

  const double accumulated = remainder + delta;
  if (!std::isfinite(accumulated)) {
    remainder = 0.0;
    return 0;
  }
  const double whole = accumulated >= 0.0
                           ? std::floor(accumulated +
                                        kWheelAccumulatorEpsilon)
                           : std::ceil(accumulated -
                                       kWheelAccumulatorEpsilon);
  if (whole > static_cast<double>(std::numeric_limits<int>::max())) {
    remainder = 0.0;
    return std::numeric_limits<int>::max();
  }
  if (whole < static_cast<double>(std::numeric_limits<int>::min())) {
    remainder = 0.0;
    return std::numeric_limits<int>::min();
  }

  const int steps = static_cast<int>(whole);
  remainder = accumulated - whole;
  if (std::abs(remainder) <= kWheelAccumulatorEpsilon) {
    remainder = 0.0;
  }
  return steps;
}

/// Retains fractional motion only while consecutive physical wheel events
/// continue resolving to the same array-copy semantic action.
inline void finish_array_wheel_event(bool array_action_resolved,
                                     double &remainder) {
  if (!array_action_resolved) {
    remainder = 0.0;
  }
}

/// Drops fractional motion that points farther outside a bounded setting.
inline void reconcile_bounded_wheel_remainder(bool at_minimum,
                                              bool at_maximum,
                                              double &remainder) {
  if (!std::isfinite(remainder) ||
      (at_minimum && remainder < 0.0) ||
      (at_maximum && remainder > 0.0)) {
    remainder = 0.0;
  }
}

/**
 * Routes every edge through the stateful keybinding dispatcher before an
 * optional stateless fallback. A pending sequence owns even an unmatched
 * continuation so the continuation cannot accidentally trigger a fresh
 * single-chord action.
 */
template <typename HasPendingChord, typename TryStatelessAction,
          typename DispatchBinding>
[[nodiscard]] bool dispatch_viewport_input_edge(
    const QuaderInputChord &chord, ViewportInputEdgeKind kind,
    HasPendingChord &&has_pending_chord,
    TryStatelessAction &&try_stateless_action,
    DispatchBinding &&dispatch_binding) {
  const bool pending_before = has_pending_chord();
  if (dispatch_binding(chord, kind)) {
    return true;
  }
  if (pending_before) {
    return true;
  }
  return try_stateless_action(chord, kind);
}

/// Shows an unlocked placement marker only in its interaction pane and a
/// locked marker in every pane.
[[nodiscard]] constexpr bool should_show_point_placement_overlay(
    bool marker_visible, bool marker_locked, bool interaction_pane) {
  return marker_visible && (marker_locked || interaction_pane);
}

/// Lists semantic contributors that can require the renderer's overlay pass.
struct ViewportOverlayPassVisibilityRequest {
  bool overlays_visible = false;
  bool gizmos_visible = false;
  bool wireframe_pane = false;
  bool pivot_handle_visible = false;
  bool native_tool_overlay_visible = false;
  bool tool_overlay_visible = false;
};

[[nodiscard]] constexpr bool should_enable_viewport_overlay_pass(
    const ViewportOverlayPassVisibilityRequest &request) {
  return request.overlays_visible || request.gizmos_visible ||
         request.wireframe_pane || request.pivot_handle_visible ||
         request.native_tool_overlay_visible || request.tool_overlay_visible;
}

/**
 * Counts scene-overlay snapshot extractions for one prepare pass. Multiple
 * requesting panes share one immutable extraction.
 */
[[nodiscard]] constexpr std::size_t
viewport_scene_overlay_snapshot_extraction_count(
    std::span<const bool> pane_requests) {
  for (const bool requested : pane_requests) {
    if (requested) {
      return 1u;
    }
  }
  return 0u;
}

} // namespace quader
