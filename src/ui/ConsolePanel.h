#pragma once

#include "ui/ConsoleModel.h"
#include "ui/UIPanel.h"

namespace quader {

class ConsolePanel final : public UIPanel {
public:
    UIPanelId id() const override { return UIPanelId::Console; }
    const char* title() const override { return uiPanelKey(UIPanelId::Console); }
    bool defaultVisible() const override { return false; }
    void draw(UIContext& context) override;

private:
    ConsoleFilter filter_;
    bool autoScroll_ = true;
};

} // namespace quader
