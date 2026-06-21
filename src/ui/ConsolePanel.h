#pragma once

#include "ui/ConsoleModel.h"
#include "ui/UIDockPanel.h"

namespace quader {

class ConsolePanel final : public UIDockPanel {
public:
    UIPanelId id() const override { return UIPanelId::Console; }
    bool defaultVisible() const override { return false; }

private:
    void drawContent(UIContext& context) override;

    ConsoleFilter filter_;
    bool autoScroll_ = true;
};

} // namespace quader
