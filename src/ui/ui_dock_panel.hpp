////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/localization_service.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_layout.hpp"
#include "ui/ui_panel.hpp"
#include "ui/ui_style_tokens.hpp"

#include <imgui.h>

#include <string>

namespace quader {

/**
 * Owns UI Dock Panel behavior within the ImGui editor presentation layer.
 */
class UIDockPanel : public UIPanel {
public:
  const char *title() const override { return ui_panel_key(id()); }

  void draw(UIContext &context) final {
    const float min_width = minimum_width(context);
    if (min_width > 0.0f) {
      set_next_window_min_width(min_width);
    }

    const std::string window_title = context.localization.imgui_label(
        ui_panel_localization_key(id()), ui_panel_key(id()), title());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        panel_window_padding(context));
    if (!ImGui::Begin(window_title.c_str(), nullptr, window_flags(context))) {
      ImGui::PopStyleVar();
      ImGui::End();
      return;
    }

    if (routes_panel_focus() &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        panel_focus_input_active()) {
      context.request_panel_focus(id());
    }

    draw_content(context);
    ImGui::PopStyleVar();
    ImGui::End();
  }

protected:
  virtual void draw_content(UIContext &context) = 0;
  virtual float minimum_width(const UIContext &context) const {
    (void)context;
    return 0.0f;
  }
  virtual ImGuiWindowFlags window_flags(const UIContext &context) const {
    return context.style.panel_window_flags;
  }
  virtual ImVec2 panel_window_padding(const UIContext &context) const {
    return context.style.window_padding;
  }
  virtual bool routes_panel_focus() const { return true; }
};

} // namespace quader
