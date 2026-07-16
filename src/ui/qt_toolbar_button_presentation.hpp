////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

class QAction;
class QStyle;
class QToolBar;

namespace quader {

/**
 * Creates one toolbar-scoped Fusion presentation shared by its action buttons.
 * Install it after adding the toolbar actions so it outlives their widgets.
 */
QStyle &install_native_toolbar_button_presentation(QToolBar &toolbar);

/** Gives one action a persistent native face and accessible presentation. */
void configure_native_toolbar_button_face(QToolBar &toolbar, QAction &action,
                                          QStyle &presentation);

} // namespace quader
