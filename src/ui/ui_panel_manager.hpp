////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/ui_panel.hpp"

#include <QObject>

#include <array>
#include <cstddef>
#include <functional>

class QDockWidget;
class QEvent;

namespace quader {

class UISettings;

/// Reports the requested panel visibility and whether it differs from current state.
struct UIPanelVisibilityTransition {
  bool visible = false;
  bool changed = false;
};

[[nodiscard]] constexpr UIPanelVisibilityTransition
ui_panel_visibility_transition(bool current, bool requested) noexcept {
  return {.visible = requested, .changed = current != requested};
}

/**
 * Owns the stable mapping between public panel identifiers and QDockWidgets.
 */
class UIPanelManager final : public QObject {
public:
  using VisibilityChangedCallback = std::function<void()>;

  UIPanelManager() = default;

  void add(UIPanelId id, QDockWidget &dock);
  void set_visibility_changed_callback(VisibilityChangedCallback callback);
  void apply_settings(const UISettings &settings);
  void write_settings(UISettings &settings) const;

  [[nodiscard]] bool is_visible(UIPanelId id) const;
  bool set_visible(UIPanelId id, bool visible);
  bool focus(UIPanelId id);
  [[nodiscard]] QDockWidget *dock(UIPanelId id) const;

private:
  static constexpr std::size_t index(UIPanelId id) {
    return static_cast<std::size_t>(id);
  }

  bool eventFilter(QObject *watched, QEvent *event) override;
  bool record_explicit_visibility(UIPanelId id, bool visible);

  std::array<QDockWidget *, 5> docks_{};
  std::array<bool, 5> explicit_visibility_{
      ui_panel_default_visible(UIPanelId::Viewport),
      ui_panel_default_visible(UIPanelId::Outliner),
      ui_panel_default_visible(UIPanelId::Inspector),
      ui_panel_default_visible(UIPanelId::Console),
      ui_panel_default_visible(UIPanelId::Layers),
  };
  VisibilityChangedCallback visibility_changed_;
};

} // namespace quader
