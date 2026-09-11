////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

class QTreeView;

namespace quader {

class QtUiScaleMetrics;

/** Installs the shared relationship-line presentation while retaining Qt arrows. */
void install_qt_tree_relationship_presentation(QTreeView &tree);

/** Reapplies authored tree metrics from one immutable interface-scale snapshot. */
void apply_ui_scale_to_qt_tree_relationship_presentation(
    QTreeView &tree, const QtUiScaleMetrics &metrics);

} // namespace quader
