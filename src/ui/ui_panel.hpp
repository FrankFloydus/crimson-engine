////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

class UIContext;

/**
 * Enumerates UIPanelId values used by the ui layer.
 */
enum class UIPanelId {
    Viewport,
    Outliner,
    Inspector,
    Console,
};

inline const char *ui_panel_key(UIPanelId id) {
  switch (id) {
  case UIPanelId::Viewport:
    return "Viewport";
  case UIPanelId::Outliner:
    return "Outliner";
  case UIPanelId::Inspector:
    return "Inspector";
  case UIPanelId::Console:
    return "Console";
  }
  return "Unknown";
}

inline const char *ui_panel_stable_id(UIPanelId id) {
  switch (id) {
  case UIPanelId::Viewport:
    return "viewport";
  case UIPanelId::Outliner:
    return "outliner";
  case UIPanelId::Inspector:
    return "inspector";
  case UIPanelId::Console:
    return "console";
  }
  return "unknown";
}

inline bool ui_panel_default_visible(UIPanelId id) {
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

inline const char *ui_panel_localization_key(UIPanelId id) {
  switch (id) {
  case UIPanelId::Viewport:
    return "ui.panel.viewport";
  case UIPanelId::Outliner:
    return "ui.panel.outliner";
  case UIPanelId::Inspector:
    return "ui.panel.inspector";
  case UIPanelId::Console:
    return "ui.panel.console";
  }
  return "ui.panel.unknown";
}

/**
 * Owns UI Panel behavior within the Qt editor presentation layer.
 */
class UIPanel {
public:
    virtual ~UIPanel() = default;

    virtual UIPanelId id() const = 0;
    virtual const char* title() const = 0;
    virtual bool default_visible() const {
      return ui_panel_default_visible(id());
    }
    virtual bool can_toggle() const { return true; }
    virtual void draw(UIContext& context) = 0;
};

} // namespace quader
