////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_panel_manager.hpp"

#include "diagnostics/profile.hpp"
#include "ui/ui_settings.hpp"
#include "core/release_features.hpp"

#include <QAction>
#include <QDockWidget>
#include <QEvent>
#include <QWidget>

#include <utility>

namespace quader {

void UIPanelManager::add(UIPanelId id, QDockWidget &dock_widget) {
  if (!ui_panel_available(id)) {
    dock_widget.hide();
    return;
  }
  const std::size_t slot = index(id);
  if (slot >= docks_.size()) {
    return;
  }
  docks_[slot] = &dock_widget;
  explicit_visibility_[slot] = !dock_widget.isHidden();
  presented_[slot] = dock_widget.isVisible();
  dock_widget.installEventFilter(this);
  QObject::connect(&dock_widget, &QDockWidget::visibilityChanged, this,
                   [this, id](bool visible) {
                     const std::size_t presented_slot = index(id);
                     if (presented_slot < presented_.size() &&
                         presented_[presented_slot] != visible) {
                       presented_[presented_slot] = visible;
                       if (presentation_changed_) {
                         presentation_changed_(id, visible);
                       }
                     }
                   });
  QObject::connect(dock_widget.toggleViewAction(), &QAction::triggered, this,
                   [this, id](bool visible) {
                     (void)record_explicit_visibility(id, visible);
                   });
}

void UIPanelManager::set_visibility_changed_callback(
    VisibilityChangedCallback callback) {
  visibility_changed_ = std::move(callback);
}

void UIPanelManager::set_presentation_changed_callback(
    PresentationChangedCallback callback) {
  presentation_changed_ = std::move(callback);
}

void UIPanelManager::apply_settings(const UISettings &settings) {
  for (UIPanelId id : kUiDockPanelIds) {
    (void)set_visible(id, settings.is_panel_visible(id));
  }
}

void UIPanelManager::write_settings(UISettings &settings) const {
  for (UIPanelId id : kUiDockPanelIds) {
    settings.set_panel_visible(id, is_visible(id));
  }
}

bool UIPanelManager::is_visible(UIPanelId id) const {
  const std::size_t slot = index(id);
  return id == UIPanelId::Viewport ||
         (slot < docks_.size() && docks_[slot] != nullptr &&
          explicit_visibility_[slot]);
}

bool UIPanelManager::is_presented(UIPanelId id) const {
  const std::size_t slot = index(id);
  return id == UIPanelId::Viewport ||
         (slot < docks_.size() && docks_[slot] != nullptr &&
          explicit_visibility_[slot] && presented_[slot]);
}

bool UIPanelManager::set_visible(UIPanelId id, bool visible) {
  if (id == UIPanelId::Viewport) {
    return false;
  }
  QDockWidget *panel = dock(id);
  if (panel == nullptr) {
    return false;
  }
  const bool changed = record_explicit_visibility(id, visible);
  if (panel->isHidden() == visible) {
    panel->setVisible(visible);
  }
  return changed;
}

bool UIPanelManager::focus(UIPanelId id) {
  if (id == UIPanelId::Viewport) {
    return false;
  }
  QDockWidget *panel = dock(id);
  if (panel == nullptr) {
    return false;
  }
  (void)set_visible(id, true);
  panel->raise();
  if (QWidget *content = panel->widget()) {
    content->setFocus(Qt::OtherFocusReason);
  } else {
    panel->setFocus(Qt::OtherFocusReason);
  }
  return true;
}

QDockWidget *UIPanelManager::dock(UIPanelId id) const {
  const std::size_t slot = index(id);
  return slot < docks_.size() ? docks_[slot] : nullptr;
}

bool UIPanelManager::eventFilter(QObject *watched, QEvent *event) {
  if (event != nullptr && event->type() == QEvent::Close) {
    QDR_PROFILE_SCOPE("Qt dock close event dispatch");
    for (UIPanelId id : kUiDockPanelIds) {
      if (dock(id) == watched) {
        (void)record_explicit_visibility(id, false);
        break;
      }
    }
  }
  return QObject::eventFilter(watched, event);
}

bool UIPanelManager::record_explicit_visibility(UIPanelId id, bool visible) {
  const std::size_t slot = index(id);
  if (id == UIPanelId::Viewport || slot >= docks_.size() ||
      docks_[slot] == nullptr) {
    return false;
  }
  const UIPanelVisibilityTransition transition =
      ui_panel_visibility_transition(explicit_visibility_[slot], visible);
  explicit_visibility_[slot] = transition.visible;
  if (transition.changed && visibility_changed_) {
    visibility_changed_();
  }
  return transition.changed;
}

} // namespace quader
