////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"
#include "api/quader_viewport_layout_types.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"

class QPoint;
class QWidget;

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class ViewportInputRouter;

/** Projects existing modeling commands into a gesture menu, fencing dispatch. */
class QtViewportMarkingMenuPresenter final {
public:
  [[nodiscard]] bool show(
      QWidget &focus_parent, const QPoint &global_point,
      ViewportPaneId initiating_pane, const QuaderInputChord &initiating_chord,
      QuaderEditorApi &editor, ViewportInputRouter &input_router,
      LocalizationService &localization,
      const QtUiScaleMetrics &metrics = QtUiScaleMetrics{},
      ViewportRuntimeBindingToken runtime_binding = {}) const;
};

} // namespace quader
