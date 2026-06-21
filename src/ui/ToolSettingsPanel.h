#pragma once

#include "ui/UIFloatingPanel.h"

namespace quader {

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
