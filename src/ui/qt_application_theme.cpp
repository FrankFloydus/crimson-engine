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
#include <QFont>
#include <QGuiApplication>
#include <QStyle>
#include <QStyleHints>

#include <algorithm>

namespace quader {

QColor qt_current_item_outline_color() {
  // The orange authored in the face-selection toolbar icon.
  return QColor(255, 143, 24);
}

namespace {

void apply_shared_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kAllGroups = QPalette::All;

  // Quiet neutral surfaces separate content, panel chrome and editable fields
  // without the bright bevels of a traditional desktop palette.
  palette.setColor(kAllGroups, QPalette::Window, QColor(17, 18, 19));
  palette.setColor(kAllGroups, QPalette::Base, QColor(23, 24, 25));
  palette.setColor(kAllGroups, QPalette::AlternateBase, QColor(28, 29, 31));
  palette.setColor(kAllGroups, QPalette::Button, QColor(36, 37, 40));
  palette.setColor(kAllGroups, QPalette::ToolTipBase, QColor(41, 43, 47));

  palette.setColor(kAllGroups, QPalette::WindowText, QColor(173, 176, 182));
  palette.setColor(kAllGroups, QPalette::Text, QColor(225, 226, 229));
  palette.setColor(kAllGroups, QPalette::ButtonText, QColor(218, 220, 225));
  palette.setColor(kAllGroups, QPalette::BrightText, QColor(255, 255, 255));
  palette.setColor(kAllGroups, QPalette::ToolTipText, QColor(238, 240, 246));
  palette.setColor(kAllGroups, QPalette::PlaceholderText,
                   QColor(133, 137, 145));

  // Fusion resolves menu separators, frames, splitter handles, grid lines,
  // toolbar separators, and Quader's custom panel edges through this ladder.
  // Reserve stronger edges for interaction; resting chrome stays subdued.
  palette.setColor(kAllGroups, QPalette::Light, QColor(65, 66, 70));
  palette.setColor(kAllGroups, QPalette::Midlight, QColor(54, 55, 59));
  palette.setColor(kAllGroups, QPalette::Mid, QColor(44, 45, 48));
  palette.setColor(kAllGroups, QPalette::Dark, QColor(37, 38, 41));
  palette.setColor(kAllGroups, QPalette::Shadow, QColor(8, 9, 10));

  // Quader's logo supplies the purple accent (#9a64d8) and pale companion
  // (#d6bdf3). Selection uses a restrained tint rather than a solid brand fill.
  palette.setColor(kAllGroups, QPalette::Highlight, QColor(48, 39, 56));
  palette.setColor(kAllGroups, QPalette::HighlightedText,
                   QColor(236, 229, 245));
  palette.setColor(kAllGroups, QPalette::Accent, QColor(154, 100, 216));
  palette.setColor(kAllGroups, QPalette::Link, QColor(214, 189, 243));
  palette.setColor(kAllGroups, QPalette::LinkVisited, QColor(190, 165, 216));
}

void apply_inactive_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kGroup = QPalette::Inactive;
  palette.setColor(kGroup, QPalette::WindowText, QColor(164, 167, 174));
  palette.setColor(kGroup, QPalette::Text, QColor(212, 214, 220));
  palette.setColor(kGroup, QPalette::ButtonText, QColor(202, 205, 212));
  palette.setColor(kGroup, QPalette::PlaceholderText, QColor(125, 129, 138));
  palette.setColor(kGroup, QPalette::Button, QColor(34, 35, 38));
  palette.setColor(kGroup, QPalette::Highlight, QColor(42, 36, 49));
  palette.setColor(kGroup, QPalette::HighlightedText, QColor(214, 211, 220));
  palette.setColor(kGroup, QPalette::Accent, QColor(160, 132, 194));
  palette.setColor(kGroup, QPalette::Link, QColor(194, 174, 219));
  palette.setColor(kGroup, QPalette::LinkVisited, QColor(180, 157, 198));
}

void apply_disabled_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kGroup = QPalette::Disabled;
  palette.setColor(kGroup, QPalette::Base, QColor(23, 24, 25));
  palette.setColor(kGroup, QPalette::AlternateBase, QColor(26, 27, 28));
  palette.setColor(kGroup, QPalette::Button, QColor(27, 28, 30));

  palette.setColor(kGroup, QPalette::WindowText, QColor(132, 135, 144));
  palette.setColor(kGroup, QPalette::Text, QColor(132, 135, 144));
  palette.setColor(kGroup, QPalette::ButtonText, QColor(132, 135, 144));
  palette.setColor(kGroup, QPalette::BrightText, QColor(179, 182, 192));
  palette.setColor(kGroup, QPalette::PlaceholderText, QColor(120, 124, 132));

  palette.setColor(kGroup, QPalette::Light, QColor(53, 55, 59));
  palette.setColor(kGroup, QPalette::Midlight, QColor(44, 45, 48));
  palette.setColor(kGroup, QPalette::Mid, QColor(38, 39, 42));
  palette.setColor(kGroup, QPalette::Highlight, QColor(34, 31, 39));
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
  // Establish the readable design font before the scale controller captures
  // its immutable baseline. Live contrast transitions never reset typography.
  QFont interface_font = QApplication::font();
  if (interface_font.pointSizeF() > 0.0) {
    interface_font.setPointSizeF(std::max(10.0, interface_font.pointSizeF()));
    QApplication::setFont(interface_font);
  }
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
