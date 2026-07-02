////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_panel_manager.hpp"

#include "diagnostics/profile.hpp"
#include "ui/ui_context.hpp"

namespace quader {

void UIPanelManager::add(std::unique_ptr<UIPanel> panel) {
    if (!panel) {
        return;
    }
    visibility_.push_back(panel->default_visible());
    panels_.push_back(std::move(panel));
}

void UIPanelManager::apply_settings(const UISettings &settings) {
  for (std::size_t index = 0; index < panels_.size(); ++index) {
    const UIPanel &panel = *panels_[index];
    visibility_[index] =
        panel.can_toggle() ? settings.is_panel_visible(panel.id()) : true;
  }
}

void UIPanelManager::write_settings(UISettings &settings) const {
  for (std::size_t index = 0; index < panels_.size(); ++index) {
    const UIPanel &panel = *panels_[index];
    if (panel.can_toggle()) {
      settings.set_panel_visible(panel.id(), visibility_[index]);
    }
  }
}

bool UIPanelManager::is_visible(UIPanelId id) const {
  for (std::size_t index = 0; index < panels_.size(); ++index) {
    if (panels_[index]->id() == id) {
      return panels_[index]->can_toggle() ? visibility_[index] : true;
    }
  }
  return default_visibility(id);
}

void UIPanelManager::set_visible(UIPanelId id, bool visible) {
  for (std::size_t index = 0; index < panels_.size(); ++index) {
    if (panels_[index]->id() == id && panels_[index]->can_toggle()) {
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
        if (!panel.can_toggle() || visibility_[index]) {
          switch (panel.id()) {
          case UIPanelId::Viewport: {
            QDR_PROFILE_SCOPE("UI draw panel viewport");
            panel.draw(context);
            break;
          }
          case UIPanelId::Outliner: {
            QDR_PROFILE_SCOPE("UI draw panel outliner");
            panel.draw(context);
            break;
          }
          case UIPanelId::Inspector: {
            QDR_PROFILE_SCOPE("UI draw panel inspector");
            panel.draw(context);
            break;
          }
          case UIPanelId::Console: {
            QDR_PROFILE_SCOPE("UI draw panel console");
            panel.draw(context);
            break;
          }
          }
        }
    }
}

bool UIPanelManager::default_visibility(UIPanelId id) const {
  return ui_panel_default_visible(id);
}

} // namespace quader
