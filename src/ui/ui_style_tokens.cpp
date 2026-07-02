////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_style_tokens.hpp"

#include "core/logger.hpp"
#include "ui/phosphor_icons.hpp"

#include <cstdio>

namespace quader {
namespace {

ImVec4 style_color(int red, int green, int blue, float alpha = 1.0f) {
  return {
      static_cast<float>(red) / 255.0f,
      static_cast<float>(green) / 255.0f,
      static_cast<float>(blue) / 255.0f,
      alpha,
  };
}

} // namespace

void apply_ui_style(const UIStyleTokens &tokens) {
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowPadding = tokens.window_padding;
  style.FramePadding = tokens.frame_padding;
  style.ItemSpacing = tokens.item_spacing;
  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 4.0f;
  style.TabRounding = 4.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.WindowMenuButtonPosition = ImGuiDir_Left;

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_Text] = style_color(216, 216, 216);
  colors[ImGuiCol_TextDisabled] = style_color(131, 131, 131);
  colors[ImGuiCol_WindowBg] = style_color(15, 15, 15);
  colors[ImGuiCol_ChildBg] = style_color(15, 15, 15);
  colors[ImGuiCol_PopupBg] = style_color(15, 15, 15);
  colors[ImGuiCol_Border] = style_color(72, 74, 82);
  colors[ImGuiCol_FrameBg] = style_color(23, 24, 27);
  colors[ImGuiCol_FrameBgHovered] = style_color(38, 40, 45);
  colors[ImGuiCol_FrameBgActive] = style_color(50, 54, 61);
  colors[ImGuiCol_TitleBg] = style_color(25, 25, 25);
  colors[ImGuiCol_TitleBgActive] = style_color(25, 25, 25);
  colors[ImGuiCol_TitleBgCollapsed] = style_color(20, 20, 20);
  colors[ImGuiCol_MenuBarBg] = style_color(25, 25, 25);
  colors[ImGuiCol_Button] = style_color(25, 25, 25);
  colors[ImGuiCol_ButtonHovered] = style_color(46, 46, 46);
  colors[ImGuiCol_ButtonActive] = style_color(60, 60, 60);
  colors[ImGuiCol_Header] = style_color(25, 25, 25);
  colors[ImGuiCol_HeaderHovered] = style_color(59, 41, 82);
  colors[ImGuiCol_HeaderActive] = style_color(36, 23, 52);
  colors[ImGuiCol_Tab] = style_color(20, 20, 20);
  colors[ImGuiCol_TabHovered] = style_color(59, 41, 82);
  colors[ImGuiCol_TabSelected] = style_color(25, 25, 25);
  colors[ImGuiCol_TabSelectedOverline] = style_color(183, 139, 233);
  colors[ImGuiCol_Separator] = style_color(40, 40, 40);
  colors[ImGuiCol_SeparatorHovered] = style_color(183, 139, 233);
  colors[ImGuiCol_SeparatorActive] = style_color(154, 100, 216);
  colors[ImGuiCol_CheckMark] = style_color(236, 241, 245);
  colors[ImGuiCol_SliderGrab] = style_color(183, 139, 233);
  colors[ImGuiCol_SliderGrabActive] = style_color(154, 100, 216);
  colors[ImGuiCol_ResizeGrip] = style_color(91, 91, 91, 0.35f);
  colors[ImGuiCol_ResizeGripHovered] = style_color(183, 139, 233, 0.7f);
  colors[ImGuiCol_ResizeGripActive] = style_color(154, 100, 216, 0.9f);
  colors[ImGuiCol_DockingPreview] = style_color(183, 139, 233, 0.35f);
}

void load_ui_default_font(const std::filesystem::path &executable_dir,
                          const UIStyleTokens &tokens) {
  ImGuiIO &io = ImGui::GetIO();
  static constexpr ImWchar kIconRanges[] = {kPhosphorIconRangeStart,
                                            kPhosphorIconRangeEnd, 0};
  const std::filesystem::path font_path =
      executable_dir / "fonts" / "Inter.ttf";
  ImFontConfig config;
  config.FontDataOwnedByAtlas = true;
  config.GlyphExcludeRanges = kIconRanges;
  std::snprintf(config.Name, sizeof(config.Name), "Inter");

  ImFont *font = io.Fonts->AddFontFromFileTTF(font_path.string().c_str(),
                                              tokens.font_size, &config);
  if (font == nullptr) {
    LOG_WARN("Unable to load Inter font from {}; using ImGui default font",
             font_path.string());
    io.Fonts->AddFontDefault();
    return;
  }

  io.FontDefault = font;
  LOG_INFO("Loaded Inter as default ImGui font from {}", font_path.string());

  const std::filesystem::path icon_font_path =
      executable_dir / "fonts" / "Phosphor.ttf";
  ImFontConfig icon_config;
  icon_config.FontDataOwnedByAtlas = true;
  icon_config.MergeMode = true;
  icon_config.PixelSnapH = true;
  std::snprintf(icon_config.Name, sizeof(icon_config.Name), "Phosphor");
  if (io.Fonts->AddFontFromFileTTF(icon_font_path.string().c_str(),
                                   tokens.font_size, &icon_config,
                                   kIconRanges) == nullptr) {
    LOG_WARN("Unable to load Phosphor icon font from {}; ImGui icon glyphs may "
             "be unavailable",
             icon_font_path.string());
    return;
  }

  LOG_INFO("Merged Phosphor icon font into ImGui atlas from {}",
           icon_font_path.string());
}

ImVec4 ui_log_level_color(const UIStyleTokens &tokens,
                          spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
    return tokens.log_trace;
  case spdlog::level::debug:
    return tokens.log_debug;
  case spdlog::level::warn:
    return tokens.log_warn;
  case spdlog::level::err:
  case spdlog::level::critical:
    return tokens.log_error;
  default:
    return tokens.log_info;
  }
}

} // namespace quader
