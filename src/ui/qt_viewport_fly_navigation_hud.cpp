////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_fly_navigation_hud.hpp"

#include "core/localization_service.hpp"
#include "ui/qt_numeric_format.hpp"
#include "viewport/viewport_layout.hpp"

#include <QBitmap>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <string>
#include <string_view>

namespace quader {
namespace {

constexpr qreal kCrosshairGap = 4.0;
constexpr qreal kCrosshairLength = 9.0;

void draw_crosshair_segments(QPainter &painter, QSize size, const QPen &pen,
                             const QtUiScaleMetrics &metrics) {
  const qreal center_x = static_cast<qreal>(size.width()) * 0.5;
  const qreal center_y = static_cast<qreal>(size.height()) * 0.5;
  const qreal scale = static_cast<qreal>(metrics.factor());
  const qreal gap = kCrosshairGap * scale;
  const qreal length = kCrosshairLength * scale;
  painter.setPen(pen);
  painter.drawLine(QPointF{center_x - length, center_y},
                   QPointF{center_x - gap, center_y});
  painter.drawLine(QPointF{center_x + gap, center_y},
                   QPointF{center_x + length, center_y});
  painter.drawLine(QPointF{center_x, center_y - length},
                   QPointF{center_x, center_y - gap});
  painter.drawLine(QPointF{center_x, center_y + gap},
                   QPointF{center_x, center_y + length});
}

QBitmap crosshair_window_mask(QSize size, const QtUiScaleMetrics &metrics) {
  QBitmap mask(size);
  mask.clear();
  QPainter painter(&mask);
  painter.setRenderHint(QPainter::Antialiasing, false);
  draw_crosshair_segments(
      painter, size,
      QPen(Qt::color1,
           std::max<qreal>(1.0, 3.0 * metrics.factor()), Qt::SolidLine,
           Qt::RoundCap),
      metrics);
  return mask;
}

/// Draws the fly-navigation crosshair exclusively from the active Qt palette.
class QtViewportFlyCrosshair final : public QWidget {
public:
  explicit QtViewportFlyCrosshair(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // Native child widgets are opaque on Windows even when their background is
    // not erased. Shape the native window to the painted arms so its untouched
    // rectangular backing surface cannot cover the Filament viewport.
    refresh_scaled_geometry();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    if (ui_scale_metrics_.revision() == metrics.revision() &&
        ui_scale_metrics_.factor() == metrics.factor()) {
      return;
    }
    ui_scale_metrics_ = metrics;
    refresh_scaled_geometry();
    update();
  }

protected:
  bool event(QEvent *event) override {
    if (event != nullptr &&
        (event->type() == QEvent::DevicePixelRatioChange ||
         event->type() == QEvent::ScreenChangeInternal)) {
      refresh_scaled_geometry();
    }
    return QWidget::event(event);
  }

  void paintEvent(QPaintEvent *event) override {
    event->accept();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const auto colors = palette();
    draw_crosshair_segments(
        painter, size(),
        QPen(colors.shadow().color(),
             std::max<qreal>(1.0, 3.0 * ui_scale_metrics_.factor()),
             Qt::SolidLine, Qt::RoundCap),
        ui_scale_metrics_);
    draw_crosshair_segments(
        painter, size(),
        QPen(colors.brightText().color(),
             std::max<qreal>(1.0, ui_scale_metrics_.factor()), Qt::SolidLine,
             Qt::RoundCap),
        ui_scale_metrics_);
  }

private:
  void refresh_scaled_geometry() {
    setFixedSize(ui_scale_metrics_.size(QSize{28, 28}));
    setMask(crosshair_window_mask(size(), ui_scale_metrics_));
    setProperty("quaderFlyMaskRevision",
                property("quaderFlyMaskRevision").toInt() + 1);
  }

  QtUiScaleMetrics ui_scale_metrics_;
};

QString pane_suffix(ViewportPaneId pane_id) {
  const std::string_view id = viewport_pane_stable_id(pane_id);
  return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

std::string fly_speed_label(float fly_speed,
                            const LocalizationService *localization) {
  const std::string speed_value =
      qt_format_number(fly_speed, QLocale::c()).toStdString();
  if (localization != nullptr) {
    return localization->format(
        "viewport.fly_navigation.speed_with_reset",
        "Speed {speed} (Middle Mouse to reset)", {{"speed", speed_value}});
  }
  return std::string("Speed ") + speed_value +
         " (Middle Mouse to reset)";
}

} // namespace

QtViewportFlyNavigationHudPresentation
qt_viewport_fly_navigation_hud_presentation(
    bool fly_hud_active, bool fly_speed_is_default, float fly_speed,
    const LocalizationService *localization) {
  QtViewportFlyNavigationHudPresentation presentation{
      .crosshair_visible = fly_hud_active,
      .speed_visible = fly_hud_active && !fly_speed_is_default,
  };
  if (presentation.speed_visible) {
    presentation.speed_label = fly_speed_label(fly_speed, localization);
  }
  return presentation;
}

QtViewportFlyNavigationHud::QtViewportFlyNavigationHud(
    QWidget &parent, ViewportPaneId pane_id)
    : QObject(&parent), crosshair_(new QtViewportFlyCrosshair(&parent)),
      speed_label_(new QLabel(&parent)), bounds_(parent.rect()) {
  const QString suffix = pane_suffix(pane_id);
  crosshair_->setObjectName(
      QStringLiteral("viewportFlyCrosshair_%1").arg(suffix));
  crosshair_->hide();

  speed_label_->setObjectName(
      QStringLiteral("viewportFlySpeed_%1").arg(suffix));
  speed_label_->setTextFormat(Qt::PlainText);
  speed_label_->setAlignment(Qt::AlignCenter);
  speed_label_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
  speed_label_->setMargin(ui_scale_metrics_.px(6));
  speed_label_->setAutoFillBackground(true);
  speed_label_->setAttribute(Qt::WA_NativeWindow);
  speed_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  speed_label_->hide();
  set_bounds(bounds_);
}

void QtViewportFlyNavigationHud::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  static_cast<QtViewportFlyCrosshair *>(crosshair_)->apply_ui_scale(metrics);
  const int margin = metrics.px(6);
  if (speed_label_->margin() != margin) {
    speed_label_->setMargin(margin);
  }
  update_geometry();
}

void QtViewportFlyNavigationHud::set_bounds(const QRect &bounds) {
  if (bounds_initialized_ &&
      !qt_viewport_fly_navigation_hud_bounds_refresh_required(bounds_,
                                                               bounds)) {
    return;
  }
  bounds_ = bounds;
  bounds_initialized_ = true;
  update_geometry();
}

void QtViewportFlyNavigationHud::update_geometry() {
  const QSize crosshair_size = crosshair_->size();
  crosshair_->move(bounds_.x() +
                       (bounds_.width() - crosshair_size.width()) / 2,
                   bounds_.y() +
                       (bounds_.height() - crosshair_size.height()) / 2);

  speed_label_->adjustSize();
  const QSize speed_size = speed_label_->size();
  const int bottom_inset = ui_scale_metrics_.px(10);
  speed_label_->move(
      bounds_.x() + (bounds_.width() - speed_size.width()) / 2,
      std::max(bounds_.y(), bounds_.bottom() - speed_size.height() -
                                bottom_inset + 1));
}

void QtViewportFlyNavigationHud::present(
    bool fly_hud_active, bool fly_speed_is_default, float fly_speed,
    const LocalizationService *localization) {
  const std::string_view locale =
      localization != nullptr ? localization->current_locale()
                              : std::string_view{};
  if (input_valid_ && input_fly_hud_active_ == fly_hud_active &&
      input_fly_speed_is_default_ == fly_speed_is_default &&
      input_speed_ == fly_speed && input_localization_ == localization &&
      input_locale_ == locale) {
    return;
  }

  input_fly_hud_active_ = fly_hud_active;
  input_fly_speed_is_default_ = fly_speed_is_default;
  input_speed_ = fly_speed;
  input_localization_ = localization;
  input_locale_.assign(locale);
  input_valid_ = true;
  apply_presentation(qt_viewport_fly_navigation_hud_presentation(
      fly_hud_active, fly_speed_is_default, fly_speed, localization));
}

void QtViewportFlyNavigationHud::present(
    const QtViewportFlyNavigationHudPresentation &presentation) {
  input_valid_ = false;
  apply_presentation(presentation);
}

void QtViewportFlyNavigationHud::apply_presentation(
    const QtViewportFlyNavigationHudPresentation &presentation) {
  if (!qt_viewport_fly_navigation_hud_presentation_refresh_required(
          presentation_, presentation)) {
    return;
  }

  const bool crosshair_visibility_changed =
      presentation_.crosshair_visible != presentation.crosshair_visible;
  const bool speed_visibility_changed =
      presentation_.speed_visible != presentation.speed_visible;
  bool speed_label_changed = false;
  if (presentation.speed_visible) {
    const QString label = QString::fromStdString(presentation.speed_label);
    if (speed_label_->text() != label) {
      speed_label_->setText(label);
      speed_label_changed = true;
    }
  }

  presentation_ = presentation;
  if (speed_label_changed) {
    update_geometry();
  }
  if (crosshair_visibility_changed) {
    crosshair_->setVisible(presentation.crosshair_visible);
  }
  if (speed_visibility_changed) {
    speed_label_->setVisible(presentation.speed_visible);
  }
  if (crosshair_visibility_changed && presentation.crosshair_visible) {
    crosshair_->raise();
  }
  if (speed_visibility_changed && presentation.speed_visible) {
    speed_label_->raise();
  }
}

void QtViewportFlyNavigationHud::hide() {
  input_valid_ = false;
  apply_presentation({});
}

} // namespace quader
