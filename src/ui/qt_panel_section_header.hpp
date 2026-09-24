////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QEvent>
#include <QKeyEvent>
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
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Button);
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
    option.font.setBold(true);
    option.fontMetrics = QFontMetrics(option.font);
    QStylePainter painter(this);
    painter.setFont(option.font);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
    if (hasFocus()) {
      QStyleOptionFocusRect focus;
      focus.initFrom(this);
      focus.state |= QStyle::State_KeyboardFocusChange;
      const int inset = metrics_.px(3);
      focus.rect = rect().adjusted(inset, inset, -inset, -inset);
      focus.backgroundColor = palette().color(QPalette::Button);
      painter.drawPrimitive(QStyle::PE_FrameFocusRect, focus);
    }
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
  void update_metrics() {
    const QSize icon_size = metrics_.size(QSize(16, 16));
    if (iconSize() != icon_size) {
      setIconSize(icon_size);
    }
    const int height = std::max(metrics_.px(26),
                                fontMetrics().height() + metrics_.px(10));
    if (minimumHeight() != height) {
      setMinimumHeight(height);
    }
  }

  QtUiScaleMetrics metrics_;
};

} // namespace quader
