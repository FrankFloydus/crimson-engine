////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/localization_service.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_style_tokens.hpp"

#include <imgui.h>

#include <string>

namespace quader {

/**
 * Owns UI Floating Panel behavior within the ImGui editor presentation layer.
 */
class UIFloatingPanel {
public:
    virtual ~UIFloatingPanel() = default;

    void draw_floating(UIContext &context, bool &open) {
      if (!open) {
        return;
      }

      before_begin(context, open);
      ImGui::SetNextWindowSize(default_size(), ImGuiCond_FirstUseEver);
      const std::string window_title = context.localization.imgui_label(
          title_key(), title_stable_id(), title());
      if (ImGui::Begin(window_title.c_str(), &open, window_flags(context))) {
        draw_content(context, open);
      }
      ImGui::End();
    }

protected:
    virtual const char* title() const = 0;
    virtual const char *title_key() const { return title(); }
    virtual const char *title_stable_id() const { return title(); }
    virtual void draw_content(UIContext &context, bool &open) = 0;

    virtual ImVec2 default_size() const { return {360.0f, 240.0f}; }

    virtual void before_begin(UIContext &context, bool &open) {
      (void)context;
      (void)open;
    }

    virtual ImGuiWindowFlags window_flags(const UIContext &context) const {
      return context.style.panel_window_flags | ImGuiWindowFlags_NoDocking;
    }
};

} // namespace quader
