////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

#include <algorithm>
#include <limits>

namespace quader {

inline float ui_fill_width_at_least(float minimum_width) {
  return std::max(ImGui::GetContentRegionAvail().x, minimum_width);
}

inline void set_next_item_fill_width_at_least(float minimum_width) {
  ImGui::SetNextItemWidth(ui_fill_width_at_least(minimum_width));
}

inline void set_next_window_min_width(float minimum_width) {
  ImGui::SetNextWindowSizeConstraints(
      {minimum_width, 0.0f},
      {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
}

inline bool panel_focus_input_active() {
  const ImGuiIO &io = ImGui::GetIO();
  return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
         io.MouseWheel != 0.0f;
}

} // namespace quader
