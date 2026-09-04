////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_application_theme.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAccessibilityHints>
#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QMainWindow>
#include <QMargins>
#include <QObject>
#include <QPalette>
#include <QPainter>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

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

void expect_palette_brushes_equal(const QPalette &actual,
                                  const QPalette &expected) {
  constexpr std::array<QPalette::ColorGroup, 3> kGroups{
      QPalette::Active, QPalette::Inactive, QPalette::Disabled};
  for (const QPalette::ColorGroup group : kGroups) {
    for (int role = 0; role < static_cast<int>(QPalette::NColorRoles);
         ++role) {
      const auto color_role = static_cast<QPalette::ColorRole>(role);
      EXPECT_EQ(actual.brush(group, color_role),
                expected.brush(group, color_role))
          << "group=" << static_cast<int>(group) << " role=" << role;
    }
  }
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
      quader::qt_dock_title_surface_uses_light_glyph(surface)
          ? QColor(242, 242, 242)
          : QColor(16, 16, 16);
  EXPECT_GE(contrast_ratio(selected_glyph, surface), 3.0);
}

QImage render_icon(const QIcon &icon, const QSize &logical_size, qreal dpr) {
  QImage image(qRound(logical_size.width() * dpr),
               qRound(logical_size.height() * dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(dpr);
  image.fill(Qt::transparent);
  {
    QPainter painter(&image);
    icon.paint(&painter, QRect(QPoint{}, logical_size));
  }
  return image;
}

} // namespace

TEST(QtUiScaleMetricsTests, ScalesDesignValuesWithoutCompounding) {
  const quader::QtUiScaleMetrics metrics(2.0F, 7U);

  EXPECT_FLOAT_EQ(metrics.factor(), 2.0F);
  EXPECT_EQ(metrics.revision(), 7U);
  EXPECT_EQ(metrics.px(9), 18);
  EXPECT_FLOAT_EQ(metrics.px(1.25F), 2.5F);
  EXPECT_DOUBLE_EQ(metrics.px(2.5), 5.0);
  EXPECT_EQ(metrics.size(QSize(12, 8)), QSize(24, 16));
  EXPECT_EQ(metrics.size(QSize(-1, 8)), QSize(-1, 16));
  EXPECT_EQ(metrics.margins(QMargins(1, 2, 3, 4)),
            QMargins(2, 4, 6, 8));
  EXPECT_EQ(metrics.device_pixels(10, 1.5), 30);

  QFont point_font;
  point_font.setPointSizeF(10.0);
  EXPECT_DOUBLE_EQ(metrics.font(point_font).pointSizeF(), 20.0);

  QFont pixel_font;
  pixel_font.setPixelSize(11);
  EXPECT_EQ(metrics.font(pixel_font).pixelSize(), 22);

  const quader::QtUiScaleMetrics restored(1.0F, 8U);
  EXPECT_EQ(restored.size(QSize(12, 8)), QSize(12, 8));
  EXPECT_DOUBLE_EQ(restored.font(point_font).pointSizeF(), 10.0);
}

TEST(QtUiScaleMetricsTests, NormalizesInvalidFactorsAndDeviceRatios) {
  EXPECT_FLOAT_EQ(quader::QtUiScaleMetrics(0.1F).factor(), 0.5F);
  EXPECT_FLOAT_EQ(quader::QtUiScaleMetrics(5.0F).factor(), 3.0F);
  EXPECT_FLOAT_EQ(
      quader::QtUiScaleMetrics(std::numeric_limits<float>::infinity())
          .factor(),
      1.0F);
  EXPECT_FLOAT_EQ(
      quader::QtUiScaleMetrics(std::numeric_limits<float>::quiet_NaN())
          .factor(),
      1.0F);

  const quader::QtUiScaleMetrics metrics(2.0F);
  EXPECT_EQ(metrics.device_pixels(10, 0.0), 20);
  EXPECT_EQ(metrics.device_pixels(10,
                                  std::numeric_limits<qreal>::quiet_NaN()),
            20);
  EXPECT_EQ(metrics.device_pixels(-1, 2.0), -1);
}

TEST(QtUiScaleMetricsTests,
     ControllerAppliesOneRevisionFromAnImmutableApplicationFont) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication style lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_ui_scale_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QFont baseline = QApplication::font();
  baseline.setPointSizeF(10.0);
  QApplication::setFont(baseline);
  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::Active, QPalette::Window, QColor(17, 18, 19));
  QApplication::setPalette(palette);
  const QPalette applied_palette = QApplication::palette();

  quader::QtUiScaleApplicationController controller(application);
  expect_palette_brushes_equal(QApplication::palette(), applied_palette);
  const int baseline_margin = controller.style().pixelMetric(
      QStyle::PM_LayoutLeftMargin, nullptr, nullptr);
  const int baseline_hairline = controller.style().pixelMetric(
      QStyle::PM_DefaultFrameWidth, nullptr, nullptr);

  QObject receiver;
  int notification_count = 0;
  std::uint64_t observed_revision = 99U;
  const auto listener_id = controller.add_change_listener(
      receiver, [&](const quader::QtUiScaleMetrics &metrics) {
        ++notification_count;
        observed_revision = metrics.revision();
      });
  EXPECT_NE(listener_id, 0U);
  EXPECT_EQ(notification_count, 1);
  EXPECT_EQ(observed_revision, 0U);

  EXPECT_TRUE(controller.apply_factor(2.0F));
  EXPECT_EQ(controller.metrics().revision(), 1U);
  EXPECT_EQ(notification_count, 2);
  EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), 20.0);
  EXPECT_EQ(controller.style().pixelMetric(QStyle::PM_LayoutLeftMargin,
                                           nullptr, nullptr),
            baseline_margin * 2);
  EXPECT_EQ(controller.style().pixelMetric(QStyle::PM_DefaultFrameWidth,
                                           nullptr, nullptr),
            baseline_hairline);
  expect_palette_brushes_equal(QApplication::palette(), applied_palette);

  EXPECT_FALSE(controller.apply_factor(2.0F));
  EXPECT_EQ(notification_count, 2);
  EXPECT_TRUE(controller.apply_factor(1.0F));
  EXPECT_EQ(controller.metrics().revision(), 2U);
  EXPECT_EQ(notification_count, 3);
  EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), 10.0);
  EXPECT_EQ(controller.style().pixelMetric(QStyle::PM_LayoutLeftMargin,
                                           nullptr, nullptr),
            baseline_margin);

  controller.remove_change_listener(listener_id);
  EXPECT_TRUE(controller.apply_factor(1.5F));
  EXPECT_EQ(notification_count, 3);
}

TEST(QtUiScaleMetricsTests,
     EveryDeclaredFusionPixelMetricReceivesTheUserFactorExactlyOnce) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication style lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_ui_scale_proxy_metric_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  quader::QtUiScaleProxyStyle style;
  constexpr std::array<QStyle::PixelMetric, 74> kScaledMetrics{
      QStyle::PM_ButtonMargin,
      QStyle::PM_ButtonDefaultIndicator,
      QStyle::PM_MenuButtonIndicator,
      QStyle::PM_ScrollBarExtent,
      QStyle::PM_ScrollBarSliderMin,
      QStyle::PM_SliderThickness,
      QStyle::PM_SliderControlThickness,
      QStyle::PM_SliderLength,
      QStyle::PM_SliderTickmarkOffset,
      QStyle::PM_DockWidgetSeparatorExtent,
      QStyle::PM_DockWidgetHandleExtent,
      QStyle::PM_TabBarTabOverlap,
      QStyle::PM_TabBarTabHSpace,
      QStyle::PM_TabBarTabVSpace,
      QStyle::PM_TabBarBaseHeight,
      QStyle::PM_TabBarBaseOverlap,
      QStyle::PM_ProgressBarChunkWidth,
      QStyle::PM_SplitterWidth,
      QStyle::PM_TitleBarHeight,
      QStyle::PM_MenuScrollerHeight,
      QStyle::PM_MenuHMargin,
      QStyle::PM_MenuVMargin,
      QStyle::PM_MenuTearoffHeight,
      QStyle::PM_MenuBarItemSpacing,
      QStyle::PM_MenuBarVMargin,
      QStyle::PM_MenuBarHMargin,
      QStyle::PM_IndicatorWidth,
      QStyle::PM_IndicatorHeight,
      QStyle::PM_ExclusiveIndicatorWidth,
      QStyle::PM_ExclusiveIndicatorHeight,
      QStyle::PM_MdiSubWindowMinimizedWidth,
      QStyle::PM_HeaderMargin,
      QStyle::PM_HeaderMarkSize,
      QStyle::PM_HeaderGripMargin,
      QStyle::PM_TabBarScrollButtonWidth,
      QStyle::PM_ToolBarHandleExtent,
      QStyle::PM_ToolBarItemSpacing,
      QStyle::PM_ToolBarItemMargin,
      QStyle::PM_ToolBarSeparatorExtent,
      QStyle::PM_ToolBarExtensionExtent,
      QStyle::PM_SpinBoxSliderHeight,
      QStyle::PM_ToolBarIconSize,
      QStyle::PM_ListViewIconSize,
      QStyle::PM_IconViewIconSize,
      QStyle::PM_SmallIconSize,
      QStyle::PM_LargeIconSize,
      QStyle::PM_FocusFrameVMargin,
      QStyle::PM_FocusFrameHMargin,
      QStyle::PM_CheckBoxLabelSpacing,
      QStyle::PM_TabBarIconSize,
      QStyle::PM_SizeGripSize,
      QStyle::PM_DockWidgetTitleMargin,
      QStyle::PM_MessageBoxIconSize,
      QStyle::PM_ButtonIconSize,
      QStyle::PM_DockWidgetTitleBarButtonMargin,
      QStyle::PM_RadioButtonLabelSpacing,
      QStyle::PM_LayoutLeftMargin,
      QStyle::PM_LayoutTopMargin,
      QStyle::PM_LayoutRightMargin,
      QStyle::PM_LayoutBottomMargin,
      QStyle::PM_LayoutHorizontalSpacing,
      QStyle::PM_LayoutVerticalSpacing,
      QStyle::PM_TabBar_ScrollButtonOverlap,
      QStyle::PM_TabCloseIndicatorWidth,
      QStyle::PM_TabCloseIndicatorHeight,
      QStyle::PM_ScrollView_ScrollBarSpacing,
      QStyle::PM_SubMenuOverlap,
      QStyle::PM_TreeViewIndentation,
      QStyle::PM_HeaderDefaultSectionSizeHorizontal,
      QStyle::PM_HeaderDefaultSectionSizeVertical,
      QStyle::PM_TitleBarButtonIconSize,
      QStyle::PM_TitleBarButtonSize,
      QStyle::PM_LineEditIconSize,
      QStyle::PM_LineEditIconMargin};
  std::array<int, kScaledMetrics.size()> design_values{};
  for (std::size_t index = 0; index < kScaledMetrics.size(); ++index) {
    design_values[index] =
        style.pixelMetric(kScaledMetrics[index], nullptr, nullptr);
  }

  for (const float factor : {0.5F, 1.0F, 1.25F, 2.0F, 3.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    style.set_metrics(metrics);
    for (std::size_t index = 0; index < kScaledMetrics.size(); ++index) {
      const int expected = design_values[index] > 0
                               ? std::max(1, metrics.px(design_values[index]))
                               : design_values[index];
      EXPECT_EQ(style.pixelMetric(kScaledMetrics[index], nullptr, nullptr),
                expected)
          << "metric=" << static_cast<int>(kScaledMetrics[index])
          << " factor=" << factor;
    }
  }
}

TEST(QtUiScaleMetricsTests,
     ControllerKeepsAnUnresolvedAccessibilityPalettePlatformOwned) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication style lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_ui_scale_accessibility_palette_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  std::unique_ptr<QStyle> fusion_style(
      QStyleFactory::create(QStringLiteral("Fusion")));
  ASSERT_NE(fusion_style, nullptr);
  QApplication::setStyle(fusion_style.release());
  QApplication::setPalette(QPalette{});
  ASSERT_EQ(QApplication::palette().resolveMask(), 0u);

  quader::QtUiScaleApplicationController controller(application);
  EXPECT_EQ(QApplication::palette().resolveMask(), 0u);
  expect_palette_brushes_equal(QApplication::palette(), QPalette{});

  ASSERT_TRUE(controller.apply_factor(2.0F));
  EXPECT_EQ(QApplication::palette().resolveMask(), 0u);
  expect_palette_brushes_equal(QApplication::palette(), QPalette{});
}

TEST(QtUiScaleMetricsTests,
     ControllerRestoresItsBaselineBeforeASecondInitialization) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication style lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_ui_scale_reinitialize_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QFont baseline = QApplication::font();
  baseline.setPointSizeF(10.0);
  QApplication::setFont(baseline);

  {
    quader::QtUiScaleApplicationController first(application, 2.0F);
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), 20.0);
  }
  EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), 10.0);

  {
    quader::QtUiScaleApplicationController second(application, 2.0F);
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), 20.0)
        << "reinitialization must derive from the unscaled baseline";
  }
  EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), 10.0);
}

TEST(QtUiScaleMetricsTests,
     MultiresolutionRasterIconsKeepLogicalSizeAndDprOutOfCacheIdentity) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_multiresolution_icon_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QImage source(QSize(384, 192), QImage::Format_ARGB32_Premultiplied);
  source.fill(QColor(0xad, 0x80, 0xf4));
  const QIcon icon = quader::qt_multiresolution_icon(source);

  const QPixmap at_four_x = icon.pixmap(QSize(96, 96), 4.0);
  ASSERT_FALSE(at_four_x.isNull());
  EXPECT_EQ(at_four_x.size(), QSize(384, 192));
  EXPECT_DOUBLE_EQ(at_four_x.devicePixelRatio(), 4.0);
  EXPECT_EQ(at_four_x.deviceIndependentSize(), QSizeF(96.0, 48.0));

  const QPixmap at_two_x = icon.pixmap(QSize(192, 192), 2.0);
  ASSERT_FALSE(at_two_x.isNull());
  EXPECT_EQ(at_two_x.size(), QSize(384, 192));
  EXPECT_DOUBLE_EQ(at_two_x.devicePixelRatio(), 2.0);
  EXPECT_EQ(at_two_x.deviceIndependentSize(), QSizeF(192.0, 96.0));

  constexpr qreal kFirstCloseDpr = 1.0001;
  constexpr qreal kSecondCloseDpr = 1.0002;
  const QString first_identity =
      quader::qt_multiresolution_icon_dpr_cache_identity(kFirstCloseDpr);
  const QString second_identity =
      quader::qt_multiresolution_icon_dpr_cache_identity(kSecondCloseDpr);
  EXPECT_NE(first_identity, second_identity)
      << "distinct DPRs must not alias in the shared pixmap cache";
  EXPECT_DOUBLE_EQ(first_identity.toDouble(), kFirstCloseDpr);
  EXPECT_DOUBLE_EQ(second_identity.toDouble(), kSecondCloseDpr);
}

TEST(QtUiScaleMetricsTests,
     MultiresolutionRasterIconsRenderDisabledModeThroughScaledFusion) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_multiresolution_disabled_icon_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtUiScaleApplicationController scale_controller(application, 2.0F);

  QImage source(QSize(96, 96), QImage::Format_ARGB32_Premultiplied);
  source.fill(QColor(0xad, 0x80, 0xf4));
  const QIcon icon = quader::qt_multiresolution_icon(source);
  const QPixmap disabled =
      icon.pixmap(QSize(32, 32), 2.0, QIcon::Disabled, QIcon::Off);
  const QPixmap selected =
      icon.pixmap(QSize(32, 32), 2.0, QIcon::Selected, QIcon::On);

  ASSERT_FALSE(disabled.isNull());
  EXPECT_EQ(disabled.size(), QSize(64, 64));
  EXPECT_DOUBLE_EQ(disabled.devicePixelRatio(), 2.0);
  EXPECT_EQ(disabled.deviceIndependentSize(), QSizeF(32.0, 32.0));
  ASSERT_FALSE(selected.isNull());
  EXPECT_EQ(selected.size(), QSize(64, 64));
  EXPECT_DOUBLE_EQ(selected.devicePixelRatio(), 2.0);
  EXPECT_EQ(selected.deviceIndependentSize(), QSizeF(32.0, 32.0));
}

TEST(QtUiScaleMetricsTests,
     MultiresolutionRasterUpsamplingKeepsPhysicalEdgesSharp) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_multiresolution_edge_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QImage source(QSize(96, 96), QImage::Format_ARGB32);
  source.fill(Qt::transparent);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = y; x < source.width(); ++x) {
      source.setPixelColor(x, y,
                           x == 70 ? QColor(Qt::black)
                                   : QColor(Qt::white));
    }
  }
  const QIcon icon = quader::qt_multiresolution_icon(source);
  const QImage baseline = icon.pixmap(QSize(32, 32), 1.0).toImage();

  for (const qreal dpr : {2.0, 4.0}) {
    const QPixmap pixmap = icon.pixmap(QSize(96, 96), dpr);
    ASSERT_FALSE(pixmap.isNull());
    EXPECT_EQ(pixmap.size(), QSize(qRound(96.0 * dpr),
                                   qRound(96.0 * dpr)));
    EXPECT_DOUBLE_EQ(pixmap.devicePixelRatio(), dpr);

    const QImage image = pixmap.toImage().convertToFormat(
        QImage::Format_ARGB32);
    const int row = qRound(24.0 * dpr);
    int intermediate_alpha_pixels = 0;
    int intermediate_internal_edge_pixels = 0;
    for (int x = 0; x < image.width(); ++x) {
      const QColor pixel = image.pixelColor(x, row);
      if (pixel.alpha() > 0 && pixel.alpha() < 255) {
        ++intermediate_alpha_pixels;
      }
      if (x >= qRound(66.0 * dpr) && x <= qRound(74.0 * dpr) &&
          pixel.alpha() == 255 && pixel.red() > 24 &&
          pixel.red() < 231) {
        ++intermediate_internal_edge_pixels;
      }
    }
    EXPECT_LE(intermediate_alpha_pixels, 2)
        << "the diagonal silhouette keeps a physical-pixel alpha fringe";
    EXPECT_LE(intermediate_internal_edge_pixels, 4)
        << "the opaque one-source-pixel stripe keeps bounded edge fringes";
  }

  EXPECT_EQ(icon.pixmap(QSize(32, 32), 1.0).toImage(), baseline)
      << "a large-DPR request must not alter a later baseline request";
}

TEST(QtUiScaleMetricsTests,
     SvgAuthoredMaterialGlyphsRenderAsScalablePainterGeometry) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_scalable_glyph_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  for (const quader::QtScalableGlyph glyph : {
           quader::QtScalableGlyph::MaterialVisibility,
           quader::QtScalableGlyph::MaterialVisibilityOff,
           quader::QtScalableGlyph::MaterialLock,
           quader::QtScalableGlyph::MaterialLockOpen}) {
    const QIcon icon =
        quader::qt_scalable_glyph_icon(glyph, QColor(0xad, 0x80, 0xf4));
    ASSERT_FALSE(icon.isNull());
    const QPixmap pixmap = icon.pixmap(QSize(96, 96), 4.0);
    ASSERT_FALSE(pixmap.isNull());
    EXPECT_EQ(pixmap.size(), QSize(384, 384));
    EXPECT_DOUBLE_EQ(pixmap.devicePixelRatio(), 4.0);
    EXPECT_EQ(pixmap.deviceIndependentSize(), QSizeF(96.0, 96.0));

    const QImage image = pixmap.toImage();
    std::size_t visible_pixels = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (image.pixelColor(x, y).alpha() != 0) {
          ++visible_pixels;
        }
      }
    }
    EXPECT_GT(visible_pixels, 1000u);
  }
}

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

  EXPECT_TRUE(quader::qt_dock_title_surface_uses_light_glyph(
      obsidian.color(QPalette::Inactive, QPalette::AlternateBase)));
  EXPECT_FALSE(quader::qt_dock_title_surface_uses_light_glyph(
      light.color(QPalette::Inactive, QPalette::AlternateBase)));
  EXPECT_TRUE(quader::qt_dock_title_surface_uses_light_glyph(
      high_contrast_dark.color(QPalette::Inactive,
                               QPalette::AlternateBase)));
  EXPECT_FALSE(quader::qt_dock_title_surface_uses_light_glyph(
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
  EXPECT_FALSE(obsidian_style.contains(QStringLiteral("url(")));
  EXPECT_FALSE(
      obsidian_style.contains(QStringLiteral("titlebar-close-icon")));
  const QString light_style = quader::qt_dock_title_style_sheet(light);
  EXPECT_FALSE(light_style.contains(QStringLiteral("url(")));
  EXPECT_FALSE(
      light_style.contains(QStringLiteral("titlebar-normal-icon")));
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
  EXPECT_FALSE(style_sheet.contains(QStringLiteral("url(")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("background-color: #101010")));
  EXPECT_GE(contrast_ratio(QColor(242, 242, 242), QColor(16, 16, 16)),
            3.0);
}

TEST(QtApplicationThemeTests,
     DockTitleScaleReflowsControlsButPreservesStateHairlines) {
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});
  const quader::QtUiScaleMetrics doubled(2.0F, 1U);

  const QString style_sheet =
      quader::qt_dock_title_style_sheet(palette, doubled);
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("padding: 0 8px")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("width: 36px")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("height: 36px")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("icon-size: 48px")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("border-bottom: 1px")));
  EXPECT_TRUE(style_sheet.contains(QStringLiteral("border: 2px solid")));
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
  EXPECT_FALSE(style_sheet.contains(QStringLiteral("url(")));
}

TEST(QtApplicationThemeTests,
     KeepsDockPressedOutlineDistinctAndPaintsScalableGlyphGeometry) {
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});
  const QPalette light_palette =
      dock_surface_palette(QColor(245, 245, 245), QColor(0, 90, 180));
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

  for (const QPalette *glyph_palette : {&palette, &light_palette}) {
    for (const quader::QtDockTitleGlyph glyph : {
             quader::QtDockTitleGlyph::Close,
             quader::QtDockTitleGlyph::Float}) {
      const QIcon icon =
          quader::qt_dock_title_glyph_icon(glyph, *glyph_palette);
      ASSERT_FALSE(icon.isNull());
      EXPECT_TRUE(icon.availableSizes().isEmpty())
          << "the icon engine must not expose fixed raster sizes";

      const QImage image = render_icon(icon, QSize(96, 96), 4.0);
      EXPECT_EQ(image.size(), QSize(384, 384));
      EXPECT_DOUBLE_EQ(image.devicePixelRatio(), 4.0);
      std::size_t visible_pixels = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          if (image.pixelColor(x, y).alpha() >= 128) {
            ++visible_pixels;
          }
        }
      }
      EXPECT_GT(visible_pixels, 1000u);
    }
  }
}

TEST(QtApplicationThemeTests,
     DockGlyphPixmapsKeepTransparentBackingsAcrossMonitorScales) {
  if (QCoreApplication::instance() != nullptr) {
    GTEST_SKIP() << "This test owns the QApplication lifecycle";
  }

  int argc = 1;
  char application_name[] = "quader_dock_glyph_pixmap_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  const QPalette palette =
      quader::qt_obsidian_application_palette(QPalette{});

  for (const quader::QtDockTitleGlyph glyph : {
           quader::QtDockTitleGlyph::Close,
           quader::QtDockTitleGlyph::Float}) {
    const QIcon icon = quader::qt_dock_title_glyph_icon(glyph, palette);
    for (const qreal dpr : {1.0, 1.25, 2.0, 3.0}) {
      for (const QIcon::Mode mode : {
               QIcon::Normal, QIcon::Active, QIcon::Disabled}) {
        SCOPED_TRACE(::testing::Message()
                     << "glyph=" << static_cast<int>(glyph)
                     << " dpr=" << dpr
                     << " mode=" << static_cast<int>(mode));
        const QPixmap pixmap =
            icon.pixmap(QSize(24, 24), dpr, mode, QIcon::Off);
        ASSERT_FALSE(pixmap.isNull());
        EXPECT_EQ(pixmap.size(),
                  QSize(qRound(24.0 * dpr), qRound(24.0 * dpr)));
        EXPECT_DOUBLE_EQ(pixmap.devicePixelRatio(), dpr);
        EXPECT_TRUE(pixmap.hasAlphaChannel());

        const QImage image =
            pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        ASSERT_FALSE(image.isNull());
        EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0);
        EXPECT_EQ(image.pixelColor(image.width() - 1, 0).alpha(), 0);
        EXPECT_EQ(image.pixelColor(0, image.height() - 1).alpha(), 0);
        EXPECT_EQ(image.pixelColor(image.width() - 1,
                                   image.height() - 1)
                      .alpha(),
                  0);

        std::size_t visible_pixels = 0;
        std::size_t transparent_pixels = 0;
        for (int y = 0; y < image.height(); ++y) {
          for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() == 0) {
              ++transparent_pixels;
            } else {
              ++visible_pixels;
            }
          }
        }
        EXPECT_GT(visible_pixels, 0u);
        EXPECT_GT(transparent_pixels, visible_pixels)
            << "the glyph must not acquire an opaque garbage tile";
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
  EXPECT_FALSE(dock.styleSheet().contains(QStringLiteral("url(")));

  QAbstractButton *const close_button = dock.findChild<QAbstractButton *>(
      QStringLiteral("qt_dockwidget_closebutton"),
      Qt::FindDirectChildrenOnly);
  QAbstractButton *const float_button = dock.findChild<QAbstractButton *>(
      QStringLiteral("qt_dockwidget_floatbutton"),
      Qt::FindDirectChildrenOnly);
  ASSERT_NE(close_button, nullptr);
  ASSERT_NE(float_button, nullptr);
  EXPECT_TRUE(close_button->icon().availableSizes().isEmpty());
  EXPECT_TRUE(float_button->icon().availableSizes().isEmpty());
  EXPECT_GT(render_icon(close_button->icon(), QSize(24, 24), 3.0)
                .pixelColor(36, 36)
                .alpha(),
            0);

  const auto drain_palette_transition = []() {
    for (int event_turn = 0; event_turn < 2; ++event_turn) {
      QCoreApplication::processEvents();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
  };

  QApplication::setPalette(light);
  drain_palette_transition();
  const QString light_style = dock.styleSheet();
  EXPECT_FALSE(dock.styleSheet().contains(QStringLiteral("url(")));
  EXPECT_LT(relative_luminance(
                render_icon(close_button->icon(), QSize(24, 24), 3.0)
                    .pixelColor(36, 36)),
            0.05);

  QApplication::setPalette(dark);
  drain_palette_transition();
  EXPECT_NE(dock.styleSheet(), light_style);
  EXPECT_FALSE(dock.styleSheet().contains(QStringLiteral("url(")));
  EXPECT_GT(relative_luminance(
                render_icon(close_button->icon(), QSize(24, 24), 3.0)
                    .pixelColor(36, 36)),
            0.75);

  QPalette darker = dark;
  darker.setColor(QPalette::Active, QPalette::Button, QColor(8, 8, 8));
  const QString first_dark_style = dock.styleSheet();
  QApplication::setPalette(darker);
  drain_palette_transition();
  EXPECT_NE(dock.styleSheet(), first_dark_style);
  EXPECT_EQ(dock.styleSheet(), quader::qt_dock_title_style_sheet(darker));
  EXPECT_FALSE(dock.styleSheet().contains(QStringLiteral("url(")));

  const quader::QtUiScaleMetrics doubled(2.0F, 9U);
  quader::apply_ui_scale_to_palette_aware_dock_title_presentation(
      dock, doubled);
  EXPECT_TRUE(dock.styleSheet().contains(QStringLiteral("width: 36px")));
  QApplication::setPalette(light);
  drain_palette_transition();
  EXPECT_EQ(dock.styleSheet(),
            quader::qt_dock_title_style_sheet(light, doubled));
  EXPECT_TRUE(dock.styleSheet().contains(QStringLiteral("width: 36px")));
  QApplication::setPalette(original_palette);
}
