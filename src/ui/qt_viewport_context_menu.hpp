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

/// One selected-operand command supplied to the Q menu below its radial actions.
struct QtViewportContextCommand final {
  std::string command_id;
  QString label;
  ViewportContextMenuActionSection section =
      ViewportContextMenuActionSection::Topology;
};

/// Collects the Q menu supplement through shared operand and command policies.
[[nodiscard]] std::vector<QtViewportContextCommand>
qt_viewport_context_commands(QuaderEditorApi &editor,
                             const QuaderSelectionSummary &selection,
                             const LocalizationService &localization);

/**
 * Presents pointer-specific face transfers and curve solidification through a
 * stock Qt context menu. Generic operations remain in Q; modeling availability
 * and execution remain owned by the public editor API.
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
