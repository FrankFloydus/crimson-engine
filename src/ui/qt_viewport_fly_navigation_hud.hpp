////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"

#include <QObject>
#include <QRect>

#include <string>

class QLabel;
class QWidget;

namespace quader {

class LocalizationService;

/// Carries the visibility and localized speed text presented by the Qt fly-navigation HUD.
struct QtViewportFlyNavigationHudPresentation {
  bool crosshair_visible = false;
  bool speed_visible = false;
  std::string speed_label;
};

/**
 * Reproduces the legacy fly HUD visibility contract without owning navigation
 * state: the crosshair follows the router HUD flag, while the speed/reset hint
 * appears only after the camera speed differs from its default.
 */
[[nodiscard]] QtViewportFlyNavigationHudPresentation
qt_viewport_fly_navigation_hud_presentation(
    bool fly_hud_active, bool fly_speed_is_default, float fly_speed,
    const LocalizationService *localization);

/**
 * Owns the stock-palette Qt widgets layered over one native viewport pane.
 * Navigation truth remains in ViewportInputRouter and the pane camera.
 */
class QtViewportFlyNavigationHud final : public QObject {
public:
  QtViewportFlyNavigationHud(QWidget &parent, ViewportPaneId pane_id);

  QtViewportFlyNavigationHud(const QtViewportFlyNavigationHud &) = delete;
  QtViewportFlyNavigationHud &
  operator=(const QtViewportFlyNavigationHud &) = delete;

  void set_bounds(const QRect &bounds);
  void present(const QtViewportFlyNavigationHudPresentation &presentation);
  void hide();

private:
  QWidget *crosshair_ = nullptr;
  QLabel *speed_label_ = nullptr;
  QRect bounds_;
};

} // namespace quader
