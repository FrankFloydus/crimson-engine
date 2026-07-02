////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_pane_controller.hpp"

#include "viewport/viewport_workspace_service.hpp"

namespace quader {

void ViewportPaneController::reset_camera(
    ViewportPaneId pane_id, ViewportCameraController &camera) const {
  reset_camera(viewport_view_for_pane(pane_id), camera);
}

void ViewportPaneController::reset_camera(
    ViewportView view, ViewportCameraController &camera) const {
  reset_viewport_camera_for_view(camera, view);
}

ImVec2
ViewportPaneController::pane_rect_min(ImVec2 viewport_min, ImVec2 viewport_size,
                                      ViewportNormalizedRect normalized_rect) {
  const ViewportNormalizedRect absolute =
      viewport_absolute_pane_rect({.x = viewport_min.x,
                                   .y = viewport_min.y,
                                   .width = viewport_size.x,
                                   .height = viewport_size.y},
                                  normalized_rect);
  return {absolute.x, absolute.y};
}

ImVec2
ViewportPaneController::pane_rect_size(ImVec2 viewport_size,
                                       ViewportNormalizedRect normalized_rect) {
  const ViewportNormalizedRect absolute =
      viewport_absolute_pane_rect({.x = 0.0f,
                                   .y = 0.0f,
                                   .width = viewport_size.x,
                                   .height = viewport_size.y},
                                  normalized_rect);
  return {absolute.width, absolute.height};
}

} // namespace quader
