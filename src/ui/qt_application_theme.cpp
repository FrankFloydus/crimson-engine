////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_application_theme.hpp"

#include <QAccessibilityHints>
#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QStyle>
#include <QStyleHints>

namespace quader {
namespace {

void apply_shared_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kAllGroups = QPalette::All;

  // Charcoal surfaces keep dense content quiet. Alternating rows differ by only
  // three channel values; panels and controls carry the stronger separation.
  palette.setColor(kAllGroups, QPalette::Window, QColor(31, 32, 37));
  palette.setColor(kAllGroups, QPalette::Base, QColor(24, 25, 29));
  palette.setColor(kAllGroups, QPalette::AlternateBase, QColor(27, 28, 32));
  palette.setColor(kAllGroups, QPalette::Button, QColor(43, 44, 51));
  palette.setColor(kAllGroups, QPalette::ToolTipBase, QColor(47, 48, 56));

  palette.setColor(kAllGroups, QPalette::WindowText, QColor(228, 230, 237));
  palette.setColor(kAllGroups, QPalette::Text, QColor(218, 221, 229));
  palette.setColor(kAllGroups, QPalette::ButtonText, QColor(228, 230, 237));
  palette.setColor(kAllGroups, QPalette::BrightText, QColor(255, 255, 255));
  palette.setColor(kAllGroups, QPalette::ToolTipText, QColor(238, 240, 246));
  palette.setColor(kAllGroups, QPalette::PlaceholderText,
                   QColor(148, 152, 164));

  palette.setColor(kAllGroups, QPalette::Light, QColor(57, 59, 67));
  palette.setColor(kAllGroups, QPalette::Midlight, QColor(50, 52, 60));
  palette.setColor(kAllGroups, QPalette::Mid, QColor(35, 36, 42));
  palette.setColor(kAllGroups, QPalette::Dark, QColor(22, 23, 27));
  palette.setColor(kAllGroups, QPalette::Shadow, QColor(12, 13, 16));

  // Neutral charcoal keeps selection quiet across standard Qt views. Amethyst
  // remains a distinct focus and interaction accent instead of tinting rows.
  palette.setColor(kAllGroups, QPalette::Highlight, QColor(66, 69, 76));
  palette.setColor(kAllGroups, QPalette::HighlightedText,
                   QColor(228, 230, 237));
  palette.setColor(kAllGroups, QPalette::Accent, QColor(167, 148, 221));
  palette.setColor(kAllGroups, QPalette::Link, QColor(185, 171, 231));
  palette.setColor(kAllGroups, QPalette::LinkVisited, QColor(192, 167, 210));
}

void apply_inactive_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kGroup = QPalette::Inactive;
  palette.setColor(kGroup, QPalette::WindowText, QColor(207, 211, 220));
  palette.setColor(kGroup, QPalette::Text, QColor(201, 205, 215));
  palette.setColor(kGroup, QPalette::ButtonText, QColor(207, 211, 220));
  palette.setColor(kGroup, QPalette::PlaceholderText, QColor(143, 147, 158));
  palette.setColor(kGroup, QPalette::Button, QColor(40, 41, 48));
  palette.setColor(kGroup, QPalette::Highlight, QColor(57, 59, 66));
  palette.setColor(kGroup, QPalette::HighlightedText, QColor(207, 211, 220));
  palette.setColor(kGroup, QPalette::Accent, QColor(153, 141, 191));
  palette.setColor(kGroup, QPalette::Link, QColor(172, 159, 214));
  palette.setColor(kGroup, QPalette::LinkVisited, QColor(180, 157, 198));
}

void apply_disabled_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kGroup = QPalette::Disabled;
  palette.setColor(kGroup, QPalette::Base, QColor(27, 28, 32));
  palette.setColor(kGroup, QPalette::AlternateBase, QColor(30, 31, 35));
  palette.setColor(kGroup, QPalette::Button, QColor(29, 30, 35));

  palette.setColor(kGroup, QPalette::WindowText, QColor(144, 147, 158));
  palette.setColor(kGroup, QPalette::Text, QColor(144, 147, 158));
  palette.setColor(kGroup, QPalette::ButtonText, QColor(144, 147, 158));
  palette.setColor(kGroup, QPalette::BrightText, QColor(179, 182, 192));
  palette.setColor(kGroup, QPalette::PlaceholderText, QColor(121, 125, 137));

  palette.setColor(kGroup, QPalette::Light, QColor(43, 45, 52));
  palette.setColor(kGroup, QPalette::Midlight, QColor(37, 39, 46));
  palette.setColor(kGroup, QPalette::Mid, QColor(26, 27, 32));
  palette.setColor(kGroup, QPalette::Highlight, QColor(48, 49, 56));
  palette.setColor(kGroup, QPalette::HighlightedText, QColor(150, 153, 164));
  palette.setColor(kGroup, QPalette::Accent, QColor(139, 132, 160));
  palette.setColor(kGroup, QPalette::Link, QColor(151, 143, 172));
  palette.setColor(kGroup, QPalette::LinkVisited, QColor(159, 140, 172));
}

void apply_application_palette_for_contrast(
    QStyleHints &style_hints, const QPalette &fusion_palette,
    Qt::ContrastPreference preference) {
  QPalette effective_fusion_palette = fusion_palette;
  if (preference == Qt::ContrastPreference::HighContrast) {
    // Let Qt and Windows negotiate native chrome and keep resolving the live
    // platform palette instead of overriding the user's request.
    style_hints.unsetColorScheme();
  } else {
    style_hints.setColorScheme(Qt::ColorScheme::Dark);
    // Query only after requesting dark chrome; a runtime transition out of
    // High Contrast must not reuse the preceding system scheme's baseline.
    if (QStyle *style = QApplication::style()) {
      effective_fusion_palette = style->standardPalette();
    }
  }
  QApplication::setPalette(qt_application_palette_for_contrast_preference(
      effective_fusion_palette, preference));
}

} // namespace

QPalette qt_obsidian_application_palette(const QPalette &fusion_palette) {
  QPalette palette = fusion_palette;
  apply_shared_obsidian_roles(palette);
  apply_inactive_obsidian_roles(palette);
  apply_disabled_obsidian_roles(palette);
  return palette;
}

QPalette qt_application_palette_for_contrast_preference(
    const QPalette &fusion_palette, Qt::ContrastPreference preference) {
  if (preference == Qt::ContrastPreference::HighContrast) {
    // An empty resolve mask clears Qt's explicit-application-palette flag. The
    // final colors then keep resolving against Windows' live system palette.
    return {};
  }
  return qt_obsidian_application_palette(fusion_palette);
}

void apply_qt_application_theme_for_contrast_preference(
    const QPalette &fusion_palette, Qt::ContrastPreference preference) {
  QStyleHints *style_hints = QGuiApplication::styleHints();
  if (style_hints != nullptr) {
    apply_application_palette_for_contrast(*style_hints, fusion_palette,
                                           preference);
  }
}

void apply_qt_obsidian_application_theme(const QPalette &fusion_palette) {
  QStyleHints *style_hints = QGuiApplication::styleHints();
  if (style_hints == nullptr) {
    return;
  }

  const QAccessibilityHints *accessibility = style_hints->accessibility();
  const Qt::ContrastPreference initial_preference =
      accessibility != nullptr ? accessibility->contrastPreference()
                               : Qt::ContrastPreference::NoPreference;
  apply_qt_application_theme_for_contrast_preference(fusion_palette,
                                                     initial_preference);

  if (accessibility != nullptr) {
    QObject::connect(
        accessibility, &QAccessibilityHints::contrastPreferenceChanged, qApp,
        [](Qt::ContrastPreference preference) {
          apply_qt_application_theme_for_contrast_preference(QPalette{},
                                                             preference);
        });
  }
}

} // namespace quader
