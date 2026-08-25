////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_dock_title_presentation.hpp"

#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QGuiApplication>

#include <algorithm>
#include <array>
#include <cmath>

namespace quader {
namespace {

constexpr QRgb kDarkDockGlyphRgb = qRgb(16, 16, 16);
constexpr QRgb kLightDockGlyphRgb = qRgb(242, 242, 242);

double relative_luminance(const QColor &color) {
  const auto linear_channel = [](qreal encoded) {
    return encoded <= 0.04045
               ? encoded / 12.92
               : std::pow((encoded + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear_channel(color.redF()) +
         0.7152 * linear_channel(color.greenF()) +
         0.0722 * linear_channel(color.blueF());
}

double contrast_ratio(const QColor &first, const QColor &second) {
  const double first_luminance = relative_luminance(first);
  const double second_luminance = relative_luminance(second);
  return (std::max(first_luminance, second_luminance) + 0.05) /
         (std::min(first_luminance, second_luminance) + 0.05);
}

QString dock_palette_fingerprint(const QPalette &palette) {
  constexpr std::array<QPalette::ColorGroup, 3> kGroups{
      QPalette::Active, QPalette::Inactive, QPalette::Disabled};
  constexpr std::array<QPalette::ColorRole, 6> kRoles{
      QPalette::AlternateBase, QPalette::Button,     QPalette::Text,
      QPalette::ButtonText,    QPalette::Mid,        QPalette::Highlight};

  QString fingerprint = QStringLiteral("/* quader-dock-palette");
  for (const QPalette::ColorGroup group : kGroups) {
    for (const QPalette::ColorRole role : kRoles) {
      fingerprint += QStringLiteral("-%1").arg(
          palette.color(group, role).rgba(), 8, 16, QLatin1Char('0'));
    }
  }
  fingerprint += QStringLiteral(" */\n");
  return fingerprint;
}

void apply_palette_aware_dock_title_presentation(
    QDockWidget &dock, const QPalette &application_palette) {
  const QString style_sheet =
      qt_dock_title_style_sheet(application_palette);
  if (dock.styleSheet() != style_sheet) {
    dock.setStyleSheet(style_sheet);
  }
}

} // namespace

bool qt_dock_title_surface_uses_packaged_light_glyphs(
    const QColor &surface) {
  return contrast_ratio(QColor::fromRgb(kLightDockGlyphRgb), surface) >
         contrast_ratio(QColor::fromRgb(kDarkDockGlyphRgb), surface);
}

QString qt_dock_title_style_sheet(const QPalette &palette) {
  const QColor inactive_surface =
      palette.color(QPalette::Inactive, QPalette::AlternateBase);
  const QColor active_surface =
      palette.color(QPalette::Active, QPalette::Button);
  const QColor light_glyph = QColor::fromRgb(kLightDockGlyphRgb);
  const QColor dark_glyph = QColor::fromRgb(kDarkDockGlyphRgb);
  const double light_contrast =
      std::min(contrast_ratio(light_glyph, inactive_surface),
               contrast_ratio(light_glyph, active_surface));
  const double dark_contrast =
      std::min(contrast_ratio(dark_glyph, inactive_surface),
               contrast_ratio(dark_glyph, active_surface));
  const bool use_light_glyphs = light_contrast > dark_contrast;
  const bool needs_contrast_tile =
      std::max(light_contrast, dark_contrast) < 3.0;
  const QString close_icon =
      use_light_glyphs
          ? QStringLiteral(":/quader/icons/dock_close.png")
          : QStringLiteral(":/quader/icons/dock_close_dark.png");
  const QString float_icon =
      use_light_glyphs
          ? QStringLiteral(":/quader/icons/dock_float.png")
          : QStringLiteral(":/quader/icons/dock_float_dark.png");
  const QString button_background =
      needs_contrast_tile
          ? (use_light_glyphs ? QStringLiteral("#101010")
                              : QStringLiteral("#f2f2f2"))
          : QStringLiteral("transparent");
  const auto qss_color = [](const QColor &color) {
    return color.name(QColor::HexRgb);
  };
  const QString inactive_title_background = qss_color(inactive_surface);
  const QString inactive_title_text =
      qss_color(palette.color(QPalette::Inactive, QPalette::Text));
  const QString inactive_title_border =
      qss_color(palette.color(QPalette::Inactive, QPalette::Mid));
  const QString active_title_background = qss_color(active_surface);
  const QString active_title_text =
      qss_color(palette.color(QPalette::Active, QPalette::ButtonText));
  const QString active_title_border =
      qss_color(palette.color(QPalette::Active, QPalette::Mid));
  const QString highlight =
      qss_color(palette.color(QPalette::Active, QPalette::Highlight));
  const QString pressed_background =
      needs_contrast_tile ? button_background : active_title_background;

  return dock_palette_fingerprint(palette) + QStringLiteral(R"QUADER(
QDockWidget {
  titlebar-close-icon: url(%1);
  titlebar-normal-icon: url(%2);
}
QDockWidget::title {
  background-color: %3;
  color: %4;
  border-bottom: 1px solid %5;
  padding: 0 4px;
  text-align: left;
}
QDockWidget::title:active {
  background-color: %6;
  color: %7;
  border-bottom: 1px solid %8;
}
QDockWidget::close-button,
QDockWidget::float-button {
  background-color: %9;
  border: 1px solid transparent;
  padding: 0;
  width: 18px;
  height: 18px;
  icon-size: 24px;
}
QDockWidget::close-button:hover,
QDockWidget::float-button:hover {
  background-color: %9;
  border-color: %10;
}
QDockWidget::close-button:pressed,
QDockWidget::float-button:pressed {
  background-color: %11;
  border: 2px solid %10;
}
)QUADER")
      .arg(close_icon)
      .arg(float_icon)
      .arg(inactive_title_background)
      .arg(inactive_title_text)
      .arg(inactive_title_border)
      .arg(active_title_background)
      .arg(active_title_text)
      .arg(active_title_border)
      .arg(button_background)
      .arg(highlight)
      .arg(pressed_background);
}

void install_palette_aware_dock_title_presentation(QDockWidget &dock) {
  QObject::connect(qApp, &QGuiApplication::paletteChanged, &dock,
                   [&dock](const QPalette &palette) {
                     apply_palette_aware_dock_title_presentation(dock,
                                                                  palette);
                   });
  apply_palette_aware_dock_title_presentation(dock,
                                               QApplication::palette());
}

} // namespace quader
