////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_fly_navigation_hud.hpp"

#include "core/localization_service.hpp"
#include "viewport/viewport_layout.hpp"

#include <QFrame>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace quader {
namespace {

/// Draws the fly-navigation crosshair exclusively from the active Qt palette.
class QtViewportFlyCrosshair final : public QWidget {
public:
  explicit QtViewportFlyCrosshair(QWidget *parent) : QWidget(parent) {
    setFixedSize(28, 28);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    event->accept();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const qreal center_x = static_cast<qreal>(width()) * 0.5;
    const qreal center_y = static_cast<qreal>(height()) * 0.5;
    const auto colors = palette();
    constexpr qreal kGap = 4.0;
    constexpr qreal kLength = 9.0;
    const auto draw_segments = [&](const QPen &pen) {
      painter.setPen(pen);
      painter.drawLine(QPointF{center_x - kLength, center_y},
                       QPointF{center_x - kGap, center_y});
      painter.drawLine(QPointF{center_x + kGap, center_y},
                       QPointF{center_x + kLength, center_y});
      painter.drawLine(QPointF{center_x, center_y - kLength},
                       QPointF{center_x, center_y - kGap});
      painter.drawLine(QPointF{center_x, center_y + kGap},
                       QPointF{center_x, center_y + kLength});
    };

    draw_segments(QPen(colors.shadow().color(), 3.0,
                       Qt::SolidLine, Qt::RoundCap));
    draw_segments(QPen(colors.highlightedText().color(), 1.0,
                       Qt::SolidLine, Qt::RoundCap));
  }
};

QString pane_suffix(ViewportPaneId pane_id) {
  const std::string_view id = viewport_pane_stable_id(pane_id);
  return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

std::string fly_speed_label(float fly_speed,
                            const LocalizationService *localization) {
  char speed_value[32]{};
  std::snprintf(speed_value, sizeof(speed_value), "%.1f", fly_speed);
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
  speed_label_->setMargin(6);
  speed_label_->setAutoFillBackground(true);
  speed_label_->setAttribute(Qt::WA_NativeWindow);
  speed_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  speed_label_->hide();
  set_bounds(bounds_);
}

void QtViewportFlyNavigationHud::set_bounds(const QRect &bounds) {
  bounds_ = bounds;
  const QSize crosshair_size = crosshair_->size();
  crosshair_->move(bounds_.x() +
                       (bounds_.width() - crosshair_size.width()) / 2,
                   bounds_.y() +
                       (bounds_.height() - crosshair_size.height()) / 2);

  speed_label_->adjustSize();
  const QSize speed_size = speed_label_->size();
  constexpr int kBottomInset = 10;
  speed_label_->move(
      bounds_.x() + (bounds_.width() - speed_size.width()) / 2,
      std::max(bounds_.y(), bounds_.bottom() - speed_size.height() -
                                kBottomInset + 1));
}

void QtViewportFlyNavigationHud::present(
    const QtViewportFlyNavigationHudPresentation &presentation) {
  crosshair_->setVisible(presentation.crosshair_visible);
  if (presentation.speed_visible) {
    const QString label = QString::fromStdString(presentation.speed_label);
    if (speed_label_->text() != label) {
      speed_label_->setText(label);
    }
  }
  speed_label_->setVisible(presentation.speed_visible);
  set_bounds(bounds_);
  if (presentation.crosshair_visible) {
    crosshair_->raise();
  }
  if (presentation.speed_visible) {
    speed_label_->raise();
  }
}

void QtViewportFlyNavigationHud::hide() {
  crosshair_->hide();
  speed_label_->hide();
}

} // namespace quader
