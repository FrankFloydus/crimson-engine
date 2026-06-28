////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_panel_manager.hpp"

#include "ui/ui_context.hpp"

namespace quader {

void UIPanelManager::add(std::unique_ptr<UIPanel> panel) {
    if (!panel) {
        return;
    }
    visibility_.push_back(panel->defaultVisible());
    panels_.push_back(std::move(panel));
}

void UIPanelManager::applySettings(const UISettings& settings) {
    for (std::size_t index = 0; index < panels_.size(); ++index) {
        const UIPanel& panel = *panels_[index];
        visibility_[index] = panel.canToggle() ? settings.isPanelVisible(panel.id()) : true;
    }
}

void UIPanelManager::writeSettings(UISettings& settings) const {
    for (std::size_t index = 0; index < panels_.size(); ++index) {
        const UIPanel& panel = *panels_[index];
        if (panel.canToggle()) {
            settings.setPanelVisible(panel.id(), visibility_[index]);
        }
    }
}

bool UIPanelManager::isVisible(UIPanelId id) const {
    for (std::size_t index = 0; index < panels_.size(); ++index) {
        if (panels_[index]->id() == id) {
            return panels_[index]->canToggle() ? visibility_[index] : true;
        }
    }
    return defaultVisibility(id);
}

void UIPanelManager::setVisible(UIPanelId id, bool visible) {
    for (std::size_t index = 0; index < panels_.size(); ++index) {
        if (panels_[index]->id() == id && panels_[index]->canToggle()) {
            visibility_[index] = visible;
            return;
        }
    }
}

UIPanel* UIPanelManager::panel(UIPanelId id) {
    for (const std::unique_ptr<UIPanel>& panel : panels_) {
        if (panel->id() == id) {
            return panel.get();
        }
    }
    return nullptr;
}

void UIPanelManager::draw(UIContext& context) {
    for (std::size_t index = 0; index < panels_.size(); ++index) {
        UIPanel& panel = *panels_[index];
        if (!panel.canToggle() || visibility_[index]) {
            panel.draw(context);
        }
    }
}

bool UIPanelManager::defaultVisibility(UIPanelId id) const {
    switch (id) {
    case UIPanelId::Viewport:
    case UIPanelId::Outliner:
    case UIPanelId::Inspector:
        return true;
    case UIPanelId::Console:
        return false;
    }
    return true;
}

} // namespace quader
