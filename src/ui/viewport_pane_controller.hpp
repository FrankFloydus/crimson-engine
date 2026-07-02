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
 * Coordinates Viewport Pane Controller state transitions for the ImGui editor presentation layer.
 */
class ViewportPaneController {
public:
  void reset_camera(ViewportPaneId pane_id,
                    ViewportCameraController &camera) const;
  void reset_camera(ViewportView view, ViewportCameraController &camera) const;

  [[nodiscard]] static ImVec2
  pane_rect_min(ImVec2 viewport_min, ImVec2 viewport_size,
                ViewportNormalizedRect normalized_rect);
  [[nodiscard]] static ImVec2
  pane_rect_size(ImVec2 viewport_size, ViewportNormalizedRect normalized_rect);
};

} // namespace quader
