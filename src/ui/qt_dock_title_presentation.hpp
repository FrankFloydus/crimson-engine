////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

class QDockWidget;

namespace quader {

/** Returns whether one dock-title surface needs packaged light glyphs. */
[[nodiscard]] bool qt_dock_title_surface_uses_packaged_light_glyphs(
    const QColor &surface);

/** Builds the scoped, palette-role-based dock title presentation. */
[[nodiscard]] QString
qt_dock_title_style_sheet(const QPalette &palette);

/** Installs scoped dock styling that follows application palette transitions. */
void install_palette_aware_dock_title_presentation(QDockWidget &dock);

} // namespace quader
