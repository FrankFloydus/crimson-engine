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
 * Owns Viewport Settings Panel behavior within the ImGui editor presentation layer.
 */
class ViewportSettingsPanel final : public UIFloatingPanel {
public:
    bool draw(UIContext& context, bool& open);

protected:
    const char* title() const override { return "Settings"; }
    const char *title_key() const override { return "ui.panel.settings"; }
    const char *title_stable_id() const override { return "floating.settings"; }
    ImVec2 default_size() const override { return {420.0f, 560.0f}; }
    bool show_close_button() const override { return false; }
    void draw_content(UIContext &context, bool &open) override;

  private:
    bool changed_this_frame_ = false;
};

} // namespace quader
