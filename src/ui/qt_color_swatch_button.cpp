////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_color_swatch_button.hpp"

#include <QPalette>
#include <QSizePolicy>

#include <utility>

namespace quader {

QtColorSwatchButton::QtColorSwatchButton(QWidget *parent)
    : QPushButton(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void QtColorSwatchButton::set_swatch_color(QColor color) {
  const QString hex = color.name(QColor::HexArgb).toUpper();
  setToolTip(hex);
  setAccessibleDescription(hex);
  swatch_color_ = std::move(color);
  QPalette button_palette = palette();
  button_palette.setColor(QPalette::Button, swatch_color_);
  setPalette(button_palette);
  update();
}

} // namespace quader
