////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_focus_controller.hpp"

#include "ui/ui_context.hpp"

#include <cmath>

#include <imgui.h>

namespace quader {

bool ViewportFocusController::input_active() const {
  const ImGuiIO &io = ImGui::GetIO();
  return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
         io.MouseWheel != 0.0f ||
         (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
          (std::abs(io.MouseDelta.x) > 0.0f ||
           std::abs(io.MouseDelta.y) > 0.0f));
}

ViewportFocusState ViewportFocusController::resolve(bool focus_hovered,
                                                    const UIContext &ui_context,
                                                    UIPanelId panel_id) const {
  ViewportFocusState state;
  state.requested_this_frame = focus_hovered && input_active();
  if (state.requested_this_frame) {
    ui_context.request_panel_focus(panel_id);
    ImGui::SetWindowFocus();
  }

  const bool app_focused = ui_context.focused_panel == panel_id ||
                           (ui_context.focused_panel_storage != nullptr &&
                            *ui_context.focused_panel_storage == panel_id);
  state.focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
      app_focused || state.requested_this_frame;
  return state;
}

} // namespace quader
