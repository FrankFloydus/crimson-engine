////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/ui_floating_panel.hpp"

namespace quader {

/**
 * Owns Tool Settings Panel behavior within the ImGui editor presentation layer.
 */
class ToolSettingsPanel final : public UIFloatingPanel {
public:
    void draw(UIContext& context, bool& open);

protected:
    const char* title() const override { return "Tool Settings"; }
    const char *title_key() const override { return "ui.panel.tool_settings"; }
    const char *title_stable_id() const override {
      return "floating.tool_settings";
    }
    ImVec2 default_size() const override { return {340.0f, 220.0f}; }
    void before_begin(UIContext &context, bool &open) override;
    void draw_content(UIContext &context, bool &open) override;

  private:
    bool should_open_for_active_target_ = false;
};

} // namespace quader
