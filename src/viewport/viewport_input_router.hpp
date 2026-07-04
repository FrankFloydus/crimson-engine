////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_types.hpp"

#include <memory>

namespace quader {

struct ModelingBoxPreview;
class ViewportToolPreviewPort;
struct ViewportToolContext;
struct ViewportToolEvent;
struct ViewportToolResult;

/**
 * Owns Viewport Input Router behavior within the viewport input, layout, and camera layer.
 */
class ViewportInputRouter {
public:
    ViewportInputRouter();
    ~ViewportInputRouter();

    ViewportInputResult handle_frame(const ViewportInputFrame &input,
                                     ViewportCameraController &camera);
    void reset();
    void set_active_tool(ViewportToolId tool);
    void set_selection_mode(ViewportSelectionMode mode);
    void sync_grid_preset_snapshot(int preset);
    bool active_tool_owns_viewport() const;
    bool active_tool_blocks_viewport_focus_shortcuts() const;
    bool active_tool_allows_selection_fallback() const;
    bool has_active_modal_preview(const ViewportToolPreviewPort &preview,
                                  bool snap_enabled = true) const;
    ModelingBoxPreview box_preview(const ViewportToolPreviewPort &preview,
                                   bool snap_enabled = true) const;
    void clear_modal_tool_state();
    void clear_modal_drag_state();
    ViewportToolResult handle_active_tool_event(ViewportToolContext &context,
                                                const ViewportToolEvent &event);

    const ViewportToolState& state() const { return state_; }
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
    bool pan_navigating() const {
      return navigation_mode_ == ViewportDragMode::Pan;
    }
    bool fly_cursor_capture_active() const {
      return fly_cursor_capture_active_;
    }
    bool fly_hud_active() const { return fly_hud_active_; }
    const class ViewportTool *active_tool() const;

  private:
    bool handle_shortcut(const ViewportShortcut &shortcut,
                         ViewportCameraController &camera,
                         ViewportInputResult &result);

    ViewportToolState state_;
    ViewportDragMode navigation_mode_ = ViewportDragMode::None;
    ViewportInputActionKind active_navigation_action_ =
        ViewportInputActionKind::None;
    bool orbit_snap_action_uses_alt_modifier_ = false;
    bool fly_cursor_capture_active_ = false;
    bool fly_hud_active_ = false;
    std::unique_ptr<class ViewportToolRuntime> tool_runtime_;
};

} // namespace quader
