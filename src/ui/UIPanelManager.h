#pragma once

#include "ui/UIPanel.h"
#include "ui/UISettings.h"

#include <memory>
#include <vector>

namespace quader {

class UIPanelManager {
public:
    void add(std::unique_ptr<UIPanel> panel);
    void applySettings(const UISettings& settings);
    void writeSettings(UISettings& settings) const;

    bool isVisible(UIPanelId id) const;
    void setVisible(UIPanelId id, bool visible);
    UIPanel* panel(UIPanelId id);

    void draw(UIContext& context);
    const std::vector<std::unique_ptr<UIPanel>>& panels() const { return panels_; }

private:
    bool defaultVisibility(UIPanelId id) const;

    std::vector<std::unique_ptr<UIPanel>> panels_;
    std::vector<bool> visibility_;
};

} // namespace quader
