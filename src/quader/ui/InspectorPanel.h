#pragma once

#include "quader/ui/UIPanel.h"

namespace quader {

class InspectorPanel final : public UIPanel {
public:
    UIPanelId id() const override { return UIPanelId::Inspector; }
    const char* title() const override { return uiPanelKey(UIPanelId::Inspector); }
    void draw(UIContext& context) override;
};

} // namespace quader
