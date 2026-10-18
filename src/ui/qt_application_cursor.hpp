////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QCursor>
#include <QPixmap>

class QApplication;

namespace quader {

/** Selects one authored application pointer image. */
enum class QtApplicationCursorVariant {
  Default,
  SelectionAdd,
  SelectionSubtract,
  BoxCutter,
};

inline constexpr int kQtApplicationCursorCanvasLogicalExtent = 32;

[[nodiscard]] QPixmap
qt_application_cursor_pixmap(QtApplicationCursorVariant variant,
                             qreal device_pixel_ratio);

[[nodiscard]] QCursor
qt_application_cursor(QtApplicationCursorVariant variant,
                      qreal device_pixel_ratio);

void install_qt_application_default_cursor(QApplication &application);

} // namespace quader
