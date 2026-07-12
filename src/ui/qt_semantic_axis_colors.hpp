////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/interactions/tool_identity.hpp"

#include <QColor>
#include <QLabel>
#include <QPalette>

namespace quader {

[[nodiscard]] inline QColor
qt_semantic_axis_color(quader_editor_core::ViewAxis axis) {
  switch (axis) {
  case quader_editor_core::ViewAxis::X:
    return QColor(235, 76, 82);
  case quader_editor_core::ViewAxis::Y:
    return QColor(90, 205, 118);
  case quader_editor_core::ViewAxis::Z:
    return QColor(78, 142, 255);
  }
  return QColor(Qt::white);
}

inline void apply_qt_semantic_axis_label_color(
    QLabel &label, quader_editor_core::ViewAxis axis) {
  QPalette palette = label.palette();
  palette.setColor(QPalette::WindowText, qt_semantic_axis_color(axis));
  label.setPalette(palette);
}

} // namespace quader
