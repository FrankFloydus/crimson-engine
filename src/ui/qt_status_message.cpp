////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_status_message.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>

#include <utility>

namespace quader {

QtStatusMessage::QtStatusMessage(QWidget *parent)
    : QLabel(parent),
      error_sound_callback_([]() { QApplication::beep(); }) {
  setTextFormat(Qt::PlainText);
  setContentsMargins(6, 0, 6, 0);
}

void QtStatusMessage::present(const QString &text, bool error,
                              bool announce) {
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

void QtStatusMessage::set_error_sound_callback(
    std::function<void()> callback) {
  error_sound_callback_ = std::move(callback);
}

} // namespace quader
