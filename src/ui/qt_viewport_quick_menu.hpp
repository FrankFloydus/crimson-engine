////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"
#include "viewport/viewport_workspace_service.hpp"

class QPoint;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class ViewportInputRouter;

/** Selects one cursor-position viewport launcher owned by semantic input. */
enum class QtViewportQuickMenuKind {
  Add,
  Shading,
};

/**
 * Presents viewport quick menus through stack-local Qt menus. Enabled leaves
 * dispatch stable public commands only after the nested popup loop returns.
 */
class QtViewportQuickMenuPresenter final {
public:
  [[nodiscard]] bool show(
      QtViewportQuickMenuKind kind, QWidget &focus_parent,
      const QPoint &global_point, ViewportPaneId initiating_pane,
      ViewportShadingMode current_shading, QuaderEditorApi &editor,
      ViewportInputRouter &input_router, LocalizationService &localization,
      ViewportRuntimeBindingToken runtime_binding = {}) const;
};

} // namespace quader
