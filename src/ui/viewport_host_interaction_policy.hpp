////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"

#include <cstddef>
#include <span>

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
  bool linear_array_active = false;
  bool navigation_active = false;
  bool left_button_release_only = false;
  bool insert_edge_loop_dragging = false;
  bool cut_dragging = false;
  bool mirror_dragging = false;
};

[[nodiscard]] constexpr bool should_cancel_modal_tool_for_external_pointer(
    const ViewportExternalPointerCancelRequest &request) {
  if (!request.external_pointer_event ||
      !request.modal_tool_owns_viewport || request.linear_array_active ||
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
 * its generic Wheel fallback so one physical event cannot trigger both.
 */
template <typename DispatchEdge>
void dispatch_ordered_viewport_input_edges(
    std::span<const QuaderInputChord> pressed,
    std::span<const QuaderInputChord> wheel, DispatchEdge &&dispatch_edge) {
  for (const QuaderInputChord &chord : pressed) {
    (void)dispatch_edge(chord, ViewportInputEdgeKind::Pressed);
  }

  for (std::size_t index = 0; index < wheel.size();) {
    if (index + 1u < wheel.size() &&
        viewport_wheel_chords_share_event(wheel[index], wheel[index + 1u])) {
      if (!dispatch_edge(wheel[index + 1u], ViewportInputEdgeKind::Wheel)) {
        (void)dispatch_edge(wheel[index], ViewportInputEdgeKind::Wheel);
      }
      index += 2u;
      continue;
    }
    (void)dispatch_edge(wheel[index], ViewportInputEdgeKind::Wheel);
    ++index;
  }
}

/**
 * Gives a pending multi-chord sequence ownership of its continuation before a
 * stateless single-chord action is considered.
 */
template <typename HasPendingChord, typename TryStatelessAction,
          typename DispatchBinding>
[[nodiscard]] bool dispatch_viewport_input_edge(
    const QuaderInputChord &chord, ViewportInputEdgeKind kind,
    HasPendingChord &&has_pending_chord,
    TryStatelessAction &&try_stateless_action,
    DispatchBinding &&dispatch_binding) {
  if (has_pending_chord()) {
    (void)dispatch_binding(chord, kind);
    return true;
  }
  if (try_stateless_action(chord, kind)) {
    return true;
  }
  return dispatch_binding(chord, kind);
}

/// Lists semantic contributors that can require the renderer's overlay pass.
struct ViewportOverlayPassVisibilityRequest {
  bool overlays_visible = false;
  bool gizmos_visible = false;
  bool wireframe_pane = false;
  bool pivot_handle_visible = false;
  bool linear_array_handle_visible = false;
  bool tool_overlay_visible = false;
};

[[nodiscard]] constexpr bool should_enable_viewport_overlay_pass(
    const ViewportOverlayPassVisibilityRequest &request) {
  return request.overlays_visible || request.gizmos_visible ||
         request.wireframe_pane || request.pivot_handle_visible ||
         request.linear_array_handle_visible || request.tool_overlay_visible;
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
