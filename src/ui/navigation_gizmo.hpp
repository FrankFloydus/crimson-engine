////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_camera_controller.hpp"
#include "api/quader_viewport_layout_types.hpp"

#include <imgui.h>

namespace quader {

/**
 * Owns Navigation Gizmo behavior within the ImGui editor presentation layer.
 */
class NavigationGizmo final {
public:
    [[nodiscard]] bool visible() const { return visible_; }
    void set_visible(bool visible);

    [[nodiscard]] bool input_locked() const {
      return dragging_ && drag_pane_active_;
    }
    [[nodiscard]] ViewportPaneId locked_pane_id() const {
      return drag_pane_id_;
    }

    bool draw(ViewportCameraController &camera, ViewportPaneId pane_id,
              ImVec2 item_min, ImVec2 item_size, QVec2 captured_mouse_delta,
              bool *viewport_cursor_capture_requested,
              QVec2 *viewport_cursor_capture_point, bool interactive = true);

  private:
    bool visible_ = true;
    bool dragging_ = false;
    bool drag_pane_active_ = false;
    ViewportPaneId drag_pane_id_ = ViewportPaneId::Perspective;
    bool drag_started_ = false;
    bool pressed_face_ = false;
    ViewportViewAxis pressed_axis_ = ViewportViewAxis::Z;
    int pressed_sign_ = 1;
    QVec2 accumulated_drag_ = {};
    QVec2 cursor_capture_screen_ = {};
};

} // namespace quader
