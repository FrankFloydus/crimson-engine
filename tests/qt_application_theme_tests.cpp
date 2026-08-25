////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_application_theme.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_semantic_axis_colors.hpp"

#include <QAccessibilityHints>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QImage>
#include <QMainWindow>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace {

QColor rgb(std::uint32_t value) {
  return {static_cast<int>((value >> 16u) & 0xffu),
          static_cast<int>((value >> 8u) & 0xffu),
          static_cast<int>(value & 0xffu)};
}

double linear_channel(int channel) {
  const double encoded = static_cast<double>(channel) / 255.0;
  if (encoded <= 0.04045) {
    return encoded / 12.92;
  }
  return std::pow((encoded + 0.055) / 1.055, 2.4);
}

double relative_luminance(const QColor &color) {
  return 0.2126 * linear_channel(color.red()) +
         0.7152 * linear_channel(color.green()) +
         0.0722 * linear_channel(color.blue());
}

double contrast_ratio(const QColor &first, const QColor &second) {
  const double first_luminance = relative_luminance(first);
  const double second_luminance = relative_luminance(second);
  return (std::max(first_luminance, second_luminance) + 0.05) /
         (std::min(first_luminance, second_luminance) + 0.05);
}

void expect_color(const QPalette &palette, QPalette::ColorGroup group,
                  QPalette::ColorRole role, std::uint32_t expected) {
  EXPECT_EQ(palette.color(group, role), rgb(expected));
}

void expect_contrast(const QPalette &palette, QPalette::ColorGroup group,
                     QPalette::ColorRole foreground,
                     QPalette::ColorRole background, double minimum) {
  EXPECT_GE(contrast_ratio(palette.color(group, foreground),
                           palette.color(group, background)),
            minimum);
}

QPalette dock_surface_palette(const QColor &surface,
                              const QColor &highlight) {
  QPalette palette;
  for (const QPalette::ColorGroup group :
       {QPalette::Active, QPalette::Inactive}) {
    palette.setColor(group, QPalette::AlternateBase, surface);
    palette.setColor(group, QPalette::Button, surface);
    palette.setColor(group, QPalette::Highlight, highlight);
  }
  return palette;
}

void expect_selected_dock_glyph_contrast(const QColor &surface) {
  const QColor selected_glyph =
      quader::qt_dock_title_surface_uses_packaged_light_glyphs(surface)
          ? QColor(242, 242, 242)
          : QColor(16, 16, 16);
  EXPECT_GE(contrast_ratio(selected_glyph, surface), 3.0);
}

} // namespace

TEST(QtApplicationThemeTests,
     DefinesLayeredSurfacesTextAndAmethystInteractionRoles) {
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});

  expect_color(palette, QPalette::Active, QPalette::Window, 0x18171c);
  expect_color(palette, QPalette::Active, QPalette::Base, 0x111014);
  expect_color(palette, QPalette::Active, QPalette::AlternateBase, 0x201e25);
  expect_color(palette, QPalette::Active, QPalette::Button, 0x29262f);
  expect_color(palette, QPalette::Active, QPalette::ToolTipBase, 0x2d2934);
  expect_color(palette, QPalette::Active, QPalette::WindowText, 0xe8e3ee);
  expect_color(palette, QPalette::Active, QPalette::Text, 0xe3dee9);
  expect_color(palette, QPalette::Active, QPalette::ButtonText, 0xe8e3ee);
  expect_color(palette, QPalette::Active, QPalette::BrightText, 0xffffff);
  expect_color(palette, QPalette::Active, QPalette::PlaceholderText, 0x9992a2);
  expect_color(palette, QPalette::Active, QPalette::Light, 0x45404d);
  expect_color(palette, QPalette::Active, QPalette::Midlight, 0x3c3744);
  expect_color(palette, QPalette::Active, QPalette::Mid, 0x35313d);
  expect_color(palette, QPalette::Active, QPalette::Dark, 0x0e0d11);
  expect_color(palette, QPalette::Active, QPalette::Shadow, 0x08070a);
  expect_color(palette, QPalette::Active, QPalette::Highlight, 0xad80f4);
  expect_color(palette, QPalette::Active, QPalette::HighlightedText, 0x111014);
  expect_color(palette, QPalette::Active, QPalette::Accent, 0xad80f4);
  expect_color(palette, QPalette::Active, QPalette::Link, 0xb69cff);
  expect_color(palette, QPalette::Active, QPalette::LinkVisited, 0xd0a2f1);

  expect_color(palette, QPalette::Inactive, QPalette::WindowText, 0xd3cdd9);
  expect_color(palette, QPalette::Inactive, QPalette::Text, 0xcbc4d2);
  expect_color(palette, QPalette::Inactive, QPalette::ButtonText, 0xd3cdd9);
  expect_color(palette, QPalette::Inactive, QPalette::Button, 0x25222b);
  expect_color(palette, QPalette::Inactive, QPalette::Highlight, 0xa083d3);
  expect_color(palette, QPalette::Inactive, QPalette::HighlightedText,
               0x17151a);
  expect_color(palette, QPalette::Inactive, QPalette::Accent, 0xa083d3);
  expect_color(palette, QPalette::Inactive, QPalette::Link, 0xa18ade);

  expect_color(palette, QPalette::Disabled, QPalette::Base, 0x151419);
  expect_color(palette, QPalette::Disabled, QPalette::AlternateBase, 0x1d1b21);
  expect_color(palette, QPalette::Disabled, QPalette::Button, 0x17151a);
  expect_color(palette, QPalette::Disabled, QPalette::WindowText, 0x8d8695);
  expect_color(palette, QPalette::Disabled, QPalette::Text, 0x8d8695);
  expect_color(palette, QPalette::Disabled, QPalette::ButtonText, 0x8d8695);
  expect_color(palette, QPalette::Disabled, QPalette::PlaceholderText,
               0x746e7a);
  expect_color(palette, QPalette::Disabled, QPalette::Highlight, 0x907c9e);
  expect_color(palette, QPalette::Disabled, QPalette::HighlightedText,
               0x17151a);
  expect_color(palette, QPalette::Disabled, QPalette::Accent, 0x907c9e);
  expect_color(palette, QPalette::Disabled, QPalette::Link, 0x8d79ac);
}

TEST(QtApplicationThemeTests,
     KeepsPrimaryInactiveAndDisabledContentReadable) {
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});

  for (const QPalette::ColorGroup group :
       {QPalette::Active, QPalette::Inactive}) {
    expect_contrast(palette, group, QPalette::WindowText, QPalette::Window,
                    4.5);
    expect_contrast(palette, group, QPalette::Text, QPalette::Base, 4.5);
    expect_contrast(palette, group, QPalette::ButtonText, QPalette::Button,
                    4.5);
    expect_contrast(palette, group, QPalette::ToolTipText,
                    QPalette::ToolTipBase, 4.5);
    expect_contrast(palette, group, QPalette::HighlightedText,
                    QPalette::Highlight, 4.5);
    expect_contrast(palette, group, QPalette::Link, QPalette::Base, 4.5);
  }

  expect_contrast(palette, QPalette::Disabled, QPalette::WindowText,
                  QPalette::Window, 4.5);
  expect_contrast(palette, QPalette::Disabled, QPalette::Text, QPalette::Base,
                  4.5);
  expect_contrast(palette, QPalette::Disabled, QPalette::ButtonText,
                  QPalette::Button, 4.5);
  expect_contrast(palette, QPalette::Disabled, QPalette::HighlightedText,
                  QPalette::Highlight, 4.5);
  expect_contrast(palette, QPalette::Disabled, QPalette::PlaceholderText,
                  QPalette::Base, 3.0);

  EXPECT_LT(relative_luminance(
                palette.color(QPalette::Disabled, QPalette::Text)),
            relative_luminance(
                palette.color(QPalette::Active, QPalette::Text)));
}

TEST(QtApplicationThemeTests,
     KeepsInteractionAccentDistinctFromControlsInEveryState) {
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});

  for (const QPalette::ColorGroup group :
       {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    expect_contrast(palette, group, QPalette::Highlight, QPalette::Button,
                    3.0);
    EXPECT_GE(contrast_ratio(
                  palette.color(group, QPalette::Highlight).darker(125),
                  palette.color(group, QPalette::Button)),
              3.0)
        << "Fusion's derived focus outline must remain distinct";
    EXPECT_EQ(palette.color(group, QPalette::Accent),
              palette.color(group, QPalette::Highlight));
  }
}

TEST(QtApplicationThemeTests,
     LeavesPaletteResolutionToThePlatformWhenHighContrastIsRequested) {
  QPalette fusion_palette;
  fusion_palette.setColor(QPalette::Active, QPalette::Window,
                          QColor(1, 2, 3));
  fusion_palette.setColor(QPalette::Disabled, QPalette::Highlight,
                          QColor(250, 249, 248));

  const QPalette high_contrast =
      quader::qt_application_palette_for_contrast_preference(
          fusion_palette, Qt::ContrastPreference::HighContrast);
  EXPECT_EQ(high_contrast.resolveMask(), 0u);
  EXPECT_NE(high_contrast, fusion_palette);
  EXPECT_NE(quader::qt_application_palette_for_contrast_preference(
                fusion_palette, Qt::ContrastPreference::NoPreference),
            fusion_palette);
}

TEST(QtApplicationThemeTests,
     AppliesObsidianAndPlatformResolvedPalettesAcrossContrastTransitions) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication style lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_theme_test";
  char *argv[] = {application_name, nullptr};
  auto owned_application = std::make_unique<QApplication>(argc, argv);

  std::unique_ptr<QStyle> fusion_style(
      QStyleFactory::create(QStringLiteral("Fusion")));
  ASSERT_NE(fusion_style, nullptr);
  QApplication::setStyle(fusion_style.release());
  QStyleHints *style_hints = QGuiApplication::styleHints();
  ASSERT_NE(style_hints, nullptr);

  const QPalette original_palette = QApplication::palette();
  const Qt::ColorScheme original_scheme = style_hints->colorScheme();
  const QPalette fusion_palette = QApplication::style()->standardPalette();
  const QAccessibilityHints *accessibility = style_hints->accessibility();
  const Qt::ContrastPreference preference =
      accessibility != nullptr ? accessibility->contrastPreference()
                               : Qt::ContrastPreference::NoPreference;

  quader::apply_qt_obsidian_application_theme(fusion_palette);

  if (preference == Qt::ContrastPreference::NoPreference) {
    EXPECT_EQ(QApplication::palette(),
              quader::qt_obsidian_application_palette(
                  QApplication::style()->standardPalette()));
    EXPECT_EQ(style_hints->colorScheme(), Qt::ColorScheme::Dark);
  } else {
    EXPECT_EQ(QApplication::palette().resolveMask(), 0u);
  }

  const auto publish_contrast_preference =
      [accessibility, &fusion_palette](Qt::ContrastPreference next) {
        if (accessibility != nullptr) {
          const_cast<QAccessibilityHints *>(accessibility)
              ->contrastPreferenceChanged(next);
        } else {
          quader::apply_qt_application_theme_for_contrast_preference(
              fusion_palette, next);
        }
      };

  publish_contrast_preference(Qt::ContrastPreference::HighContrast);
  EXPECT_EQ(QApplication::palette().resolveMask(), 0u);
  EXPECT_EQ(style_hints->colorScheme(), original_scheme);

  publish_contrast_preference(Qt::ContrastPreference::NoPreference);
  const QPalette effective_fusion_palette =
      QApplication::style()->standardPalette();
  EXPECT_EQ(QApplication::palette(),
            quader::qt_obsidian_application_palette(
                effective_fusion_palette));
  EXPECT_EQ(style_hints->colorScheme(), Qt::ColorScheme::Dark);

  publish_contrast_preference(Qt::ContrastPreference::HighContrast);
  EXPECT_EQ(QApplication::palette().resolveMask(), 0u);
  EXPECT_EQ(style_hints->colorScheme(), original_scheme);

  if (original_scheme == Qt::ColorScheme::Unknown) {
    style_hints->unsetColorScheme();
  } else {
    style_hints->setColorScheme(original_scheme);
  }
  QApplication::setPalette(original_palette);
}

TEST(QtApplicationThemeTests,
     PreservesSurfaceHierarchyAndReadableAxisMarkers) {
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});
  const std::array<QPalette::ColorRole, 5> surfaces{
      QPalette::Base, QPalette::Window, QPalette::AlternateBase,
      QPalette::Button, QPalette::ToolTipBase};

  for (std::size_t index = 1; index < surfaces.size(); ++index) {
    EXPECT_LT(relative_luminance(
                  palette.color(QPalette::Active, surfaces[index - 1])),
              relative_luminance(
                  palette.color(QPalette::Active, surfaces[index])));
  }

  const QColor marker_text =
      palette.color(QPalette::Active, QPalette::Shadow);
  EXPECT_GE(contrast_ratio(
                palette.color(QPalette::Active, QPalette::BrightText),
                marker_text),
            4.5);
  for (const quader_editor_core::ViewAxis axis :
       {quader_editor_core::ViewAxis::X, quader_editor_core::ViewAxis::Y,
        quader_editor_core::ViewAxis::Z}) {
    EXPECT_GE(
        contrast_ratio(marker_text, quader::qt_semantic_axis_color(axis)),
        4.5);
  }
}

TEST(QtApplicationThemeTests,
     SelectsReadableDockGlyphsForDarkLightAndHighContrastPalettes) {
  const QPalette obsidian =
      quader::qt_obsidian_application_palette(QPalette{});
  const QPalette light = dock_surface_palette(QColor(245, 245, 245),
                                              QColor(0, 90, 180));
  const QPalette high_contrast_dark =
      dock_surface_palette(QColor(0, 0, 0), QColor(255, 255, 0));
  const QPalette high_contrast_light =
      dock_surface_palette(QColor(255, 255, 255), QColor(0, 0, 180));

  EXPECT_TRUE(quader::qt_dock_title_surface_uses_packaged_light_glyphs(
      obsidian.color(QPalette::Inactive, QPalette::AlternateBase)));
  EXPECT_FALSE(quader::qt_dock_title_surface_uses_packaged_light_glyphs(
      light.color(QPalette::Inactive, QPalette::AlternateBase)));
  EXPECT_TRUE(quader::qt_dock_title_surface_uses_packaged_light_glyphs(
      high_contrast_dark.color(QPalette::Inactive,
                               QPalette::AlternateBase)));
  EXPECT_FALSE(quader::qt_dock_title_surface_uses_packaged_light_glyphs(
      high_contrast_light.color(QPalette::Inactive,
                                QPalette::AlternateBase)));

  for (const QPalette *palette :
       {&obsidian, &light, &high_contrast_dark, &high_contrast_light}) {
    expect_selected_dock_glyph_contrast(
        palette->color(QPalette::Inactive, QPalette::AlternateBase));
    expect_selected_dock_glyph_contrast(
        palette->color(QPalette::Active, QPalette::Button));
  }

  const QString obsidian_style = quader::qt_dock_title_style_sheet(obsidian);
  EXPECT_TRUE(obsidian_style.contains(
      QStringLiteral(":/quader/icons/dock_close.png")));
  EXPECT_TRUE(obsidian_style.contains(
      QStringLiteral(":/quader/icons/dock_float.png")));
  const QString light_style = quader::qt_dock_title_style_sheet(light);
  EXPECT_TRUE(light_style.contains(
      QStringLiteral(":/quader/icons/dock_close_dark.png")));
  EXPECT_TRUE(light_style.contains(
      QStringLiteral(":/quader/icons/dock_float_dark.png")));
}

TEST(QtApplicationThemeTests,
     UsesAContrastTileWhenInactiveAndActiveSurfacesAreMixed) {
  QPalette mixed = dock_surface_palette(QColor(255, 255, 255),
                                        QColor(173, 128, 244));
  mixed.setColor(QPalette::Active, QPalette::Button, QColor(0, 0, 0));

  const QColor inactive_surface =
      mixed.color(QPalette::Inactive, QPalette::AlternateBase);
  const QColor active_surface =
      mixed.color(QPalette::Active, QPalette::Button);
  expect_selected_dock_glyph_contrast(inactive_surface);
  expect_selected_dock_glyph_contrast(active_surface);

  const QString style_sheet = quader::qt_dock_title_style_sheet(mixed);
  EXPECT_TRUE(style_sheet.contains(
      QStringLiteral(":/quader/icons/dock_close.png")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("background-color: #101010")));
  EXPECT_GE(contrast_ratio(QColor(242, 242, 242), QColor(16, 16, 16)),
            3.0);
}

TEST(QtApplicationThemeTests,
     ResolvesInactiveDockTitleColorsFromTheInactivePaletteGroup) {
  QPalette palette = dock_surface_palette(QColor(0, 0, 0),
                                          QColor(173, 128, 244));
  palette.setColor(QPalette::Active, QPalette::AlternateBase,
                   QColor(255, 255, 255));
  palette.setColor(QPalette::Inactive, QPalette::Text,
                   QColor(230, 225, 235));

  const QString style_sheet = quader::qt_dock_title_style_sheet(palette);
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("background-color: #000000")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("color: #e6e1eb")));
  EXPECT_TRUE(style_sheet.contains(
      QStringLiteral(":/quader/icons/dock_close.png")));
}

TEST(QtApplicationThemeTests,
     KeepsDockPressedOutlineDistinctAndPackagedGlyphsDecodable) {
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});
  for (const QPalette::ColorGroup group :
       {QPalette::Active, QPalette::Inactive}) {
    expect_contrast(palette, group, QPalette::Highlight,
                    QPalette::AlternateBase, 3.0);
    expect_contrast(palette, group, QPalette::Highlight, QPalette::Button,
                    3.0);
  }

  const QString style_sheet = quader::qt_dock_title_style_sheet(palette);
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("background-color: #29262f")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("border: 2px solid #ad80f4")));

  for (const char *icon_name : {"dock_close.png", "dock_float.png",
                                "dock_close_dark.png",
                                "dock_float_dark.png"}) {
    const std::filesystem::path icon_path =
        std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "icons" /
        icon_name;
    const QImage icon(QString::fromStdWString(icon_path.wstring()));
    ASSERT_FALSE(icon.isNull()) << icon_path.string();
    EXPECT_EQ(icon.width(), icon.height()) << icon_path.string();
    EXPECT_GE(icon.width(), 24) << icon_path.string();

    std::size_t visible_pixels = 0;
    for (int y = 0; y < icon.height(); ++y) {
      for (int x = 0; x < icon.width(); ++x) {
        const QColor pixel = icon.pixelColor(x, y);
        if (pixel.alpha() >= 128) {
          ++visible_pixels;
          if (std::string_view(icon_name).find("_dark") ==
              std::string_view::npos) {
            EXPECT_GE(relative_luminance(pixel), 0.75) << icon_path.string();
          } else {
            EXPECT_LE(relative_luminance(pixel), 0.01) << icon_path.string();
          }
        }
      }
    }
    EXPECT_GT(visible_pixels, 0u) << icon_path.string();
  }

  for (const char *icon_stem : {"dock_close", "dock_float"}) {
    const std::filesystem::path icon_root =
        std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "icons";
    const QImage light(QString::fromStdWString(
        (icon_root / (std::string(icon_stem) + ".png")).wstring()));
    const QImage dark(QString::fromStdWString(
        (icon_root / (std::string(icon_stem) + "_dark.png")).wstring()));
    ASSERT_EQ(light.size(), dark.size());
    for (int y = 0; y < light.height(); ++y) {
      for (int x = 0; x < light.width(); ++x) {
        EXPECT_EQ(light.pixelColor(x, y).alpha(),
                  dark.pixelColor(x, y).alpha());
      }
    }
  }
}

TEST(QtApplicationThemeTests,
     InstalledDockPresentationTracksDarkLightAndDarkPaletteTransitions) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_dock_theme_test";
  char *argv[] = {application_name, nullptr};
  auto owned_application = std::make_unique<QApplication>(argc, argv);
  const QPalette original_palette = QApplication::palette();
  const QPalette dark = dock_surface_palette(QColor(20, 20, 20),
                                             QColor(173, 128, 244));
  const QPalette light = dock_surface_palette(QColor(245, 245, 245),
                                              QColor(0, 90, 180));

  QApplication::setPalette(dark);
  QMainWindow main_window;
  QDockWidget dock(&main_window);
  main_window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
  quader::install_palette_aware_dock_title_presentation(dock);
  EXPECT_TRUE(dock.styleSheet().contains(
      QStringLiteral(":/quader/icons/dock_close.png")));

  const auto drain_palette_transition = []() {
    for (int event_turn = 0; event_turn < 2; ++event_turn) {
      QCoreApplication::processEvents();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
  };

  QApplication::setPalette(light);
  drain_palette_transition();
  const QString light_style = dock.styleSheet();
  EXPECT_TRUE(dock.styleSheet().contains(
      QStringLiteral(":/quader/icons/dock_close_dark.png")));

  QApplication::setPalette(dark);
  drain_palette_transition();
  EXPECT_NE(dock.styleSheet(), light_style);
  EXPECT_TRUE(dock.styleSheet().contains(
      QStringLiteral(":/quader/icons/dock_close.png")));

  QPalette darker = dark;
  darker.setColor(QPalette::Active, QPalette::Button, QColor(8, 8, 8));
  const QString first_dark_style = dock.styleSheet();
  QApplication::setPalette(darker);
  drain_palette_transition();
  EXPECT_NE(dock.styleSheet(), first_dark_style);
  EXPECT_EQ(dock.styleSheet(), quader::qt_dock_title_style_sheet(darker));
  EXPECT_TRUE(dock.styleSheet().contains(
      QStringLiteral(":/quader/icons/dock_close.png")));
  QApplication::setPalette(original_palette);
}
