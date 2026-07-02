////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>
#include <spdlog/common.h>

#include <filesystem>

namespace quader {

/**
 * Represents an UI Style Tokens value used by the ImGui editor presentation layer.
 */
struct UIStyleTokens {
  float font_size = 18.0f;
  ImVec2 window_padding = {8.0f, 8.0f};
  ImVec2 frame_padding = {7.0f, 4.0f};
  ImVec2 item_spacing = {8.0f, 6.0f};
  ImGuiWindowFlags panel_window_flags = ImGuiWindowFlags_NoCollapse;
  float sidebar_min_width = 340.0f;

  ImVec4 axis_x = {0.95f, 0.28f, 0.28f, 1.0f};
  ImVec4 axis_y = {0.36f, 0.82f, 0.34f, 1.0f};
  ImVec4 axis_z = {0.33f, 0.58f, 1.0f, 1.0f};

  ImVec4 log_trace = {0.62f, 0.62f, 0.62f, 1.0f};
  ImVec4 log_debug = {0.48f, 0.72f, 1.0f, 1.0f};
  ImVec4 log_info = {0.86f, 0.88f, 0.90f, 1.0f};
  ImVec4 log_warn = {1.0f, 0.78f, 0.32f, 1.0f};
  ImVec4 log_error = {1.0f, 0.32f, 0.32f, 1.0f};
};

void apply_ui_style(const UIStyleTokens &tokens);
void load_ui_default_font(const std::filesystem::path &executable_dir,
                          const UIStyleTokens &tokens);
ImVec4 ui_log_level_color(const UIStyleTokens &tokens,
                          spdlog::level::level_enum level);

} // namespace quader
