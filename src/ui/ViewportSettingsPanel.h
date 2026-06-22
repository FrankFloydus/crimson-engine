////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/UIFloatingPanel.h"

namespace quader {

/**
 * Owns Viewport Settings Panel behavior within the ImGui editor presentation layer.
 */
class ViewportSettingsPanel final : public UIFloatingPanel {
public:
    bool draw(UIContext& context, bool& open);

protected:
    const char* title() const override { return "Settings"; }
    const char* titleKey() const override { return "ui.panel.settings"; }
    const char* titleStableId() const override { return "floating.settings"; }
    ImVec2 defaultSize() const override { return {420.0f, 560.0f}; }
    void drawContent(UIContext& context, bool& open) override;

private:
    bool changedThisFrame_ = false;
};

} // namespace quader
