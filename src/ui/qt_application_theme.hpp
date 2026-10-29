////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QPalette>

namespace quader {

enum class QtLogMessageSeverity { Subdued, Information, Warning, Error };

/** Resolves readable log text through the live palette and contrast policy. */
[[nodiscard]] QColor qt_log_message_color(QtLogMessageSeverity severity,
                                         const QPalette &palette,
                                         bool themed);

/** Orange outline for the current material. */
[[nodiscard]] QColor qt_current_item_outline_color();

/** Resolves subordinate disabled button labels without changing native roles. */
[[nodiscard]] QColor qt_muted_disabled_button_text_color(
    const QPalette &palette, bool themed);

/**
 * Builds Quader's dark colors with logo-purple accents on Fusion's contract.
 *
 * Fusion continues to own metrics and interaction behavior. The returned
 * palette supplies one deterministic application-wide color source.
 */
[[nodiscard]] QPalette
qt_obsidian_application_palette(const QPalette &fusion_palette);

/**
 * Returns an unresolved palette when Windows requests high contrast so Qt can
 * keep resolving live platform colors. Otherwise returns Quader's dark
 * palette.
 */
[[nodiscard]] QPalette qt_application_palette_for_contrast_preference(
    const QPalette &fusion_palette, Qt::ContrastPreference preference);

/** Applies one explicit contrast-policy transition for startup and live use. */
void apply_qt_application_theme_for_contrast_preference(
    const QPalette &fusion_palette, Qt::ContrastPreference preference);

/**
 * Installs the palette appropriate for the current accessibility preference and
 * tracks later contrast-preference changes. Call once after selecting Fusion
 * and before constructing application widgets.
 */
void apply_qt_obsidian_application_theme(const QPalette &fusion_palette);

} // namespace quader
