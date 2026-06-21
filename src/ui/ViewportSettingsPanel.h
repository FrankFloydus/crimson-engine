#pragma once

#include "ui/UIFloatingPanel.h"

namespace quader {

class ViewportSettingsPanel final : public UIFloatingPanel {
public:
    bool draw(UIContext& context, bool& open);

protected:
    const char* title() const override { return "Settings"; }
    ImVec2 defaultSize() const override { return {420.0f, 560.0f}; }
    void drawContent(UIContext& context, bool& open) override;

private:
    bool changedThisFrame_ = false;
};

} // namespace quader
