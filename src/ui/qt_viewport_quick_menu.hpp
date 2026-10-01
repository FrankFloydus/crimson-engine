////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"

class QPoint;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class ViewportInputRouter;

/**
 * Presents the viewport Add menu through a stack-local Qt menu. Enabled leaves
 * dispatch stable public commands only after the nested popup loop returns.
 */
class QtViewportQuickMenuPresenter final {
public:
  [[nodiscard]] bool show(
      QWidget &focus_parent, const QPoint &global_point,
      ViewportPaneId initiating_pane, QuaderEditorApi &editor,
      ViewportInputRouter &input_router, LocalizationService &localization,
      ViewportRuntimeBindingToken runtime_binding = {}) const;
};

} // namespace quader
