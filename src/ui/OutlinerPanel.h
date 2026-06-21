#pragma once

#include "core/Types.h"
#include "ui/UIDockPanel.h"

namespace quader {

class OutlinerPanel final : public UIDockPanel {
public:
    UIPanelId id() const override { return UIPanelId::Outliner; }

private:
    float minimumWidth(const UIContext& context) const override;
    void drawContent(UIContext& context) override;

    EntityId outlinerAnchor_ = InvalidEntityId;
};

} // namespace quader
