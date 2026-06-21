#pragma once

#include "ui/UIDockPanel.h"

namespace quader {

class InspectorPanel final : public UIDockPanel {
public:
    UIPanelId id() const override { return UIPanelId::Inspector; }

private:
    float minimumWidth(const UIContext& context) const override;
    void drawContent(UIContext& context) override;
};

} // namespace quader
