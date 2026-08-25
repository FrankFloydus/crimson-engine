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

  // Near-black surfaces carry a restrained violet warmth. Each level remains
  // visibly distinct so dense editor panels do not collapse into flat black.
  palette.setColor(kAllGroups, QPalette::Window, QColor(24, 23, 28));
  palette.setColor(kAllGroups, QPalette::Base, QColor(17, 16, 20));
  palette.setColor(kAllGroups, QPalette::AlternateBase, QColor(32, 30, 37));
  palette.setColor(kAllGroups, QPalette::Button, QColor(41, 38, 47));
  palette.setColor(kAllGroups, QPalette::ToolTipBase, QColor(45, 41, 52));

  palette.setColor(kAllGroups, QPalette::WindowText, QColor(232, 227, 238));
  palette.setColor(kAllGroups, QPalette::Text, QColor(227, 222, 233));
  palette.setColor(kAllGroups, QPalette::ButtonText, QColor(232, 227, 238));
  palette.setColor(kAllGroups, QPalette::BrightText, QColor(255, 255, 255));
  palette.setColor(kAllGroups, QPalette::ToolTipText, QColor(246, 240, 250));
  palette.setColor(kAllGroups, QPalette::PlaceholderText,
                   QColor(153, 146, 162));

  palette.setColor(kAllGroups, QPalette::Light, QColor(69, 64, 77));
  palette.setColor(kAllGroups, QPalette::Midlight, QColor(60, 55, 68));
  palette.setColor(kAllGroups, QPalette::Mid, QColor(53, 49, 61));
  palette.setColor(kAllGroups, QPalette::Dark, QColor(14, 13, 17));
  palette.setColor(kAllGroups, QPalette::Shadow, QColor(8, 7, 10));

  // A clear amethyst is the sole interactive accent. Its darker Fusion-derived
  // outlines remain distinguishable from controls, while dark selected text
  // exceeds the WCAG AA contrast threshold.
  palette.setColor(kAllGroups, QPalette::Highlight, QColor(173, 128, 244));
  palette.setColor(kAllGroups, QPalette::HighlightedText,
                   QColor(17, 16, 20));
  palette.setColor(kAllGroups, QPalette::Accent, QColor(173, 128, 244));
  palette.setColor(kAllGroups, QPalette::Link, QColor(182, 156, 255));
  palette.setColor(kAllGroups, QPalette::LinkVisited, QColor(208, 162, 241));
}

void apply_inactive_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kGroup = QPalette::Inactive;
  palette.setColor(kGroup, QPalette::WindowText, QColor(211, 205, 217));
  palette.setColor(kGroup, QPalette::Text, QColor(203, 196, 210));
  palette.setColor(kGroup, QPalette::ButtonText, QColor(211, 205, 217));
  palette.setColor(kGroup, QPalette::PlaceholderText, QColor(141, 134, 149));
  palette.setColor(kGroup, QPalette::Button, QColor(37, 34, 43));
  palette.setColor(kGroup, QPalette::Highlight, QColor(160, 131, 211));
  palette.setColor(kGroup, QPalette::HighlightedText, QColor(23, 21, 26));
  palette.setColor(kGroup, QPalette::Accent, QColor(160, 131, 211));
  palette.setColor(kGroup, QPalette::Link, QColor(161, 138, 222));
  palette.setColor(kGroup, QPalette::LinkVisited, QColor(182, 141, 208));
}

void apply_disabled_obsidian_roles(QPalette &palette) {
  constexpr QPalette::ColorGroup kGroup = QPalette::Disabled;
  palette.setColor(kGroup, QPalette::Base, QColor(21, 20, 25));
  palette.setColor(kGroup, QPalette::AlternateBase, QColor(29, 27, 33));
  palette.setColor(kGroup, QPalette::Button, QColor(23, 21, 26));

  palette.setColor(kGroup, QPalette::WindowText, QColor(141, 134, 149));
  palette.setColor(kGroup, QPalette::Text, QColor(141, 134, 149));
  palette.setColor(kGroup, QPalette::ButtonText, QColor(141, 134, 149));
  palette.setColor(kGroup, QPalette::BrightText, QColor(179, 173, 185));
  palette.setColor(kGroup, QPalette::PlaceholderText, QColor(116, 110, 122));

  palette.setColor(kGroup, QPalette::Light, QColor(49, 46, 55));
  palette.setColor(kGroup, QPalette::Midlight, QColor(43, 41, 48));
  palette.setColor(kGroup, QPalette::Mid, QColor(40, 37, 45));
  palette.setColor(kGroup, QPalette::Highlight, QColor(144, 124, 158));
  palette.setColor(kGroup, QPalette::HighlightedText, QColor(23, 21, 26));
  palette.setColor(kGroup, QPalette::Accent, QColor(144, 124, 158));
  palette.setColor(kGroup, QPalette::Link, QColor(141, 121, 172));
  palette.setColor(kGroup, QPalette::LinkVisited, QColor(144, 114, 158));
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
