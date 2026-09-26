////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_status_message.hpp"

#include <QApplication>
#include <QColor>
#include <QGraphicsOpacityEffect>
#include <QPalette>
#include <QPropertyAnimation>
#include <QTimer>

#include <utility>

namespace quader {

QtStatusMessage::QtStatusMessage(QWidget *parent)
    : QLabel(parent),
      error_sound_callback_([]() { QApplication::beep(); }) {
  setTextFormat(Qt::PlainText);
  setContentsMargins(6, 0, 6, 0);

  dismissal_timer_ = new QTimer(this);
  dismissal_timer_->setSingleShot(true);
  dismissal_timer_->setTimerType(Qt::PreciseTimer);
  dismissal_timer_->setInterval(5000);

  opacity_effect_ = new QGraphicsOpacityEffect(this);
  opacity_effect_->setOpacity(1.0);
  opacity_effect_->setEnabled(false);
  setGraphicsEffect(opacity_effect_);
  fade_animation_ = new QPropertyAnimation(opacity_effect_, "opacity", this);
  fade_animation_->setStartValue(1.0);
  fade_animation_->setEndValue(0.0);
  fade_animation_->setDuration(200);
  connect(dismissal_timer_, &QTimer::timeout, this, [this]() {
    opacity_effect_->setEnabled(true);
    fade_animation_->start();
  });
  connect(fade_animation_, &QPropertyAnimation::finished, this, [this]() {
    opacity_effect_->setEnabled(false);
    opacity_effect_->setOpacity(1.0);
    auto dismissed = std::move(dismissed_callback_);
    dismissed_callback_ = {};
    if (dismissed) {
      dismissed();
    }
  });
}

void QtStatusMessage::present(const QString &text, bool error,
                              bool announce) {
  if (text.isEmpty()) {
    cancel_dismissal();
  }
  if (this->text() != text) {
    setText(text);
  }
  if (error_ != error) {
    error_ = error;
    if (error_) {
      QPalette error_palette;
      error_palette.setColor(QPalette::All, QPalette::Window,
                             QColor(179, 38, 30));
      error_palette.setColor(QPalette::All, QPalette::WindowText,
                             QColor(Qt::white));
      setPalette(error_palette);
    } else {
      setPalette(QPalette{});
    }
    setAutoFillBackground(error_);
  }
  if (error && announce && error_sound_callback_) {
    error_sound_callback_();
  }
}

void QtStatusMessage::restart_dismissal(std::function<void()> dismissed) {
  cancel_dismissal();
  if (text().isEmpty()) {
    return;
  }
  dismissed_callback_ = std::move(dismissed);
  dismissal_timer_->start();
}

void QtStatusMessage::cancel_dismissal() {
  dismissal_timer_->stop();
  fade_animation_->stop();
  opacity_effect_->setEnabled(false);
  opacity_effect_->setOpacity(1.0);
  dismissed_callback_ = {};
}

bool QtStatusMessage::dismissal_active() const noexcept {
  return dismissal_timer_->isActive() ||
         fade_animation_->state() != QAbstractAnimation::Stopped;
}

void QtStatusMessage::set_error_sound_callback(
    std::function<void()> callback) {
  error_sound_callback_ = std::move(callback);
}

} // namespace quader
