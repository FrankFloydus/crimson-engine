////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_dock_title_presentation.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace quader {
namespace {

constexpr QRgb kDarkDockGlyphRgb = qRgb(16, 16, 16);
constexpr QRgb kLightDockGlyphRgb = qRgb(242, 242, 242);
constexpr auto kDockUiScaleFactorProperty =
    "quaderDockTitleUiScaleFactor";
constexpr auto kDockCloseButtonObjectName =
    "qt_dockwidget_closebutton";
constexpr auto kDockFloatButtonObjectName =
    "qt_dockwidget_floatbutton";
constexpr qreal kDockGlyphDesignExtent = 96.0;

class QtDockTitleGlyphIconEngine final : public QIconEngine {
public:
  QtDockTitleGlyphIconEngine(QtDockTitleGlyph glyph, QColor color)
      : glyph_(glyph), color_(std::move(color)) {}

  [[nodiscard]] QIconEngine *clone() const override {
    return new QtDockTitleGlyphIconEngine(*this);
  }

  [[nodiscard]] QString key() const override {
    return QStringLiteral("QuaderDockTitleGlyph");
  }

  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
             QIcon::State /*state*/) override {
    if (painter == nullptr || rect.isEmpty()) {
      return;
    }

    const qreal extent = std::min(rect.width(), rect.height());
    const QRectF bounds(rect.x() + (rect.width() - extent) * 0.5,
                        rect.y() + (rect.height() - extent) * 0.5, extent,
                        extent);
    const auto design_point = [&bounds, extent](qreal x, qreal y) {
      return QPointF(bounds.left() + extent * x / kDockGlyphDesignExtent,
                     bounds.top() + extent * y / kDockGlyphDesignExtent);
    };

    QColor paint_color = color_;
    if (mode == QIcon::Disabled) {
      paint_color.setAlphaF(paint_color.alphaF() * 0.55);
    }

    QPen pen(paint_color);
    pen.setWidthF(extent * (glyph_ == QtDockTitleGlyph::Close ? 11.0 : 9.0) /
                  kDockGlyphDesignExtent);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    if (glyph_ == QtDockTitleGlyph::Close) {
      painter->drawLine(design_point(18.0, 18.0),
                        design_point(78.0, 78.0));
      painter->drawLine(design_point(78.0, 18.0),
                        design_point(18.0, 78.0));
    } else {
      QPainterPath path;
      path.moveTo(design_point(60.0, 18.0));
      path.lineTo(design_point(78.0, 18.0));
      path.lineTo(design_point(78.0, 36.0));
      path.moveTo(design_point(78.0, 18.0));
      path.lineTo(design_point(46.0, 50.0));
      path.moveTo(design_point(64.0, 46.0));
      path.lineTo(design_point(64.0, 78.0));
      path.lineTo(design_point(18.0, 78.0));
      path.lineTo(design_point(18.0, 32.0));
      path.lineTo(design_point(50.0, 32.0));
      painter->drawPath(path);
    }
    painter->restore();
  }

  [[nodiscard]] QPixmap pixmap(const QSize &size, QIcon::Mode mode,
                               QIcon::State state) override {
    if (!size.isValid() || size.isEmpty()) {
      return {};
    }

    QPixmap raster(size);
    raster.fill(Qt::transparent);
    QPainter painter(&raster);
    paint(&painter, QRect(QPoint{}, size), mode, state);
    painter.end();
    return raster;
  }

private:
  QtDockTitleGlyph glyph_ = QtDockTitleGlyph::Close;
  QColor color_;
};

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

struct DockTitleGlyphPolicy final {
  QColor color;
  bool uses_light = false;
  bool needs_contrast_tile = false;
};

DockTitleGlyphPolicy dock_title_glyph_policy(const QPalette &palette) {
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
  const bool uses_light = light_contrast > dark_contrast;
  return {
      .color = uses_light ? light_glyph : dark_glyph,
      .uses_light = uses_light,
      .needs_contrast_tile =
          std::max(light_contrast, dark_contrast) < 3.0,
  };
}

QString dock_palette_fingerprint(const QPalette &palette) {
  constexpr std::array<QPalette::ColorGroup, 3> kGroups{
      QPalette::Active, QPalette::Inactive, QPalette::Disabled};
  constexpr std::array<QPalette::ColorRole, 6> kRoles{
      QPalette::AlternateBase, QPalette::Button,     QPalette::Text,
      QPalette::ButtonText,    QPalette::Mid,        QPalette::Accent};

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

void apply_programmatic_dock_title_glyphs(QDockWidget &dock,
                                           const QPalette &palette) {
  if (QAbstractButton *const close_button =
          dock.findChild<QAbstractButton *>(
              QString::fromLatin1(kDockCloseButtonObjectName),
              Qt::FindDirectChildrenOnly)) {
    close_button->setIcon(
        qt_dock_title_glyph_icon(QtDockTitleGlyph::Close, palette));
  }
  if (QAbstractButton *const float_button =
          dock.findChild<QAbstractButton *>(
              QString::fromLatin1(kDockFloatButtonObjectName),
              Qt::FindDirectChildrenOnly)) {
    float_button->setIcon(
        qt_dock_title_glyph_icon(QtDockTitleGlyph::Float, palette));
  }
}

void apply_palette_aware_dock_title_presentation(
    QDockWidget &dock, const QPalette &application_palette,
    const QtUiScaleMetrics &metrics) {
  const QString style_sheet =
      qt_dock_title_style_sheet(application_palette, metrics);
  if (dock.styleSheet() != style_sheet) {
    dock.setStyleSheet(style_sheet);
  }
  apply_programmatic_dock_title_glyphs(dock, application_palette);
}

QtUiScaleMetrics dock_title_ui_scale(const QDockWidget &dock) {
  bool converted = false;
  const float factor =
      dock.property(kDockUiScaleFactorProperty).toFloat(&converted);
  return QtUiScaleMetrics(converted ? factor : 1.0F);
}

} // namespace

bool qt_dock_title_surface_uses_light_glyph(const QColor &surface) {
  return contrast_ratio(QColor::fromRgb(kLightDockGlyphRgb), surface) >
         contrast_ratio(QColor::fromRgb(kDarkDockGlyphRgb), surface);
}

QIcon qt_dock_title_glyph_icon(QtDockTitleGlyph glyph,
                               const QPalette &palette) {
  return QIcon(new QtDockTitleGlyphIconEngine(
      glyph, dock_title_glyph_policy(palette).color));
}

QString qt_dock_title_style_sheet(const QPalette &palette) {
  return qt_dock_title_style_sheet(palette, QtUiScaleMetrics{});
}

QString qt_dock_title_style_sheet(const QPalette &palette,
                                  const QtUiScaleMetrics &metrics) {
  const QColor inactive_surface =
      palette.color(QPalette::Inactive, QPalette::AlternateBase);
  const QColor active_surface =
      palette.color(QPalette::Active, QPalette::Button);
  const DockTitleGlyphPolicy glyph_policy =
      dock_title_glyph_policy(palette);
  const QString button_background =
      glyph_policy.needs_contrast_tile
          ? (glyph_policy.uses_light ? QStringLiteral("#101010")
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
  const QString accent =
      qss_color(palette.color(QPalette::Active, QPalette::Accent));
  const QString pressed_background =
      glyph_policy.needs_contrast_tile ? button_background
                                       : active_title_background;

  return dock_palette_fingerprint(palette) + QStringLiteral(R"QUADER(
QDockWidget::title {
  background-color: %1;
  color: %2;
  border-bottom: 1px solid %3;
  padding: 0 %10px;
  text-align: left;
}
QDockWidget::title:active {
  background-color: %4;
  color: %5;
  border-bottom: 1px solid %6;
}
QDockWidget::close-button,
QDockWidget::float-button {
  background-color: %7;
  border: 1px solid transparent;
  padding: 0;
  width: %11px;
  height: %11px;
  icon-size: %12px;
}
QDockWidget::close-button:hover,
QDockWidget::float-button:hover {
  background-color: %7;
  border-color: %8;
}
QDockWidget::close-button:pressed,
QDockWidget::float-button:pressed {
  background-color: %9;
  border: 2px solid %8;
}
)QUADER")
      .arg(inactive_title_background)
      .arg(inactive_title_text)
      .arg(inactive_title_border)
      .arg(active_title_background)
      .arg(active_title_text)
      .arg(active_title_border)
      .arg(button_background)
      .arg(accent)
      .arg(pressed_background)
      .arg(metrics.px(4))
      .arg(metrics.px(18))
      .arg(metrics.px(24));
}

void install_palette_aware_dock_title_presentation(QDockWidget &dock) {
  QObject::connect(qApp, &QGuiApplication::paletteChanged, &dock,
                   [&dock](const QPalette &palette) {
                     apply_palette_aware_dock_title_presentation(dock,
                                                                  palette,
                                                                  dock_title_ui_scale(dock));
                   });
  dock.setProperty(kDockUiScaleFactorProperty, 1.0F);
  apply_palette_aware_dock_title_presentation(dock,
                                               QApplication::palette(),
                                               QtUiScaleMetrics{});
}

void apply_ui_scale_to_palette_aware_dock_title_presentation(
    QDockWidget &dock, const QtUiScaleMetrics &metrics) {
  dock.setProperty(kDockUiScaleFactorProperty, metrics.factor());
  apply_palette_aware_dock_title_presentation(
      dock, QApplication::palette(), metrics);
}

} // namespace quader
