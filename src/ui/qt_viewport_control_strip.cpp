////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_control_strip.hpp"

#include "ui/qt_application_style.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"

#include <QAction>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyleOption>
#include <QToolButton>

namespace quader {
namespace {

class QtViewportStripSeparator final : public QWidget {
public:
  explicit QtViewportStripSeparator(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    const qreal hairline = 1.0 / devicePixelRatioF();
    painter.fillRect(QRectF(width() / 2.0, 0.0, hairline, height()),
                     palette().brush(QPalette::Mid));
  }
};

} // namespace

QtViewportControlStrip::QtViewportControlStrip(QWidget *parent)
    : QWidget(parent), layout_(new QHBoxLayout(this)) {
  setObjectName(QStringLiteral("viewportControlStrip"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  layout_->setContentsMargins(metrics_.margins(QMargins(4, 4, 4, 4)));
  layout_->setSpacing(metrics_.px(2));
  layout_->addStretch();
  button_style_ = &install_native_toolbar_button_presentation(*this);
}

QtViewportControlStrip::~QtViewportControlStrip() {
  // Actions are added after the shared style. Destroy their widgets explicitly
  // while that host-owned style is still alive.
  for (const auto &button : buttons_) {
    delete button.data();
  }
}

void QtViewportControlStrip::add_action(QAction &action) {
  auto *button = new QToolButton(this);
  button->setObjectName(QStringLiteral("viewportStrip_%1").arg(action.objectName()));
  button->setDefaultAction(&action);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  configure_native_toolbar_button_face(*button, action, *button_style_);
  scale_button(*button);
  button->setVisible(action.isVisible());
  QObject::connect(&action, &QAction::changed, button, [button, &action]() {
    button->setVisible(action.isVisible());
  });
  layout_->insertWidget(layout_->count() - 1, button, 0, Qt::AlignVCenter);
  buttons_.push_back(button);
}

void QtViewportControlStrip::add_separator() {
  auto *separator = new QtViewportStripSeparator(this);
  separator->setFixedSize(metrics_.size(QSize(8, 20)));
  layout_->insertWidget(layout_->count() - 1, separator, 0, Qt::AlignVCenter);
  separators_.push_back(separator);
}

void QtViewportControlStrip::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (metrics_.factor() == metrics.factor() &&
      metrics_.revision() == metrics.revision()) {
    return;
  }
  metrics_ = metrics;
  layout_->setContentsMargins(metrics_.margins(QMargins(4, 4, 4, 4)));
  layout_->setSpacing(metrics_.px(2));
  for (const auto &button : buttons_) {
    if (button != nullptr) {
      scale_button(*button);
    }
  }
  for (auto *separator : separators_) {
    separator->setFixedSize(metrics_.size(QSize(8, 20)));
  }
  // Release the previous scale's layout minimum before a parent resizes us.
  layout_->invalidate();
  layout_->activate();
  updateGeometry();
}

void QtViewportControlStrip::scale_button(QToolButton &button) {
  apply_native_toolbar_button_face_ui_scale(button, metrics_);
  button.setFixedSize(metrics_.size(QSize(28, 28)));
  button.setIconSize(metrics_.size(QSize(24, 24)));
}

void QtViewportControlStrip::paintEvent(QPaintEvent *) {
  QStyleOption option;
  option.initFrom(this);
  QPainter painter(this);
  painter.fillRect(rect(), option.palette.brush(
                               qt_application_style_active(&option)
                                   ? QPalette::AlternateBase
                                   : QPalette::Window));
  const qreal hairline = 1.0 / devicePixelRatioF();
  painter.fillRect(QRectF(0.0, height() - hairline, width(), hairline),
                   option.palette.brush(QPalette::Mid));
}

} // namespace quader
