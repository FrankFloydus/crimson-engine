////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QStyle>

namespace quader {

class QtUiScaleMetrics;

/** Compact visible checkbox geometry; Qt retains native control interaction. */
inline constexpr int kQtCheckSwitchWidth = 26;
inline constexpr int kQtCheckSwitchHeight = 12;

/** Identifies document tabs for compact native close glyphs and alignment. */
inline constexpr char kQtConnectedTabSurfaceProperty[] =
    "quaderConnectedTabSurface";

/** Selects square property editors with quiet resting boundaries. */
inline constexpr char kQtSquareEditorSurfaceProperty[] =
    "quaderSquareEditorSurface";

/** Marks a square editor's leading strip with an integer ViewAxis value. */
inline constexpr char kQtEditorAxisProperty[] = "quaderEditorAxis";

/** Lets a framed owner paint the surface behind its frameless text editor. */
inline constexpr char kQtParentPaintedEditorSurfaceProperty[] =
    "quaderParentPaintedEditorSurface";

/** Uses theme-owned subordinate disabled labels for dense command lists. */
inline constexpr char kQtMutedDisabledButtonTextProperty[] =
    "quaderMutedDisabledButtonText";

/** Paint-only adapters shared by the application and local Fusion proxies. */
[[nodiscard]] bool qt_application_style_active(const QStyleOption *option);

[[nodiscard]] bool qt_draw_application_primitive(
    QStyle::PrimitiveElement element, const QStyleOption *option,
    QPainter *painter, const QStyle &style, const QtUiScaleMetrics &metrics,
    const QWidget *widget = nullptr);

[[nodiscard]] bool qt_draw_application_control(
    QStyle::ControlElement element, const QStyleOption *option,
    QPainter *painter, const QStyle &style, const QtUiScaleMetrics &metrics,
    const QWidget *widget = nullptr);

[[nodiscard]] bool qt_draw_application_complex_control(
    QStyle::ComplexControl control, const QStyleOptionComplex *option,
    QPainter *painter, const QStyle &style, const QtUiScaleMetrics &metrics,
    const QWidget *widget = nullptr);

} // namespace quader
