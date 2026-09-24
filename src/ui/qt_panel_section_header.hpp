////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_application_style.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QStyleOptionFocusRect>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>

#include <algorithm>

namespace quader {

/// A native disclosure target containing both the arrow and section title.
class QtPanelSectionHeader final : public QToolButton {
public:
  explicit QtPanelSectionHeader(QWidget *parent = nullptr) : QToolButton(parent) {
    setAutoRaise(true);
    setAutoFillBackground(false);
    setBackgroundRole(QPalette::Window);
    setCheckable(true);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // The host settles drafts before giving a mouse-clicked header focus.
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    update_metrics();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    metrics_ = metrics;
    update_metrics();
  }

  void set_leading_spacing(int design_pixels) {
    const int spacing = std::max(0, design_pixels);
    if (leading_spacing_ != spacing) {
      leading_spacing_ = spacing;
      update_metrics();
      update();
    }
  }

  [[nodiscard]] int leading_spacing() const { return leading_spacing_; }

  [[nodiscard]] QRect content_rect() const {
    return rect().adjusted(0, metrics_.px(leading_spacing_), 0, 0);
  }

  QSize sizeHint() const override {
    QSize hint = QToolButton::sizeHint();
    hint.rwidth() += metrics_.px(16);
    hint.setHeight(std::max(hint.height(), header_height()) +
                   metrics_.px(leading_spacing_));
    return hint;
  }

  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void changeEvent(QEvent *event) override {
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::FontChange ||
        event->type() == QEvent::StyleChange) {
      update_metrics();
    }
  }

  void paintEvent(QPaintEvent *) override {
    QStyleOptionToolButton option;
    initStyleOption(&option);
    // The arrow conveys expansion; reserve the pressed surface for input.
    option.state &= ~QStyle::State_On;
    if (!isDown()) {
      option.state |= QStyle::State_Raised;
    }
    option.font.setWeight(QFont::DemiBold);
    option.fontMetrics = QFontMetrics(option.font);
    option.rect = content_rect();
    for (const QPalette::ColorGroup group :
         {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
      option.palette.setColor(group, QPalette::ButtonText,
                              option.palette.color(group, QPalette::Text));
    }
    QStylePainter painter(this);
    painter.setFont(option.font);
    const bool themed = qt_application_style_active(&option);
    if (themed) {
      const QPalette::ColorGroup group =
          isEnabled() ? (isActiveWindow() ? QPalette::Active
                                         : QPalette::Inactive)
                      : QPalette::Disabled;
      QColor fill = palette().color(group, QPalette::Button);
      const QColor tint = palette().color(
          group, isDown() ? QPalette::Shadow : QPalette::WindowText);
      const qreal amount = isDown() ? 0.16 : (underMouse() ? 0.035 : 0.0);
      fill = QColor::fromRgbF(
          fill.redF() + (tint.redF() - fill.redF()) * amount,
          fill.greenF() + (tint.greenF() - fill.greenF()) * amount,
          fill.blueF() + (tint.blueF() - fill.blueF()) * amount);
      // The enclosing section owns its one outline; leave it visible beneath
      // this child, including the rounded upper corners and the leading gap.
      const qreal inset = 1.0 / std::max<qreal>(1.0, devicePixelRatioF());
      const QRectF surface = QRectF(option.rect).adjusted(
          inset, inset, -inset, -inset);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(Qt::NoPen);
      painter.setBrush(fill);
      QPainterPath header_surface;
      header_surface.setFillRule(Qt::WindingFill);
      const qreal radius = metrics_.px(6.0);
      header_surface.addRoundedRect(surface, radius, radius);
      if (isChecked()) {
        header_surface.addRect(QRectF(surface.left(), surface.bottom() - radius,
                                     surface.width(), radius));
      }
      painter.drawPath(header_surface.simplified());
      if (isChecked()) {
        painter.setPen(QPen(palette().color(group, QPalette::Midlight), inset));
        painter.drawLine(QPointF(surface.left(), surface.bottom()),
                         QPointF(surface.right(), surface.bottom()));
      }
      option.rect.adjust(metrics_.px(8), 0, -metrics_.px(8), 0);
      painter.drawControl(QStyle::CE_ToolButtonLabel, option);
    } else {
      painter.drawComplexControl(QStyle::CC_ToolButton, option);
    }
    if (hasFocus()) {
      if (themed) {
        const qreal line_width =
            1.0 / std::max<qreal>(1.0, devicePixelRatioF());
        const qreal inset = metrics_.px(2.0) + line_width * 0.5;
        const QRectF focus_rect = QRectF(content_rect()).adjusted(
            inset, inset, -inset, -inset);
        const QPalette::ColorGroup group =
            isActiveWindow() ? QPalette::Active : QPalette::Inactive;
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(group, QPalette::Accent),
                            line_width));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(focus_rect, metrics_.px(4.0),
                                 metrics_.px(4.0));
      } else {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        focus.state |= QStyle::State_KeyboardFocusChange;
        const int inset = metrics_.px(3);
        focus.rect = content_rect().adjusted(inset, inset, -inset, -inset);
        focus.backgroundColor = palette().color(backgroundRole());
        painter.drawPrimitive(QStyle::PE_FrameFocusRect, focus);
      }
    }
  }

  bool hitButton(const QPoint &position) const override {
    return content_rect().contains(position) && QToolButton::hitButton(position);
  }

  void keyPressEvent(QKeyEvent *event) override {
    if (event->modifiers() == Qt::NoModifier) {
      switch (event->key()) {
      case Qt::Key_Return:
      case Qt::Key_Enter:
        click();
        event->accept();
        return;
      case Qt::Key_Left:
      case Qt::Key_Right:
        setChecked(event->key() == Qt::Key_Right);
        event->accept();
        return;
      default:
        break;
      }
    }
    QToolButton::keyPressEvent(event);
  }

private:
  [[nodiscard]] int header_height() const {
    return std::max(metrics_.px(30),
                    fontMetrics().height() + metrics_.px(12));
  }

  void update_metrics() {
    const QSize icon_size = metrics_.size(QSize(16, 16));
    if (iconSize() != icon_size) {
      setIconSize(icon_size);
    }
    const int height = header_height() + metrics_.px(leading_spacing_);
    if (minimumHeight() != height) {
      setMinimumHeight(height);
    }
    updateGeometry();
  }

  QtUiScaleMetrics metrics_;
  int leading_spacing_ = 0;
};

} // namespace quader
