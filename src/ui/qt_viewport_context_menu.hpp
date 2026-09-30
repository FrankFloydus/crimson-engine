////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"

#include <QString>
#include <optional>
#include <string>
#include <vector>

class QPoint;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class ViewportInputRouter;

/// One canonical RMB command projected without pointer-target-only transfers.
struct QtViewportContextCommand final {
  std::string command_id;
  QString label;
  ViewportContextMenuActionSection section =
      ViewportContextMenuActionSection::Topology;
};

/// Collects selected-operand RMB commands using the context menu's own policies.
[[nodiscard]] std::vector<QtViewportContextCommand>
qt_viewport_context_commands(QuaderEditorApi &editor,
                             const QuaderSelectionSummary &selection,
                             const LocalizationService &localization);

/**
 * Presents the modeling operations available at a viewport position through a
 * stock Qt context menu. Modeling availability and execution remain owned by
 * the public editor API.
 */
class QtViewportContextMenuPresenter final {
public:
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  void show(QWidget &parent, const QPoint &global_point,
            QuaderEditorApi &editor, ViewportInputRouter &input_router,
            LocalizationService &localization,
            const QuaderActionSet &action_set,
            const QuaderSelectionSummary &selection, bool target_hit,
            const std::optional<QuaderHoverTarget> &material_face_target,
            ViewportRuntimeBindingToken runtime_binding = {}) const;

private:
  QtUiScaleMetrics ui_scale_metrics_;
};

} // namespace quader
