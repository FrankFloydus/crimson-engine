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
 * Owns Tool Settings Panel behavior within the ImGui editor presentation layer.
 */
class ToolSettingsPanel final : public UIFloatingPanel {
public:
    void draw(UIContext& context, bool& open);

protected:
    const char* title() const override { return "Tool Settings"; }
    const char* titleKey() const override { return "ui.panel.tool_settings"; }
    const char* titleStableId() const override { return "floating.tool_settings"; }
    ImVec2 defaultSize() const override { return {340.0f, 220.0f}; }
    void beforeBegin(UIContext& context, bool& open) override;
    void drawContent(UIContext& context, bool& open) override;

private:
    bool shouldOpenForActiveTarget_ = false;
};

} // namespace quader
