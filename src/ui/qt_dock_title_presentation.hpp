////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

class QDockWidget;

namespace quader {

enum class QtDockTitleGlyph { Close, Float };

/** Returns whether one dock-title surface needs a light foreground glyph. */
[[nodiscard]] bool
qt_dock_title_surface_uses_light_glyph(const QColor &surface);

/** Builds a resolution-independent dock glyph from palette-aware geometry. */
[[nodiscard]] QIcon qt_dock_title_glyph_icon(
    QtDockTitleGlyph glyph, const QPalette &palette);

/** Builds the scoped, palette-role-based dock title presentation. */
[[nodiscard]] QString
qt_dock_title_style_sheet(const QPalette &palette);

/** Builds the dock title presentation from immutable 100% design metrics. */
[[nodiscard]] QString qt_dock_title_style_sheet(
    const QPalette &palette, const QtUiScaleMetrics &metrics);

/** Installs scoped dock styling that follows application palette transitions. */
void install_palette_aware_dock_title_presentation(QDockWidget &dock);

/** Reflows an installed dock title without changing its palette or state. */
void apply_ui_scale_to_palette_aware_dock_title_presentation(
    QDockWidget &dock, const QtUiScaleMetrics &metrics);

} // namespace quader
