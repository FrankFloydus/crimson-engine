////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_types.hpp"

namespace quader {

/** Identifies the selected navigation action and its exact event-time press. */
struct ViewportNavigationPress {
  ViewportInputActionKind action = ViewportInputActionKind::None;
  std::optional<ViewportPointerButtonEdge> pointer_edge;
};

/** Selects one navigation press and its matching pointer provenance. */
[[nodiscard]] ViewportNavigationPress
viewport_navigation_press(const ViewportNavigationInput &navigation);

/**
 * Owns Viewport Input Router behavior within the viewport input, layout, and camera layer.
 */
class ViewportInputRouter {
public:
    ViewportInputRouter();

    ViewportInputResult handle_frame(const ViewportInputFrame &input,
                                     ViewportCameraController &camera);
    void reset();
    void set_active_tool(ViewportToolId tool);
    void set_selection_mode(ViewportSelectionMode mode);
    void sync_grid_preset_snapshot(int preset);

    ViewportToolState &state() { return state_; }
    const ViewportToolState &state() const { return state_; }
    bool navigating() const {
      return navigation_mode_ != ViewportDragMode::None;
    }
    bool fly_navigating() const {
      return navigation_mode_ == ViewportDragMode::Fly;
    }
    bool orbit_navigating() const {
      return navigation_mode_ == ViewportDragMode::Orbit ||
             navigation_mode_ == ViewportDragMode::OrbitSnap;
    }
    bool orbit_snap_navigating() const {
      return navigation_mode_ == ViewportDragMode::OrbitSnap;
    }
    bool pan_navigating() const {
      return navigation_mode_ == ViewportDragMode::Pan;
    }
    bool fly_cursor_capture_active() const {
      return fly_cursor_capture_active_;
    }
    bool fly_hud_active() const { return fly_hud_active_; }
  private:
    bool handle_shortcut(const ViewportShortcut &shortcut,
                         ViewportCameraController &camera,
                         ViewportInputResult &result,
                         bool active_tool_blocks_focus_shortcuts);

    ViewportToolState state_;
    ViewportDragMode navigation_mode_ = ViewportDragMode::None;
    ViewportInputActionKind active_navigation_action_ =
        ViewportInputActionKind::None;
    bool orbit_snap_action_uses_alt_modifier_ = false;
    bool fly_cursor_capture_active_ = false;
    bool fly_hud_active_ = false;
};

} // namespace quader
