////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_numeric_spin_box.hpp"

#include <QSpinBox>

namespace quader {

/// Integer editor with the application-wide numeric display and scrub gesture.
class QtSpinBox : public QSpinBox {
public:
  explicit QtSpinBox(QWidget *parent = nullptr) : QSpinBox(parent) {
    qt_install_numeric_spin_box(*this, true);
  }

  [[nodiscard]] QSize sizeHint() const override {
    return qt_numeric_spin_box_size_hint(*this, QSpinBox::sizeHint());
  }

  [[nodiscard]] QSize minimumSizeHint() const override {
    return qt_numeric_spin_box_size_hint(*this, QSpinBox::minimumSizeHint());
  }
};

} // namespace quader
