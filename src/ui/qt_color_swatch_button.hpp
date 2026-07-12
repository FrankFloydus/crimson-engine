////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QColor>
#include <QPushButton>

namespace quader {

/**
 * Stock Qt push button whose Fusion-owned button brush previews a color. The
 * style remains responsible for the complete rounded face, hover, press, and
 * focus states.
 */
class QtColorSwatchButton final : public QPushButton {
public:
  explicit QtColorSwatchButton(QWidget *parent = nullptr);

  void set_swatch_color(QColor color);
  [[nodiscard]] const QColor &swatch_color() const { return swatch_color_; }

private:
  QColor swatch_color_ = Qt::transparent;
};

} // namespace quader
