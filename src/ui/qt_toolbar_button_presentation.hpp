////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QPalette>
#include <QStyle>

class QAction;
class QToolBar;
class QToolButton;
class QWidget;

namespace quader {

class QtUiScaleMetrics;

/** Resolves the palette group used to paint one toolbar-button state. */
[[nodiscard]] QPalette::ColorGroup
qt_toolbar_button_color_group(QStyle::State state);

/**
 * Creates one host-scoped Fusion presentation shared by its action buttons.
 * Install it after creating the buttons so it outlives their widgets.
 */
QStyle &install_native_toolbar_button_presentation(QWidget &host);
QStyle &install_native_toolbar_button_presentation(QToolBar &toolbar);

/** Gives one action native interaction states and accessible presentation. */
void configure_native_toolbar_button_face(QToolButton &button, QAction &action,
                                          QStyle &presentation);
void configure_native_toolbar_button_face(QToolBar &toolbar, QAction &action,
                                          QStyle &presentation);

/** Recomputes one action button's logical hit target from 100% constants. */
void apply_native_toolbar_button_face_ui_scale(
    QToolButton &button, const QtUiScaleMetrics &metrics);
void apply_native_toolbar_button_face_ui_scale(
    QToolBar &toolbar, QAction &action, const QtUiScaleMetrics &metrics);

} // namespace quader
