#pragma once

#include "quader/core/Types.h"
#include "quader/ui/UIPanel.h"

namespace quader {

class OutlinerPanel final : public UIPanel {
public:
    UIPanelId id() const override { return UIPanelId::Outliner; }
    const char* title() const override { return uiPanelKey(UIPanelId::Outliner); }
    void draw(UIContext& context) override;

private:
    EntityId outlinerAnchor_ = InvalidEntityId;
};

} // namespace quader
