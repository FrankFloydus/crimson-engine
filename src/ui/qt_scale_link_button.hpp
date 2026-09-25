////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QEvent>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>

#include <algorithm>

namespace quader {

/// Native proportion-link toggle; the host owns the edited values and lifetime.
class QtScaleLinkButton final : public QToolButton {
public:
  explicit QtScaleLinkButton(QWidget *parent = nullptr) : QToolButton(parent) {
    setCheckable(true);
    setAutoRaise(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QObject::connect(this, &QToolButton::toggled, this,
                     [this]() { update_glyph(); });
    update_glyph();
    update_metrics();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    if (metrics_.revision() == metrics.revision() &&
        metrics_.factor() == metrics.factor()) {
      return;
    }
    metrics_ = metrics;
    update_metrics();
  }

  QSize sizeHint() const override {
    const QSize native = QToolButton::sizeHint();
    const int extent = std::max({metrics_.px(24), native.width(),
                                 native.height(),
                                 fontMetrics().height() + metrics_.px(6)});
    return {extent, extent};
  }

  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void changeEvent(QEvent *event) override {
    QToolButton::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::EnabledChange:
    case QEvent::ActivationChange:
      update_glyph();
      break;
    case QEvent::FontChange:
    case QEvent::StyleChange:
      update_glyph();
      update_metrics();
      break;
    default:
      break;
    }
  }

  void paintEvent(QPaintEvent *) override {
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.icon = {};
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
    // The live palette already supplies disabled contrast. Paint this exact
    // color without the icon engine's generic disabled-opacity approximation.
    icon().paint(&painter,
                 QStyle::alignedRect(layoutDirection(), Qt::AlignCenter,
                                     iconSize(), rect()),
                 Qt::AlignCenter, QIcon::Normal);
  }

private:
  void update_metrics() {
    setIconSize(metrics_.size(QSize(16, 16)));
    setFixedSize(sizeHint());
    updateGeometry();
  }

  void update_glyph() {
    const QPalette::ColorGroup group =
        !isEnabled() ? QPalette::Disabled
                     : (isActiveWindow() ? QPalette::Active : QPalette::Inactive);
    setIcon(qt_scalable_glyph_icon(
        isChecked() ? QtScalableGlyph::ScaleLinked
                    : QtScalableGlyph::ScaleUnlinked,
        palette().color(group, QPalette::ButtonText)));
  }

  QtUiScaleMetrics metrics_;
};

} // namespace quader
